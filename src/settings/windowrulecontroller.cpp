// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulecontroller.h"

#include "dbusutils.h"

#include "../core/logging.h"
#include "../pz_i18n.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
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

/// Enum-option list for the snapping/autotile engine-mode pickers. Shared by
/// `SetEngineMode` and `DisableEngine`: both pick from the same lowercase
/// wire tokens but show properly-cased UI labels.
QVariantList engineModeOptions()
{
    QVariantList options;
    QVariantMap snap;
    snap[QStringLiteral("value")] = QStringLiteral("snapping");
    snap[QStringLiteral("label")] = PzI18n::tr("Snapping");
    options.append(snap);
    QVariantMap tile;
    tile[QStringLiteral("value")] = QStringLiteral("autotile");
    tile[QStringLiteral("label")] = PzI18n::tr("Autotile");
    options.append(tile);
    return options;
}

/// The parameter schema for @p type — the editor `Loader` is driven entirely
/// off this, so the per-type `if (t === "...")` ladder lives in C++ only.
QVariantList paramsForActionType(QLatin1StringView type)
{
    QVariantList params;
    if (type == ActionType::SetEngineMode) {
        QVariantMap p = paramDescriptor(QLatin1String("mode"), QStringLiteral("enum"), PzI18n::tr("Engine mode"));
        p[QStringLiteral("options")] = engineModeOptions();
        params.append(p);
    } else if (type == ActionType::SetSnappingLayout) {
        // `snappingLayout` is the picker-aware kind the QML editor recognises;
        // it swaps in a ComboBox over `settingsController.layouts` so the
        // user picks "Grid (2x2)" instead of pasting "{25828c9b-…}".
        params.append(paramDescriptor(QLatin1String("layoutId"), QStringLiteral("snappingLayout"),
                                      PzI18n::tr("Snapping layout")));
    } else if (type == ActionType::SetTilingAlgorithm) {
        // Tiling algorithms are still string tokens (`bsp`, `grid`, …) — the
        // editor offers a ComboBox over the catalogue but stores the token
        // verbatim.
        params.append(paramDescriptor(QLatin1String("algorithm"), QStringLiteral("tilingAlgorithm"),
                                      PzI18n::tr("Tiling algorithm")));
    } else if (type == ActionType::DisableEngine) {
        // Validator requires `mode` ∈ {snapping, autotile}; without a picker
        // the user couldn't author the action. Same enum shape as
        // SetEngineMode.
        QVariantMap p = paramDescriptor(QLatin1String("mode"), QStringLiteral("enum"), PzI18n::tr("Engine to disable"));
        p[QStringLiteral("options")] = engineModeOptions();
        params.append(p);
    } else if (type == ActionType::SetOpacity) {
        // The wire value is a 0.0–1.0 fraction; the editor shows a 0–100
        // percentage, so the stored value is `display * scale`.
        QVariantMap p =
            paramDescriptor(QLatin1String("value"), QStringLiteral("percent"), PzI18n::tr("Opacity percentage"));
        p[QStringLiteral("min")] = 0;
        p[QStringLiteral("max")] = 100;
        p[QStringLiteral("scale")] = 0.01;
        params.append(p);
    } else if (type == ActionType::OverrideAnimationShader) {
        // Wire keys must match PhosphorWindowRule::ActionRegistry's
        // OverrideAnimationShader descriptor (`event`, `effectId`, `params`).
        // `params` is a free-form shader-uniform object — not authorable
        // through a flat key/kind descriptor, so it is intentionally omitted;
        // a shader-uniform editor would graduate the rule to Advanced.
        //
        // `animationEvent` / `shaderEffect` are picker-aware kinds the QML
        // editor recognises — they swap ComboBoxes driven by
        // `AnimationsPageController::eventSections()` and
        // `availableShaderEffects()` in place of the freeform string field.
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        params.append(
            paramDescriptor(QLatin1String("effectId"), QStringLiteral("shaderEffect"), PzI18n::tr("Shader effect")));
    } else if (type == ActionType::OverrideAnimationTiming) {
        // Duration-only override. Curve lives in `OverrideAnimationCurve`
        // (separate slot) so the user can override curve and duration
        // independently per event. The descriptor still allows `curve` for
        // back-compat with legacy rules; the editor doesn't expose it here.
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        QVariantMap p =
            paramDescriptor(QLatin1String("durationMs"), QStringLiteral("number"), PzI18n::tr("Duration (ms)"));
        p[QStringLiteral("min")] = 0;
        p[QStringLiteral("max")] = 60000;
        params.append(p);
    } else if (type == ActionType::OverrideAnimationCurve) {
        // Curve-only override. `OverrideAnimationDuration` lives under
        // `OverrideAnimationTiming` (kept as the duration-only override).
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        params.append(paramDescriptor(QLatin1String("curve"), QStringLiteral("curveEditor"), PzI18n::tr("Curve")));
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
        return PzI18n::tr("Override animation duration");
    }
    if (type == ActionType::OverrideAnimationCurve) {
        return PzI18n::tr("Override animation curve");
    }
    if (type == ActionType::SetOpacity) {
        return PzI18n::tr("Set opacity");
    }
    return WindowRuleModel::actionTypeFallbackLabel(QString::fromLatin1(type));
}

} // namespace

WindowRuleController::WindowRuleController(QObject* parent)
    : QObject(parent)
{
    // Re-fetch from the daemon whenever it broadcasts a rule change — but only
    // while the page is clean, so a daemon-side write doesn't silently stomp
    // the user's unsaved staged edits.
    //
    // `QDBusConnection::connect` accepts only the const-char* SLOT form (no
    // member-pointer overload), so the SLOT() macro is canonical here. Capture
    // the return so a failed subscription (e.g. service not yet up) is
    // diagnosable rather than a silent miss — the deferred reload below still
    // fetches the initial state, but without this subscription the page would
    // never re-sync on subsequent daemon-side writes.
    const bool subscribed = QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::WindowRules), QStringLiteral("rulesChanged"), this,
        SLOT(reload()));
    if (!subscribed) {
        qCWarning(lcConfig) << "WindowRuleController: failed to subscribe to org.plasmazones.WindowRules.rulesChanged "
                               "— page will not refresh on daemon-side writes";
    }
    // Defer the initial fetch to the next event-loop tick. fetchFromDaemon()
    // issues a synchronous blocking QDBusConnection::call() (up to ~500 ms on
    // a timeout); running it inline in the constructor would stall settings-app
    // startup whenever the daemon is slow or absent.
    QTimer::singleShot(0, this, &WindowRuleController::reload);
}

WindowRuleController::~WindowRuleController()
{
    // Mirror the `connect()` from the constructor so the bus doesn't keep
    // a slot binding referencing this destroyed instance. Settings-app
    // lifetime is process-wide today, but the symmetric disconnect makes
    // the controller safe to instantiate from a transient owner (tests,
    // future KCM page where the controller is rebuilt on demand) without
    // leaking dangling slot dispatches.
    QDBusConnection::sessionBus().disconnect(QString(PhosphorProtocol::Service::Name),
                                             QString(PhosphorProtocol::Service::ObjectPath),
                                             QString(PhosphorProtocol::Service::Interface::WindowRules),
                                             QStringLiteral("rulesChanged"), this, SLOT(reload()));
}

void WindowRuleController::setScreenLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setScreenLabelLookup(std::move(fn));
}

void WindowRuleController::setActivityLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setActivityLabelLookup(std::move(fn));
}

void WindowRuleController::setLayoutLookup(WindowRuleModel::LabelLookup fn)
{
    m_layoutLookup = fn;
    // Also forward to the model so its `actionSummary` can resolve layoutId /
    // algorithm-token wire values when building the rule-list captions.
    m_model.setLayoutLabelLookup(std::move(fn));
}

void WindowRuleController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void WindowRuleController::setDaemonReachable(bool reachable)
{
    if (m_daemonReachable == reachable) {
        return;
    }
    m_daemonReachable = reachable;
    Q_EMIT daemonReachableChanged();
}

void WindowRuleController::setDaemonChangedWhileDirty(bool changed)
{
    if (m_daemonChangedWhileDirty == changed) {
        return;
    }
    m_daemonChangedWhileDirty = changed;
    Q_EMIT daemonChangedWhileDirtyChanged();
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
    // accepted count. Bail out BEFORE pushing if any rule was rejected — the
    // earlier code would push a truncated set to the daemon and only fail
    // the return, leaving the daemon persisting a divergent rule set the
    // model would never refresh (the next reload is guarded by the dirty
    // bit, which stays set on failure → permanent divergence).
    const int accepted = set.setRules(rules);
    if (accepted != rules.size()) {
        qCWarning(lcConfig) << "WindowRuleController::pushToDaemon: rejecting push —" << (rules.size() - accepted)
                            << "of" << rules.size() << "rules failed client-side validation; daemon NOT updated";
        return false;
    }
    const QJsonDocument doc(set.toJson());
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::WindowRules),
                               QStringLiteral("setAllRules"), {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        setDaemonReachable(false);
        return false;
    }
    setDaemonReachable(true);
    return !reply.arguments().isEmpty() && reply.arguments().constFirst().toBool();
}

bool WindowRuleController::fetchAndLoad()
{
    const auto fetched = fetchFromDaemon();
    if (!fetched) {
        // Daemon unreachable — there is nothing authoritative to load. Leave
        // the model and the dirty bit untouched and report failure so the
        // caller does not pretend a reload happened. fetchFromDaemon() has
        // already cleared `daemonReachable`.
        return false;
    }
    m_model.setRules(fetched->rules());
    setDirty(false);
    setDaemonChangedWhileDirty(false);
    return true;
}

void WindowRuleController::reload()
{
    // A daemon `rulesChanged` broadcast routes here. If the user has unsaved
    // staged edits, re-fetching would stomp them — skip the refresh and keep
    // the staged set. The explicit revert() path calls fetchAndLoad()
    // directly so it bypasses this guard.
    if (m_dirty) {
        // The staged set is now divergent from the daemon's newer rules. Flag
        // it so the page can warn the user before a commit() silently
        // overwrites the daemon-side changes (lost update).
        setDaemonChangedWhileDirty(true);
        return;
    }
    fetchAndLoad();
}

bool WindowRuleController::commit(bool force)
{
    if (!m_dirty) {
        return true;
    }
    if (m_daemonChangedWhileDirty && !force) {
        // The daemon's rules changed under the staged edits. Pushing now would
        // silently overwrite the daemon's newer set (lost update). Refuse and
        // keep the page dirty — the page surfaces `daemonChangedWhileDirty` so
        // the user can review/revert or force the overwrite via commit(true).
        qCWarning(lcConfig) << "WindowRuleController::commit: refusing to push — daemon rules changed while the page "
                               "had staged edits; review or force-overwrite required";
        return false;
    }
    if (!pushToDaemon(m_model.rules())) {
        // The push failed (daemon down or a partial drop). Keep the dirty bit
        // set so the user can retry — the bool return tells
        // SettingsController::save() to keep the page dirty.
        return false;
    }
    setDirty(false);
    setDaemonChangedWhileDirty(false);
    return true;
}

bool WindowRuleController::revert()
{
    // Discard staged edits by re-fetching the daemon's authoritative set —
    // bypass reload()'s dirty guard. fetchAndLoad() only clears the dirty bit
    // if the daemon was reachable; a failed re-fetch keeps the page dirty
    // (and `daemonReachable` false) rather than silently dropping the staged
    // edits while telling the user the revert succeeded.
    return fetchAndLoad();
}

void WindowRuleController::renormalizePriorities()
{
    // Walk the model's list order and re-stamp priority so list order maps
    // monotonically onto evaluation order. Higher list index ⇒ lower
    // priority within the rule's band; the bands keep the section ordering
    // (Advanced > Monitor > Application > Animation) stable.
    //
    // This is invoked ONLY when list order changes — on a drag-reorder
    // (moveRule) and when a new rule is appended (addRuleFromJson). An in-place
    // edit (updateRuleFromJson) never reorders the list, so it does not call
    // this: the Advanced editor exposes `priority` directly, and an explicit
    // priority edit there is honoured verbatim rather than being overwritten by
    // the band-derived value. A non-priority in-place edit likewise leaves
    // priority untouched, so list order and PriorityRole stay consistent.
    //
    // Only the priority changes — push the new values through setPriorities()
    // so the model emits a single dataChanged(PriorityRole) instead of a full
    // reset (which would tear down and rebuild every QML delegate).
    const QList<WindowRule>& rules = m_model.rules();
    const int n = rules.size();
    QList<int> priorities;
    priorities.reserve(n);
    // Bands are spaced 100 apart (Animation=100, Application=200,
    // Context=300, Advanced=500). Without a per-band offset cap, a band
    // with >100 rules would have its later entries spill upward into the
    // next band — e.g. application rule #101 would get priority 200+100 =
    // 300, colliding with the Context band's base. Clamp the per-rule
    // offset so it stays strictly inside the 99-slot window owned by
    // each band; ties at the high end keep list order via the model's
    // stable sort (insertion order is preserved by `setPriorities`).
    constexpr int kBandWidth = 100;
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
        // Earlier list index ⇒ slightly higher priority within the band,
        // capped at `kBandWidth - 1` so we never spill into the next
        // band's range.
        const int offset = qMin(n - i, kBandWidth - 1);
        priorities.append(base + offset);
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
    } else if (subject == QLatin1String("desktop")) {
        rule.name = PzI18n::tr("New desktop rule");
        rule.priority = kContextBandBase;
        // VirtualDesktop is numeric; seed with 1 (typical first desktop).
        // Validator rejects 0 because 0 means "all desktops" and is the same
        // as having no predicate at all.
        rule.match = MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 1);
    } else if (subject == QLatin1String("application")) {
        rule.name = PzI18n::tr("New application rule");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
    } else if (subject == QLatin1String("activity")) {
        rule.name = PzI18n::tr("New activity rule");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QString());
    } else if (subject == QLatin1String("animation")) {
        rule.name = PzI18n::tr("New animation rule");
        rule.priority = kAnimationBandBase;
        // Animation overrides typically apply globally — the action carries
        // the event scope (see `anim-shader:`/`anim-timing:`/`anim-curve:`
        // slot prefixes). Start with an always-true match so the user goes
        // straight to picking event + override in the action editor.
        rule.match = MatchExpression{};
    } else {
        // "custom" — start from the always-true catch-all so the user builds
        // the tree from scratch in the Advanced editor.
        rule.name = PzI18n::tr("New custom rule");
        rule.priority = kAdvancedBandBase;
        rule.match = MatchExpression{};
    }
    return rule.toJson().toVariantMap();
}

QVariantList WindowRuleController::ruleTemplates() const
{
    auto entry = [](QLatin1StringView id, const QString& label, const QString& description, QLatin1StringView icon) {
        QVariantMap m;
        m[QStringLiteral("id")] = QString::fromLatin1(id);
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("description")] = description;
        m[QStringLiteral("icon")] = QString::fromLatin1(icon);
        return m;
    };

    // Templates mirror the flows the per-settings pages used to author
    // before the unified rule store: monitor → layout / algorithm
    // (assignments) and app → exclusion (per-mode disable + animation
    // exclusion lists). That's where the bulk of real-world rules live, so
    // these three give one-click starting points for the common cases.
    QVariantList out;
    out.append(entry(QLatin1String("layoutOnMonitor"), PzI18n::tr("Set a layout on a monitor"),
                     PzI18n::tr("Pick a snapping layout to use on one monitor."), QLatin1String("view-grid")));
    out.append(entry(QLatin1String("algorithmOnMonitor"), PzI18n::tr("Set a tiling algorithm on a monitor"),
                     PzI18n::tr("Pick an autotile algorithm to use on one monitor."), QLatin1String("view-list-tree")));
    out.append(entry(QLatin1String("excludeApp"), PzI18n::tr("Exclude an app from tiling"),
                     PzI18n::tr("Keep one application's windows out of the snap and autotile engines entirely."),
                     QLatin1String("edit-delete-remove")));
    return out;
}

QVariantMap WindowRuleController::newRuleFromTemplate(const QString& templateId) const
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.enabled = true;

    if (templateId == QLatin1String("layoutOnMonitor")) {
        rule.name = PzI18n::tr("Snapping layout on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Two seeded actions — set engine mode AND pick the layout — so the
        // editor opens with the same shape the old MonitorStatePage
        // assignment flow produced. The user fills in the screen and layout
        // pickers; the engine-mode is pre-set to "snapping" because the
        // template's whole point is the snap layout.
        RuleAction engineMode;
        engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
        engineMode.params.insert(QStringLiteral("mode"), QStringLiteral("snapping"));
        rule.actions.append(engineMode);
        RuleAction layoutAction;
        layoutAction.type = QString::fromLatin1(ActionType::SetSnappingLayout);
        layoutAction.params.insert(QStringLiteral("layoutId"), QString());
        rule.actions.append(layoutAction);
    } else if (templateId == QLatin1String("algorithmOnMonitor")) {
        rule.name = PzI18n::tr("Tiling algorithm on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Mirror of the layout template, but for the autotile engine + an
        // algorithm picker. Same rationale: this is the assignment flow.
        RuleAction engineMode;
        engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
        engineMode.params.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        rule.actions.append(engineMode);
        RuleAction algoAction;
        algoAction.type = QString::fromLatin1(ActionType::SetTilingAlgorithm);
        algoAction.params.insert(QStringLiteral("algorithm"), QString());
        rule.actions.append(algoAction);
    } else if (templateId == QLatin1String("excludeApp")) {
        rule.name = PzI18n::tr("Exclude an app from tiling");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::Exclude);
        rule.actions.append(action);
    } else {
        return {};
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
    if (!parsed || !parsed->isValid()) {
        qCWarning(lcConfig) << "WindowRuleController::addRuleFromJson: rejecting malformed rule payload";
        return QString();
    }
    if (!m_model.addRule(*parsed)) {
        qCWarning(lcConfig) << "WindowRuleController::addRuleFromJson: model rejected rule" << parsed->id.toString()
                            << "(id collision or invalid)";
        return QString();
    }
    // Re-stamp priorities so two rules added back-to-back in the same band do
    // not collide on the band-base priority — list order maps onto evaluation
    // order exactly as it does after a drag-reorder.
    renormalizePriorities();
    setDirty(true);
    return parsed->id.toString();
}

bool WindowRuleController::updateRuleFromJson(const QVariantMap& ruleJson)
{
    const WindowRule rule = ruleFromVariant(ruleJson);
    if (rule.id.isNull()) {
        // ruleFromVariant() yields a null-id rule when WindowRule::fromJson
        // rejects the payload (malformed map, missing/invalid id).
        qCWarning(lcConfig) << "WindowRuleController::updateRuleFromJson: rejecting malformed rule payload";
        return false;
    }
    const WindowRuleModel::UpdateResult result = m_model.updateRule(rule);
    switch (result) {
    case WindowRuleModel::UpdateResult::NotFound:
        qCWarning(lcConfig) << "WindowRuleController::updateRuleFromJson: no rule with id" << rule.id.toString();
        return false;
    case WindowRuleModel::UpdateResult::Unchanged:
        // A genuine no-op save — the rule is unchanged, so do not dirty the
        // page. Still report success: the caller's "save" intent succeeded.
        return true;
    case WindowRuleModel::UpdateResult::Applied:
        setDirty(true);
        return true;
    }
    return false;
}

bool WindowRuleController::removeRule(const QString& ruleId)
{
    if (!m_model.removeRule(QUuid::fromString(ruleId))) {
        return false;
    }
    setDirty(true);
    return true;
}

bool WindowRuleController::setRuleEnabled(const QString& ruleId, bool enabled)
{
    WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull() || rule.enabled == enabled) {
        return false;
    }
    rule.enabled = enabled;
    // The enabled-flag flip is guaranteed a real change (guarded above), so
    // anything other than Applied is a model-level failure.
    if (m_model.updateRule(rule) != WindowRuleModel::UpdateResult::Applied) {
        return false;
    }
    setDirty(true);
    return true;
}

bool WindowRuleController::moveRule(const QString& ruleId, const QString& beforeRuleId)
{
    if (!m_model.moveRule(QUuid::fromString(ruleId), QUuid::fromString(beforeRuleId))) {
        return false;
    }
    renormalizePriorities();
    setDirty(true);
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
        // ScreenIdsRole is computed in the model's data() — no per-row
        // by-id lookup, so the snapshot stays O(n).
        entry[QStringLiteral("screenIds")] = m_model.data(idx, WindowRuleModel::ScreenIdsRole);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::monitorOverview(const QVariantList& screens) const
{
    // Per-screen accumulator — built in a single pass over the rule set so the
    // total cost is O(rules × actions + screens) rather than O(screens × rules
    // × actions). A screen with no pinned rule simply never gets an entry and
    // falls through to the default "not assigned" tile below.
    struct Summary
    {
        int ruleCount = 0;
        bool tilingEnabled = true;
        QString snappingLayout;
        QString tilingAlgorithm;
        // Mode the rule's `SetEngineMode` action selects (if any). Used to
        // disambiguate which layout name the overview tile should show
        // when a rule carries BOTH a snapping layout and a tiling
        // algorithm — the engine mode is the source of truth for which
        // engine actually runs.
        QString engineMode;
    };
    QHash<QString, Summary> byScreen;

    for (const WindowRule& rule : m_model.rules()) {
        // Only context-only rules that pin a monitor count toward a tile.
        if (!rule.match.isContextOnly()) {
            continue;
        }
        const QStringList screenIds = WindowRuleModel::screenIdsOf(rule.match);
        if (screenIds.isEmpty()) {
            continue;
        }
        for (const QString& screenId : screenIds) {
            Summary& s = byScreen[screenId];
            ++s.ruleCount;
            for (const RuleAction& a : rule.actions) {
                if (a.type == ActionType::DisableEngine) {
                    s.tilingEnabled = false;
                } else if (a.type == ActionType::SetEngineMode && s.engineMode.isEmpty()) {
                    s.engineMode = a.params.value(QLatin1String("mode")).toString();
                } else if (a.type == ActionType::SetSnappingLayout && s.snappingLayout.isEmpty()) {
                    s.snappingLayout = a.params.value(QLatin1String("layoutId")).toString();
                } else if (a.type == ActionType::SetTilingAlgorithm && s.tilingAlgorithm.isEmpty()) {
                    s.tilingAlgorithm = a.params.value(QLatin1String("algorithm")).toString();
                }
            }
        }
    }

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

        const auto it = byScreen.constFind(screenId);
        const bool assigned = it != byScreen.constEnd();
        const Summary summary = assigned ? *it : Summary{};

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        // Pick the layout token that matches the rule's engine mode. When a
        // rule sets BOTH a snapping layout AND a tiling algorithm (legal —
        // they live in independent slots) the engine mode decides which
        // one is actually visible. Without an explicit engine mode we
        // prefer the snapping layout (the more common case) and fall back
        // to the algorithm so the tile is never blank when only one is
        // set.
        QString layoutLabel;
        if (summary.engineMode == QLatin1String("autotile") && !summary.tilingAlgorithm.isEmpty()) {
            layoutLabel = summary.tilingAlgorithm;
        } else if (summary.engineMode == QLatin1String("snapping") && !summary.snappingLayout.isEmpty()) {
            layoutLabel = summary.snappingLayout;
        } else if (!summary.snappingLayout.isEmpty()) {
            layoutLabel = summary.snappingLayout;
        } else {
            layoutLabel = summary.tilingAlgorithm;
        }
        // The token is the raw layoutId / algorithm name from the rule's
        // action params — resolve it to a user-facing label when a lookup
        // is wired so the tile reads "BSP" instead of "{25828c9b-…}".
        if (m_layoutLookup && !layoutLabel.isEmpty()) {
            const QString resolved = m_layoutLookup(layoutLabel);
            if (!resolved.isEmpty()) {
                layoutLabel = resolved;
            }
        }
        tile[QStringLiteral("layoutName")] = layoutLabel;
        tile[QStringLiteral("tilingEnabled")] = summary.tilingEnabled;
        tile[QStringLiteral("ruleCount")] = summary.ruleCount;
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
    return WindowRuleModel::screenIdsOf(rule.match);
}

QVariantList WindowRuleController::matchFields() const
{
    // Pid and WindowRole are intentionally omitted from the picker —
    // both are footguns in a persistent rule store:
    //   * Pid is ephemeral. A `Pid equals 12345` predicate matches one
    //     specific process instance and is dead the moment that process
    //     restarts. Surfacing it in the picker invites users to author
    //     rules that silently stop working.
    //   * WindowRole is the X11 WM_WINDOW_ROLE property, empty for every
    //     Wayland-native window — PlasmaZones is Wayland-only (per
    //     CLAUDE.md), so the picker would always read as blank.
    // The Field enum keeps both values for back-compat with already-saved
    // rules; only the authoring UI hides them.
    static const QList<Field> kFields = {
        Field::AppId,       Field::WindowClass, Field::DesktopFile,    Field::Title,
        Field::WindowType,  Field::IsSticky,    Field::IsFullscreen,   Field::IsMinimized,
        Field::IsMaximized, Field::ScreenId,    Field::VirtualDesktop, Field::Activity,
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
        } else if (f == Field::ScreenId) {
            // QML editor swaps this for a screen-picker ComboBox driven by
            // `settingsController.screens`, so the user sees "LG Ultra HD" not
            // "LG Electronics:LG Ultra HD:115107/vs:0".
            kind = QStringLiteral("screen");
        } else if (f == Field::Activity) {
            // QML editor swaps this for an activity-picker ComboBox driven by
            // `settingsController.activities`, so the user sees the activity
            // name not its UUID.
            kind = QStringLiteral("activity");
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
    // SetOpacity is registered as an action type (and validated) but no
    // consumer ever reads its slot — KWin per-window opacity is achievable
    // via `EffectWindow::setOpacity`, but the plumbing was never wired up.
    // Exposing it here would let users create rules that silently never
    // fire. Reinstate when the effect-side handler lands.
    static const QList<QLatin1StringView> kTypes = {
        ActionType::SetEngineMode,
        ActionType::SetSnappingLayout,
        ActionType::SetTilingAlgorithm,
        ActionType::DisableEngine,
        ActionType::Exclude,
        ActionType::Float,
        ActionType::OverrideAnimationShader,
        ActionType::OverrideAnimationCurve,
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
