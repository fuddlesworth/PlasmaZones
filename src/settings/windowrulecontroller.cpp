// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulecontroller.h"

#include "dbusutils.h"

#include "../pz_i18n.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
using PhosphorWindowRule::ActionRegistry;
using PhosphorWindowRule::Field;
using PhosphorWindowRule::MatchExpression;
using PhosphorWindowRule::Operator;
using PhosphorWindowRule::RuleAction;
using PhosphorWindowRule::WindowRule;
using PhosphorWindowRule::WindowRuleSet;

/// Priority bands. Context rules keep the migration's cascade bands;
/// application/animation rules are list-ordered within their band so the
/// drag-to-reorder is meaningful.
constexpr int kContextBandBase = 300;
constexpr int kApplicationBandBase = 200;
constexpr int kAnimationBandBase = 100;
constexpr int kAdvancedBandBase = 500;

/// Convert a JSON map (QML's QVariantMap) into a WindowRule. Returns a
/// null-id rule on a malformed map.
WindowRule ruleFromVariant(const QVariantMap& map)
{
    const QJsonObject obj = QJsonObject::fromVariantMap(map);
    const auto rule = WindowRule::fromJson(obj);
    return rule.value_or(WindowRule{});
}

/// Append every ScreenId leaf value found anywhere in @p expr to @p out.
/// Plain recursive walk — no per-call std::function allocation.
void collectScreenIds(const MatchExpression& expr, QStringList& out)
{
    if (expr.isLeaf()) {
        if (expr.predicate().field == Field::ScreenId) {
            const QString value = expr.predicate().value.toString();
            if (!value.isEmpty()) {
                out.append(value);
            }
        }
        return;
    }
    for (const MatchExpression& child : expr.children()) {
        collectScreenIds(child, out);
    }
}

QString operatorLabel(Operator op)
{
    switch (op) {
    case Operator::Equals:
        return PzI18n::tr("is");
    case Operator::Contains:
        return PzI18n::tr("contains");
    case Operator::StartsWith:
        return PzI18n::tr("starts with");
    case Operator::EndsWith:
        return PzI18n::tr("ends with");
    case Operator::Regex:
        return PzI18n::tr("matches regex");
    case Operator::AppIdMatches:
        return PzI18n::tr("matches app-id");
    case Operator::In:
        return PzI18n::tr("is one of");
    case Operator::GreaterThan:
        return PzI18n::tr("greater than");
    case Operator::LessThan:
        return PzI18n::tr("less than");
    }
    return QString();
}

/// Build one parameter descriptor for the action editor. @p kind is one of
/// "string" / "number" / "enum" / "percent"; the optional trailing fields are
/// kind-specific (see WindowRuleController::actionTypes doc).
QVariantMap paramDescriptor(QLatin1StringView key, const QString& kind, const QString& label)
{
    QVariantMap p;
    p[QStringLiteral("key")] = QString::fromLatin1(key);
    p[QStringLiteral("kind")] = kind;
    p[QStringLiteral("label")] = label;
    return p;
}

/// The parameter schema for @p type — the editor `Loader` is driven entirely
/// off this, so the per-type `if (t === "...")` ladder lives in C++ only.
QVariantList paramsForActionType(QLatin1StringView type)
{
    QVariantList params;
    if (type == ActionType::SetEngineMode) {
        QVariantMap p = paramDescriptor(QLatin1String("mode"), QStringLiteral("enum"), PzI18n::tr("Engine mode"));
        p[QStringLiteral("options")] = QStringList{QStringLiteral("snapping"), QStringLiteral("autotile")};
        params.append(p);
    } else if (type == ActionType::SetSnappingLayout) {
        params.append(
            paramDescriptor(QLatin1String("layoutId"), QStringLiteral("string"), PzI18n::tr("Snapping layout id")));
    } else if (type == ActionType::SetTilingAlgorithm) {
        params.append(
            paramDescriptor(QLatin1String("algorithm"), QStringLiteral("string"), PzI18n::tr("Tiling algorithm id")));
    } else if (type == ActionType::SetOpacity) {
        // The wire value is a 0.0–1.0 fraction; the editor shows a 0–100
        // percentage, so the stored value is `display * scale`.
        QVariantMap p =
            paramDescriptor(QLatin1String("value"), QStringLiteral("percent"), PzI18n::tr("Opacity percentage"));
        p[QStringLiteral("min")] = 0;
        p[QStringLiteral("max")] = 100;
        p[QStringLiteral("scale")] = 0.01;
        params.append(p);
    } else if (type == ActionType::OverrideAnimationShader || type == ActionType::OverrideAnimationTiming) {
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("string"),
                                      PzI18n::tr("Event path, e.g. window.open")));
    }
    // float / disableEngine / exclude carry no parameters — empty list.
    return params;
}

QString actionTypeLabel(QLatin1StringView type)
{
    if (type == ActionType::SetEngineMode) {
        return PzI18n::tr("Set engine mode");
    }
    if (type == ActionType::SetSnappingLayout) {
        return PzI18n::tr("Set snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm) {
        return PzI18n::tr("Set tiling algorithm");
    }
    if (type == ActionType::DisableEngine) {
        return PzI18n::tr("Disable engine");
    }
    if (type == ActionType::Exclude) {
        return PzI18n::tr("Exclude window");
    }
    if (type == ActionType::Float) {
        return PzI18n::tr("Float window");
    }
    if (type == ActionType::OverrideAnimationShader) {
        return PzI18n::tr("Override animation shader");
    }
    if (type == ActionType::OverrideAnimationTiming) {
        return PzI18n::tr("Override animation timing");
    }
    if (type == ActionType::SetOpacity) {
        return PzI18n::tr("Set opacity");
    }
    return QString::fromLatin1(type);
}

} // namespace

WindowRuleController::WindowRuleController(QObject* parent)
    : QObject(parent)
{
    // Re-fetch from the daemon whenever it broadcasts a rule change — but only
    // while the page is clean, so a daemon-side write doesn't silently stomp
    // the user's unsaved staged edits.
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::WindowRules),
                                          QStringLiteral("rulesChanged"), this, SLOT(reload()));
    reload();
}

WindowRuleController::~WindowRuleController() = default;

void WindowRuleController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void WindowRuleController::markDirty()
{
    // Flip the dirty bit to true (and emit dirtyChanged on a transition).
    setDirty(true);
}

void WindowRuleController::setDaemonReachable(bool reachable)
{
    if (m_daemonReachable == reachable) {
        return;
    }
    m_daemonReachable = reachable;
    Q_EMIT daemonReachableChanged();
}

std::optional<WindowRuleSet> WindowRuleController::fetchFromDaemon()
{
    const QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::WindowRules),
                                                      QStringLiteral("getAllRules"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        setDaemonReachable(false);
        return std::nullopt;
    }
    setDaemonReachable(true);
    const QString json = reply.arguments().constFirst().toString();
    if (json.isEmpty()) {
        // Daemon answered but has no store — treat as an empty set.
        return WindowRuleSet{};
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return WindowRuleSet{};
    }
    return WindowRuleSet::fromJson(doc.object()).value_or(WindowRuleSet{});
}

bool WindowRuleController::pushToDaemon(const QList<WindowRule>& rules)
{
    WindowRuleSet set;
    // setRules() drops invalid rules with a logged diagnostic and returns the
    // accepted count. A partial drop means the daemon would persist fewer
    // rules than the user staged — treat that as a failed push so the page
    // stays dirty rather than silently telling the user everything saved.
    const int accepted = set.setRules(rules);
    const QJsonDocument doc(set.toJson());
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::WindowRules),
                               QStringLiteral("setAllRules"), {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        setDaemonReachable(false);
        return false;
    }
    setDaemonReachable(true);
    const bool daemonAccepted = !reply.arguments().isEmpty() && reply.arguments().constFirst().toBool();
    return daemonAccepted && accepted == rules.size();
}

void WindowRuleController::fetchAndLoad()
{
    const auto fetched = fetchFromDaemon();
    if (fetched) {
        m_model.setRules(fetched->rules());
    } else if (m_model.rowCount() == 0) {
        // Daemon down and nothing loaded — leave the model empty.
        m_model.setRules({});
    }
    setDirty(false);
}

void WindowRuleController::reload()
{
    // A daemon `rulesChanged` broadcast routes here. If the user has unsaved
    // staged edits, re-fetching would stomp them — skip the refresh and keep
    // the staged set. The explicit revert()/commit() paths call fetchAndLoad()
    // directly so they bypass this guard.
    if (m_dirty) {
        return;
    }
    fetchAndLoad();
}

bool WindowRuleController::commit()
{
    if (!m_dirty) {
        return true;
    }
    if (!pushToDaemon(m_model.rules())) {
        // The push failed (daemon down or a partial drop). Keep the dirty bit
        // set so the user can retry; tell SettingsController to keep the page
        // dirty too.
        Q_EMIT commitFailed();
        return false;
    }
    setDirty(false);
    return true;
}

void WindowRuleController::revert()
{
    // Discard staged edits unconditionally — bypass reload()'s dirty guard.
    fetchAndLoad();
}

void WindowRuleController::renormalizePriorities()
{
    // Walk the model's list order and re-stamp priority so list order maps
    // monotonically onto evaluation order. Higher list index ⇒ lower
    // priority within the rule's band; the bands keep the section ordering
    // (Advanced > Monitor > Application > Animation) stable.
    //
    // Only the priority changes — push the new values through setPriorities()
    // so the model emits a single dataChanged(PriorityRole) instead of a full
    // reset (which would tear down and rebuild every QML delegate).
    const QList<WindowRule>& rules = m_model.rules();
    const int n = rules.size();
    QList<int> priorities;
    priorities.reserve(n);
    for (int i = 0; i < n; ++i) {
        int base = kAnimationBandBase;
        switch (WindowRuleModel::sectionFor(rules.at(i))) {
        case WindowRuleModel::Section::Advanced:
            base = kAdvancedBandBase;
            break;
        case WindowRuleModel::Section::Monitor:
        case WindowRuleModel::Section::Activity:
            base = kContextBandBase;
            break;
        case WindowRuleModel::Section::Application:
            base = kApplicationBandBase;
            break;
        case WindowRuleModel::Section::Animation:
            base = kAnimationBandBase;
            break;
        }
        // Earlier list index ⇒ slightly higher priority within the band.
        priorities.append(base + (n - i));
    }
    m_model.setPriorities(priorities);
}

QVariantMap WindowRuleController::newEmptyRule(const QString& subject) const
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.enabled = true;

    if (subject == QLatin1String("monitor")) {
        rule.name = PzI18n::tr("New monitor rule");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
    } else if (subject == QLatin1String("application")) {
        rule.name = PzI18n::tr("New application rule");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
    } else if (subject == QLatin1String("activity")) {
        rule.name = PzI18n::tr("New activity rule");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QString());
    } else {
        // "custom" — start from the always-true catch-all so the user builds
        // the tree from scratch in the Advanced editor.
        rule.name = PzI18n::tr("New custom rule");
        rule.priority = kAdvancedBandBase;
        rule.match = MatchExpression{};
    }
    return rule.toJson().toVariantMap();
}

QString WindowRuleController::addRuleFromJson(const QVariantMap& ruleJson)
{
    // The editor sheet may hand back a map with no id (a never-stored rule).
    // Inject a fresh UUID into the JSON *before* WindowRule::fromJson runs —
    // fromJson rejects an id-less object outright, so generating the id after
    // the fact would silently drop the name / match / actions the user built.
    QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);
    const QString idStr = obj.value(QLatin1String("id")).toString();
    if (idStr.isEmpty() || QUuid::fromString(idStr).isNull()) {
        obj.insert(QLatin1String("id"), QUuid::createUuid().toString());
    }
    const auto parsed = WindowRule::fromJson(obj);
    if (!parsed || !parsed->isValid() || !m_model.addRule(*parsed)) {
        return QString();
    }
    markDirty();
    return parsed->id.toString();
}

bool WindowRuleController::updateRuleFromJson(const QVariantMap& ruleJson)
{
    const WindowRule rule = ruleFromVariant(ruleJson);
    if (rule.id.isNull() || !m_model.updateRule(rule)) {
        return false;
    }
    markDirty();
    return true;
}

bool WindowRuleController::removeRule(const QString& ruleId)
{
    if (!m_model.removeRule(QUuid::fromString(ruleId))) {
        return false;
    }
    markDirty();
    return true;
}

bool WindowRuleController::setRuleEnabled(const QString& ruleId, bool enabled)
{
    WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull() || rule.enabled == enabled) {
        return false;
    }
    rule.enabled = enabled;
    if (!m_model.updateRule(rule)) {
        return false;
    }
    markDirty();
    return true;
}

bool WindowRuleController::moveRule(const QString& ruleId, const QString& beforeRuleId)
{
    if (!m_model.moveRule(QUuid::fromString(ruleId), QUuid::fromString(beforeRuleId))) {
        return false;
    }
    renormalizePriorities();
    markDirty();
    return true;
}

QVariantMap WindowRuleController::ruleJson(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    return rule.id.isNull() ? QVariantMap{} : rule.toJson().toVariantMap();
}

QVariantList WindowRuleController::sections() const
{
    // Canonical display order — Monitor & Layout first, Advanced last. The
    // enum values are emitted as data so QML never hardcodes them.
    static const QList<WindowRuleModel::Section> kOrder = {
        WindowRuleModel::Section::Monitor,   WindowRuleModel::Section::Application, WindowRuleModel::Section::Activity,
        WindowRuleModel::Section::Animation, WindowRuleModel::Section::Advanced,
    };
    QVariantList out;
    for (WindowRuleModel::Section s : kOrder) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(s);
        entry[QStringLiteral("label")] = WindowRuleModel::sectionLabel(s);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::rulesSnapshot() const
{
    // Read every field through the model's own data() + role enum so the
    // section / summary logic stays in exactly one place and QML never has to
    // reference raw `Qt.UserRole + N` integers.
    QVariantList out;
    const int n = m_model.rowCount();
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = m_model.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("ruleId")] = m_model.data(idx, WindowRuleModel::IdRole);
        entry[QStringLiteral("name")] = m_model.data(idx, WindowRuleModel::NameRole);
        entry[QStringLiteral("enabled")] = m_model.data(idx, WindowRuleModel::EnabledRole);
        entry[QStringLiteral("section")] =
            static_cast<int>(m_model.data(idx, WindowRuleModel::SectionRole).value<WindowRuleModel::Section>());
        entry[QStringLiteral("matchSummary")] = m_model.data(idx, WindowRuleModel::MatchSummaryRole);
        entry[QStringLiteral("actionSummary")] = m_model.data(idx, WindowRuleModel::ActionSummaryRole);
        entry[QStringLiteral("conditionCount")] = m_model.data(idx, WindowRuleModel::ConditionCountRole);
        entry[QStringLiteral("actionCount")] = m_model.data(idx, WindowRuleModel::ActionCountRole);
        entry[QStringLiteral("isComposite")] = m_model.data(idx, WindowRuleModel::IsCompositeRole);
        QStringList screenIds;
        collectScreenIds(
            m_model.ruleById(QUuid::fromString(m_model.data(idx, WindowRuleModel::IdRole).toString())).match,
            screenIds);
        entry[QStringLiteral("screenIds")] = screenIds;
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::monitorOverview(const QVariantList& screens) const
{
    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // Settings screen maps key the connector under "name" (and sometimes
        // "id"); accept either so the overview never silently drops a tile.
        QString screenId = screen.value(QStringLiteral("name")).toString();
        if (screenId.isEmpty()) {
            screenId = screen.value(QStringLiteral("id")).toString();
        }
        if (screenId.isEmpty()) {
            continue;
        }

        int ruleCount = 0;
        bool tilingEnabled = true;
        bool assigned = false;
        QString layoutName;

        for (const WindowRule& rule : m_model.rules()) {
            // Only context-only rules that pin this exact monitor count
            // toward the overview tile.
            if (!rule.match.isContextOnly()) {
                continue;
            }
            // Walk the (flat or nested) match for a ScreenId == screenId leaf.
            QStringList screenIds;
            collectScreenIds(rule.match, screenIds);
            if (!screenIds.contains(screenId)) {
                continue;
            }
            ++ruleCount;
            assigned = true;
            for (const RuleAction& a : rule.actions) {
                if (a.type == ActionType::DisableEngine) {
                    tilingEnabled = false;
                }
                if (a.type == ActionType::SetSnappingLayout && layoutName.isEmpty()) {
                    layoutName = a.params.value(QLatin1String("layoutId")).toString();
                }
                if (a.type == ActionType::SetTilingAlgorithm && layoutName.isEmpty()) {
                    layoutName = a.params.value(QLatin1String("algorithm")).toString();
                }
            }
        }

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        tile[QStringLiteral("layoutName")] = layoutName;
        tile[QStringLiteral("tilingEnabled")] = tilingEnabled;
        tile[QStringLiteral("ruleCount")] = ruleCount;
        tile[QStringLiteral("assigned")] = assigned;
        out.append(tile);
    }
    return out;
}

QStringList WindowRuleController::ruleScreenIds(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull()) {
        return {};
    }
    QStringList out;
    collectScreenIds(rule.match, out);
    return out;
}

QVariantList WindowRuleController::matchFields() const
{
    static const QList<Field> kFields = {
        Field::AppId,    Field::WindowClass,    Field::DesktopFile, Field::WindowRole,   Field::Pid,
        Field::Title,    Field::WindowType,     Field::IsSticky,    Field::IsFullscreen, Field::IsMinimized,
        Field::ScreenId, Field::VirtualDesktop, Field::Activity,
    };
    QVariantList out;
    for (Field f : kFields) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(f);
        // The JSON wire string for this field — QML keys off this rather than
        // reconstructing the enum↔string table itself.
        entry[QStringLiteral("wire")] = PhosphorWindowRule::fieldToString(f);
        entry[QStringLiteral("label")] = WindowRuleModel::fieldLabel(f);
        QString kind = QStringLiteral("string");
        if (PhosphorWindowRule::fieldIsNumeric(f) || f == Field::WindowType) {
            kind = QStringLiteral("number");
        } else if (PhosphorWindowRule::fieldIsBool(f)) {
            kind = QStringLiteral("bool");
        }
        entry[QStringLiteral("valueKind")] = kind;
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::operatorsForField(int fieldValue) const
{
    const Field field = static_cast<Field>(fieldValue);
    QList<Operator> ops;
    if (PhosphorWindowRule::fieldIsString(field)) {
        ops = {Operator::Equals, Operator::Contains, Operator::StartsWith, Operator::EndsWith, Operator::Regex};
        if (field == Field::AppId) {
            ops.append(Operator::AppIdMatches);
        }
        if (field == Field::ScreenId || field == Field::Activity) {
            ops.append(Operator::In);
        }
    } else if (PhosphorWindowRule::fieldIsNumeric(field)) {
        ops = {Operator::Equals, Operator::GreaterThan, Operator::LessThan};
        if (field == Field::VirtualDesktop) {
            ops.append(Operator::In);
        }
    } else if (PhosphorWindowRule::fieldIsBool(field) || field == Field::WindowType) {
        ops = {Operator::Equals};
        if (field == Field::WindowType) {
            ops.append(Operator::In);
        }
    }
    QVariantList out;
    for (Operator op : ops) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(op);
        // The JSON wire string for this operator — same contract as matchFields.
        entry[QStringLiteral("wire")] = PhosphorWindowRule::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabel(op);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::actionTypes() const
{
    static const QList<QLatin1StringView> kTypes = {
        ActionType::SetEngineMode,
        ActionType::SetSnappingLayout,
        ActionType::SetTilingAlgorithm,
        ActionType::DisableEngine,
        ActionType::Exclude,
        ActionType::Float,
        ActionType::SetOpacity,
        ActionType::OverrideAnimationShader,
        ActionType::OverrideAnimationTiming,
    };
    QVariantList out;
    for (QLatin1StringView type : kTypes) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = QString::fromLatin1(type);
        entry[QStringLiteral("label")] = actionTypeLabel(type);
        entry[QStringLiteral("params")] = paramsForActionType(type);
        out.append(entry);
    }
    return out;
}

} // namespace PlasmaZones
