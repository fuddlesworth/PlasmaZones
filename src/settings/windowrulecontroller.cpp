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

#include <functional>

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

/// Human label for a match Field — mirror of WindowRuleModel's private helper
/// (kept local so the two stay decoupled).
QString ctrlFieldLabel(Field field)
{
    switch (field) {
    case Field::AppId:
        return PzI18n::tr("Application");
    case Field::WindowClass:
        return PzI18n::tr("Window class");
    case Field::DesktopFile:
        return PzI18n::tr("Desktop file");
    case Field::WindowRole:
        return PzI18n::tr("Window role");
    case Field::Pid:
        return PzI18n::tr("Process ID");
    case Field::Title:
        return PzI18n::tr("Title");
    case Field::WindowType:
        return PzI18n::tr("Window type");
    case Field::IsSticky:
        return PzI18n::tr("Sticky");
    case Field::IsFullscreen:
        return PzI18n::tr("Fullscreen");
    case Field::IsMinimized:
        return PzI18n::tr("Minimized");
    case Field::ScreenId:
        return PzI18n::tr("Monitor");
    case Field::VirtualDesktop:
        return PzI18n::tr("Desktop");
    case Field::Activity:
        return PzI18n::tr("Activity");
    }
    return QString();
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
    set.setRules(rules);
    const QJsonDocument doc(set.toJson());
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::WindowRules),
                               QStringLiteral("setAllRules"), {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        setDaemonReachable(false);
        return false;
    }
    setDaemonReachable(true);
    return reply.arguments().isEmpty() ? false : reply.arguments().constFirst().toBool();
}

void WindowRuleController::reload()
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

void WindowRuleController::commit()
{
    if (!m_dirty) {
        return;
    }
    if (pushToDaemon(m_model.rules())) {
        setDirty(false);
    }
    // On a failed push the dirty bit stays set so the user can retry.
}

void WindowRuleController::revert()
{
    reload();
}

void WindowRuleController::renormalizePriorities()
{
    // Walk the model's list order and re-stamp priority so list order maps
    // monotonically onto evaluation order. Higher list index ⇒ lower
    // priority within the rule's band; the bands keep the section ordering
    // (Advanced > Monitor > Application > Animation) stable.
    QList<WindowRule> rules = m_model.rules();
    const int n = rules.size();
    for (int i = 0; i < n; ++i) {
        WindowRule& rule = rules[i];
        int base = kAnimationBandBase;
        switch (WindowRuleModel::sectionFor(rule)) {
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
        rule.priority = base + (n - i);
    }
    m_model.setRules(rules);
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
    WindowRule rule = ruleFromVariant(ruleJson);
    if (rule.id.isNull()) {
        rule.id = QUuid::createUuid();
    }
    if (!rule.isValid() || !m_model.addRule(rule)) {
        return QString();
    }
    markDirty();
    return rule.id.toString();
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

bool WindowRuleController::setRulePriority(const QString& ruleId, int priority)
{
    WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull() || rule.priority == priority) {
        return false;
    }
    rule.priority = priority;
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
            bool pinsThisScreen = false;
            std::function<void(const MatchExpression&)> scan = [&](const MatchExpression& e) {
                if (e.isLeaf()) {
                    if (e.predicate().field == Field::ScreenId && e.predicate().value.toString() == screenId) {
                        pinsThisScreen = true;
                    }
                    return;
                }
                for (const MatchExpression& c : e.children()) {
                    scan(c);
                }
            };
            scan(rule.match);
            if (!pinsThisScreen) {
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
        entry[QStringLiteral("label")] = ctrlFieldLabel(f);
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
        out.append(entry);
    }
    return out;
}

} // namespace PlasmaZones
