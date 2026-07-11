// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/RuleAction.h>

#include <QJsonArray>
#include <QJsonValue>

#include "rulelogging.h"

#include <algorithm>

namespace PhosphorRules {

namespace {

constexpr QLatin1StringView kKeyType{"type"};
// Action params are stored inline alongside `type` in the action object; the
// loader strips `type` and treats the remainder as `params`.

/// A descriptor whose params payload carries no constraint — accepts any
/// object. Used for actions whose params are free-form or future-extensible.
bool acceptAny(const QJsonObject&)
{
    return true;
}

/// Validates that @p params has a non-empty string at @p key.
bool hasNonEmptyString(const QJsonObject& params, QLatin1StringView key)
{
    const QJsonValue v = params.value(key);
    return v.isString() && !v.toString().isEmpty();
}

/// Validates that @p params has a JSON bool at @p key.
bool hasBool(const QJsonObject& params, QLatin1StringView key)
{
    return params.value(key).isBool();
}

/// Validates that @p params has a number in [0, @p maxValue] at @p key.
/// Consumers truncate to int; the upper bound keeps a hand-edited payload from
/// carrying an absurd value into the geometry/border path.
bool hasNumberInRange(const QJsonObject& params, QLatin1StringView key, double maxValue)
{
    const QJsonValue v = params.value(key);
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    return d >= 0.0 && d <= maxValue;
}

/// Validates that @p params has a `#`-prefixed hex colour string at @p key.
/// Accepts the standard QColor hex shapes the effect-side consumer parses via
/// `QColor(QString)`: `#RGB` (4), `#RRGGBB` (7) and `#AARRGGBB` (9 — QColor reads
/// a 9-digit hex as alpha-first). The picker emits `#AARRGGBB`; the wider set
/// also keeps hand-edited short-form payloads from being silently dropped on
/// load while still rejecting non-hex/garbage. Named colours ("red") are NOT
/// accepted here — the boundary stays hex-only even though the consumer's
/// QColor would resolve them.
bool hasHexColor(const QJsonObject& params, QLatin1StringView key)
{
    const QJsonValue v = params.value(key);
    if (!v.isString()) {
        return false;
    }
    const QString s = v.toString();
    if ((s.size() != 4 && s.size() != 7 && s.size() != 9) || s.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

/// A border colour param value: a hex shape `hasHexColor` admits, OR the
/// `BorderColorToken::Accent` sentinel ("track the live system accent").
bool hasHexColorOrAccent(const QJsonObject& params, QLatin1StringView key)
{
    if (params.value(key).toString() == BorderColorToken::Accent) {
        return true;
    }
    return hasHexColor(params, key);
}

// Upper validation bounds (display units). The effect/daemon clamp to their
// own ConfigDefaults ranges on consumption; these only reject grossly
// malformed hand-edited payloads. Kept generous so values a user could pick
// through the global UI are never dropped on load.
// Border width/radius upper bounds live in RuleAction.h (MaxBorderWidth /
// MaxBorderRadius) so the KWin-effect consumer re-validation shares them.
constexpr double kMaxGap = 500.0;
// Zone-overlay border dimensions have their own bounds mirroring the global
// `Snapping.Zones.Border` config ranges (width 0-10, radius 0-50) — the overlay
// radius goes wider than the per-window `MaxBorderRadius` (20).
constexpr double kMaxOverlayBorderRadius = 50.0;
// Autotile parameter bounds (display units), mirroring the AutotileDefaults
// clamps the engine applies on consumption. These only reject grossly malformed
// hand-edited payloads.
constexpr double kMaxTiledWindows = 12.0;
constexpr double kMaxMasterCount = 5.0;
// Split-ratio bounds. The percent-editor display range is the exact primary pair
// ([10, 90] %); the wire ratio is derived (÷ 100) so the two never drift and the
// display bounds stay exact (0.1 * 100.0 is not exactly 10.0 in IEEE-754).
constexpr double kMinSplitPercent = 10.0;
constexpr double kMaxSplitPercent = 90.0;
constexpr double kMinSplitRatio = kMinSplitPercent / 100.0;
constexpr double kMaxSplitRatio = kMaxSplitPercent / 100.0;

} // namespace

// ── RuleAction (de)serialization ────────────────────────────────────────

QJsonObject RuleAction::toJson() const
{
    // `params` are written inline alongside `type`, so `"type"` is a RESERVED
    // param key: a `params` entry keyed `"type"` is overwritten by `insert`
    // here (and `fromJson` strips it back out on load). A free-form
    // `acceptAny` action must never carry a user `"type"` param — it would be
    // silently clobbered. The strict-key check rejects it for any descriptor
    // with a non-empty `allowedKeys`, but free-form descriptors opt out.
    if (params.contains(kKeyType)) {
        // A free-form (`acceptAny`) action whose params carry a `"type"` key
        // would have it silently clobbered by the insert below. Log at debug
        // level — the clobber is documented in the RuleAction.h header
        // comments on `params` and would otherwise re-emit on every save of
        // every store reload. `qCWarning` here turned routine persistence
        // cycles into recurring log noise even for correctly-loaded rules.
        qCDebug(lcRule) << "RuleAction::toJson: params carry a reserved `type` key — it will be clobbered by "
                           "the action type. action type:"
                        << type;
    }
    QJsonObject o = params;
    o.insert(kKeyType, type);
    return o;
}

std::optional<RuleAction> RuleAction::fromJson(const QJsonObject& obj)
{
    const QString type = obj.value(kKeyType).toString();
    if (type.isEmpty()) {
        qCWarning(lcRule) << "Action object has no `type` — dropping action.";
        return std::nullopt;
    }
    if (!ActionRegistry::instance().isRegistered(type)) {
        qCWarning(lcRule) << "Action type is not registered — dropping action. type:" << type;
        return std::nullopt;
    }
    RuleAction action;
    action.type = type;
    action.params = obj;
    action.params.remove(kKeyType);

    // Strict-key discipline — reject an action carrying a param key the
    // descriptor does not declare. An unknown key is almost always a typo or
    // a stale-schema payload; silently retaining it would let a misspelled
    // key sit inertly in the rule store forever. A descriptor with an empty
    // `allowedKeys` set opts out (free-form params).
    const auto descriptor = ActionRegistry::instance().descriptor(type);
    if (descriptor && !descriptor->allowedKeys.isEmpty()) {
        // Iterate via const-iterator rather than `keys()` to avoid the
        // per-call QStringList allocation `QJsonObject::keys()` does.
        // The per-key `allowedKeys.contains(...)` lookup is O(M) over a
        // QStringList — acceptable at the built-in scale (M ≤ 3 allowed
        // keys × K ≤ 3 params = 9 string compares per action load) and
        // faster in practice than the QSet alternative at these sizes.
        // If a future descriptor grows allowedKeys beyond a handful,
        // switch the field type to QSet<QString> in ActionDescriptor.
        for (auto it = action.params.constBegin(); it != action.params.constEnd(); ++it) {
            if (!descriptor->allowedKeys.contains(it.key())) {
                qCWarning(lcRule) << "Action params carry an unexpected key — dropping action. type:" << type
                                  << "key:" << it.key();
                return std::nullopt;
            }
        }
    }

    if (!ActionRegistry::instance().validate(action)) {
        qCWarning(lcRule) << "Action params failed descriptor validation — dropping action. type:" << type;
        return std::nullopt;
    }
    return action;
}

// ── ActionRegistry ──────────────────────────────────────────────────────

ActionRegistry& ActionRegistry::instance()
{
    static ActionRegistry registry;
    return registry;
}

ActionRegistry::ActionRegistry()
{
    registerBuiltins();
}

void ActionRegistry::registerAction(const ActionDescriptor& descriptor)
{
    m_descriptors.insert(descriptor.type, descriptor);
}

bool ActionRegistry::unregisterAction(const QString& type)
{
    return m_descriptors.remove(type) > 0;
}

bool ActionRegistry::isRegistered(const QString& type) const
{
    return m_descriptors.contains(type);
}

std::optional<ActionDescriptor> ActionRegistry::descriptor(const QString& type) const
{
    const auto it = m_descriptors.constFind(type);
    if (it == m_descriptors.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

QString ActionRegistry::slotFor(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd() || !it->slotFor) {
        return QString();
    }
    return it->slotFor(action.params);
}

bool ActionRegistry::isTerminal(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    return it != m_descriptors.constEnd() && it->terminal;
}

bool ActionRegistry::validate(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd()) {
        return false;
    }
    if (it->validate && !it->validate(action.params)) {
        return false;
    }
    // A descriptor that resolves to no slot (and is not terminal) cannot
    // contribute to a resolution — treat it as invalid.
    if (!it->terminal && (!it->slotFor || it->slotFor(action.params).isEmpty())) {
        return false;
    }
    return true;
}

ActionDomain ActionRegistry::domainFor(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd()) {
        return ActionDomain::Window;
    }
    return it->domain;
}

QStringList ActionRegistry::registeredTypes() const
{
    return m_descriptors.keys();
}

bool ActionRegistry::hasTag(const QString& type, QLatin1StringView tag) const
{
    const auto it = m_descriptors.constFind(type);
    if (it == m_descriptors.constEnd()) {
        return false;
    }
    // Compare against QLatin1StringView directly (QString::operator== has a
    // non-allocating overload) rather than materialising a throwaway QString.
    return std::any_of(it->tags.cbegin(), it->tags.cend(), [tag](const QString& t) {
        return t == tag;
    });
}

QStringList ActionRegistry::typesWithTag(QLatin1StringView tag) const
{
    QStringList result;
    const QString tagStr(tag);
    for (auto it = m_descriptors.constBegin(); it != m_descriptors.constEnd(); ++it) {
        if (it->tags.contains(tagStr)) {
            result.append(it.key());
        }
    }
    return result;
}

namespace {

/// Helper to keep the registerBuiltins body legible — every built-in shares
/// the same constant slot pattern (no slot-from-params resolution).
ActionDescriptor::SlotResolver constantSlot(QLatin1StringView slot)
{
    return [s = QString(slot)](const QJsonObject&) {
        return s;
    };
}

/// The engine-token wire strings DisableEngine / SetEngineMode pickers
/// expose. Keeping them together makes the "both pickers share the engine
/// enum" invariant visible at the descriptor level. The order mirrors
/// `PhosphorZones::allModes()` so the editor's enum dropdown lists modes
/// in the same order across surfaces.
///
/// Returns a const reference into a function-local static. The DisableEngine
/// validator runs on every action-load (rule store load, every rule edit),
/// so the previous by-value form rebuilt the 3-element list on every call;
/// the static keeps the descriptor's enum-vocabulary stable across the
/// process and the validator's `contains` cheap.
const QStringList& engineModeOptions()
{
    // NOTE: this is the engine-mode ACTION vocabulary (SetEngineMode param) and
    // is DELIBERATELY distinct from the Mode MATCH-field vocabulary in
    // MatchTypes.h, which uses "snapping" / "tiling" (no "autotile"). The action
    // names the engine ("autotile"); the match field names the placement mode a
    // window is in ("tiling"). Do not unify them — a Mode match rule authored
    // with "autotile" would silently never match.
    static const QStringList s_options{
        QStringLiteral("snapping"),
        QStringLiteral("autotile"),
        QStringLiteral("scrolling"),
    };
    return s_options;
}

} // namespace

void ActionRegistry::registerBuiltins()
{
    using P = ParamSchema;

    // ── engine-mode slot ──
    // SetEngineMode intentionally validates only that the `mode` param is
    // non-empty — vocabulary validation lives at consumers, NOT at load.
    // Two reasons: (a) the rule-evaluator test suites use arbitrary mode
    // strings as per-rule identifiers (`test_ruleevaluator.cpp` uses
    // "alpha"/"beta"/"gamma"; `test_ruleevaluator_cascade.cpp` uses
    // "window-rule"), so tightening to a closed set would force those
    // tests to re-route through a different action type; (b) the open
    // vocabulary mirrors the bridge's `disableRuleMode` contract — the
    // wire token round-trips verbatim, and the assignment-side consumer
    // `PhosphorZones::RuleHelpers::entryFromRuleMatchActions` maps it
    // via `PhosphorZones::modeFromWireString`, which returns nullopt
    // for unknown tokens and leaves the entry on its Snapping default.
    // DisableEngine is the asymmetric case below: it gates on the
    // closed set to fail malformed disable rules early at load
    // (per-mode disable lists never use synthetic identifiers).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetEngineMode),
        .slotFor = constantSlot(ActionSlot::EngineMode),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Mode);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Mode)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Mode),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = engineModeOptions()}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 0,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── layout slot — both layout-shaping actions share it ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetSnappingLayout),
        .slotFor = constantSlot(ActionSlot::Layout),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::LayoutId);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::LayoutId)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::LayoutId), .kind = QStringLiteral("snappingLayout")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 1,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTilingAlgorithm),
        .slotFor = constantSlot(ActionSlot::Layout),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Algorithm);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Algorithm)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Algorithm), .kind = QStringLiteral("tilingAlgorithm")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 2,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── engine-enable slot ──
    // `mode` records which engine the rule disables. The recognised tokens
    // are the wire vocabulary `PhosphorZones::modeFromWireString` accepts —
    // listed verbatim here because the LGPL PhosphorRules lib does not
    // depend on PhosphorZones, and depending on it just for the string
    // vocabulary would couple the two libs over a stable wire format. New
    // tokens added here MUST mirror the Mode enum extension in
    // libs/phosphor-zones/include/PhosphorZones/AssignmentEntry.h; the
    // round-trip tests pin the contract.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DisableEngine),
        .slotFor = constantSlot(ActionSlot::EngineEnable),
        .validate =
            [](const QJsonObject& p) {
                return engineModeOptions().contains(p.value(ActionParam::Mode).toString());
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Mode)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Mode),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = engineModeOptions()}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 3,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── locked slot — context-domain layout lock ──
    // A matched context rule pins the active layout for its screen/desktop/
    // activity so it can't be switched, mirroring the manual ToggleLayoutLock
    // shortcut. Boolean `value`: true locks (the meaningful default for a
    // freshly-authored action, hence `defaultDisplay = 1.0`); false fills the
    // Locked slot with not-locked — a no-op against the manual lock store (the
    // daemon ORs, so it never unlocks a manual lock), but as a single-winner
    // slot a higher-priority false rule overrides a lower-priority true one.
    // Mode-agnostic — the rule query ignores the Mode
    // axis, so the same lock surfaces for whichever engine mode is asked — and
    // live-resolved: the daemon ORs it with (never replaces) the manual
    // ToggleLayoutLock store, so rule locks and manual toggles do not fight.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::LockContext),
        .slotFor = constantSlot(ActionSlot::Locked),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 4,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── default-assignment slot — context-domain override of the global
    //    "suppress default layout assignment" setting. A matched context rule
    //    flips the synthesized level-1 default for its screen/desktop/activity:
    //    value == false suppresses it (no engine activates until the user
    //    explicitly assigns one), value == true forces it through even when the
    //    global suppress setting is on. Mode-agnostic (the level-1 default is a
    //    single mode-carrying AssignmentEntry) and live-resolved per-slot at
    //    cascade-miss by LayoutRegistry::resolveContextDefaultAssignment —
    //    carries no SetEngineMode action, so it never wins the single-rule
    //    assignment cascade. Seeds FALSE: the global setting defaults OFF (every
    //    context gets a default), so the only meaningful per-context rule a user
    //    adds is "suppress on this monitor" — defaultDisplay 0.0 lands the fresh
    //    action on that value.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DefaultLayoutAssignment),
        .slotFor = constantSlot(ActionSlot::DefaultAssignment),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 5,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── manage slot — terminal. Exclude is intentionally free-form: an empty
    //    `allowedKeys` opts out of the strict-key check so a future Exclude
    //    reason/scope param can be added without a schema bump. ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::Exclude),
        .slotFor = constantSlot(ActionSlot::Manage),
        .validate = &acceptAny,
        .terminal = true,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 0,
    });

    // ── float slot — intentionally free-form (future float-geometry hints);
    //    empty `allowedKeys` opts out of the strict-key check. ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::Float),
        .slotFor = constantSlot(ActionSlot::Float),
        .validate = &acceptAny,
        .terminal = false,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 1,
    });

    registerAction(ActionDescriptor{
        .type = QString(ActionType::SnapToZone),
        .slotFor = constantSlot(ActionSlot::Placement),
        .validate =
            [](const QJsonObject& p) {
                // A non-empty array of positive integer (1-based) zone ordinals.
                const QJsonValue v = p.value(ActionParam::Zones);
                if (!v.isArray()) {
                    return false;
                }
                const QJsonArray arr = v.toArray();
                if (arr.isEmpty()) {
                    return false;
                }
                for (const QJsonValue& e : arr) {
                    if (!e.isDouble()) {
                        return false;
                    }
                    const double d = e.toDouble();
                    // Bound on the DOUBLE before narrowing — a float-to-int cast
                    // out of int's range is undefined behaviour. Reject < 1
                    // (ordinals are 1-based) and an absurd upper value first; only
                    // then is the cast for the integrality check well-defined.
                    if (d < 1.0 || d > MaxZoneOrdinal) {
                        return false;
                    }
                    if (static_cast<double>(static_cast<int>(d)) != d) {
                        return false; // non-integral
                    }
                }
                return true;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Zones)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Zones), .kind = QStringLiteral("zoneOrdinals")}},
        // No tags: SnapToZone is daemon-placement only (consumed by the SnapEngine
        // open path), not an Effect / Border / Animation / Overlay action.
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 2,
    });

    // ── open-routing: send a matched window to a target monitor / desktop ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RouteToScreen),
        .slotFor = constantSlot(ActionSlot::RouteScreen),
        .validate =
            [](const QJsonObject& p) {
                // A non-empty canonical screen id (physical or virtual). The id
                // form is not validated against live screen state here — this lib
                // has no view of connected outputs, and a rule targeting a
                // currently-absent monitor is legitimate (it fires when that
                // monitor returns). The daemon's placement path no-ops a route
                // whose target screen is not currently resolvable.
                return hasNonEmptyString(p, ActionParam::TargetScreenId);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::TargetScreenId)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::TargetScreenId), .kind = QStringLiteral("screenId")}},
        // No tags: RouteToScreen is daemon open-path routing only, not an
        // Effect / Border / Animation / Overlay / Gap action.
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 3,
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RouteToDesktop),
        .slotFor = constantSlot(ActionSlot::RouteDesktop),
        .validate =
            [](const QJsonObject& p) {
                // A single 1-based virtual-desktop number. Bound the DOUBLE before
                // narrowing (a float-to-int cast out of int's range is UB), then
                // require integrality — mirrors the SnapToZone ordinal check.
                const QJsonValue v = p.value(ActionParam::TargetDesktop);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > MaxVirtualDesktopOrdinal) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::TargetDesktop)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::TargetDesktop),
                     .kind = QStringLiteral("virtualDesktop"),
                     .min = 1.0,
                     .max = static_cast<double>(MaxVirtualDesktopOrdinal)}},
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 4,
    });

    // ── animation slots — event-scoped: "anim-shader:<event>" ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationShader),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimShaderPrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::EffectId), QString(ActionParam::Params)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
                   P{.key = QString(ActionParam::EffectId), .kind = QStringLiteral("shaderEffect")}},
        .category = QStringLiteral("animation"),
        .displayOrder = 0,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationTiming),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimTimingPrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::Curve), QString(ActionParam::DurationMs)},
        .domain = ActionDomain::Window,
        .params =
            {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
             P{.key = QString(ActionParam::DurationMs), .kind = QStringLiteral("number"), .min = 0.0, .max = 60000.0}},
        .category = QStringLiteral("animation"),
        .displayOrder = 1,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationCurve),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimCurvePrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::Curve)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
                   P{.key = QString(ActionParam::Curve), .kind = QStringLiteral("curveEditor")}},
        .category = QStringLiteral("animation"),
        .displayOrder = 2,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });

    // ── anim-exclude slot — terminal within the animation pipeline ──
    // A rule with `ExcludeAnimations` suppresses every animation override
    // for matched windows, regardless of event. Single event-agnostic
    // slot (vs. the per-event AnimShader/AnimTiming/AnimCurve slots) so
    // ONE rule covers the window's full animation surface. Terminal so
    // an ExcludeAnimations action on the same rule as an
    // OverrideAnimation* action wins. Migrated from the legacy
    // animationExcludedApplications / animationExcludedWindowClasses
    // lists by the v3→v4 chain; user-authored via Rules going
    // forward.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::ExcludeAnimations),
        .slotFor = constantSlot(ActionSlot::AnimExclude),
        .validate = &acceptAny,
        .terminal = true,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("animation"),
        .displayOrder = 3,
        .tags = {QString(Tag::Animation)},
    });

    // ── opacity slot ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOpacity),
        .slotFor = constantSlot(ActionSlot::Opacity),
        .validate =
            [](const QJsonObject& p) {
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                return d >= 0.0 && d <= 1.0;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = 0.0,
                     .max = 100.0,
                     .scale = 0.01,
                     .defaultDisplay = 100.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = -1,
        .tags = {QString(Tag::Effect)},
    });

    // ── overlay-property slots — context-domain overrides of the active
    //    layout's zone-overlay shader / style. Daemon-side only
    //    (LayoutRegistry::resolveContextOverlay → OverlayService); no Tag::Effect.
    //    Shader-id vocabulary validation lives at the consumer (the overlay
    //    service falls back to the layout default for an unknown id), mirroring
    //    SetEngineMode's open-vocabulary rationale above.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideOverlayShader),
        .slotFor = constantSlot(ActionSlot::OverlayShader),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::EffectId);
            },
        .terminal = false,
        // Params carries the optional shader-uniform overrides, mirroring
        // OverrideAnimationShader; the inline ParameterEditor writes it.
        .allowedKeys = {QString(ActionParam::EffectId), QString(ActionParam::Params)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::EffectId), .kind = QStringLiteral("overlayShader")}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 0,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideOverlayStyle),
        .slotFor = constantSlot(ActionSlot::OverlayStyle),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == OverlayStyleToken::Rectangles || v == OverlayStyleToken::Preview;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(OverlayStyleToken::Rectangles), QString(OverlayStyleToken::Preview)}}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 1,
        .tags = {QString(Tag::Overlay)},
    });

    // ── per-context overlay-APPEARANCE slots (domain Context) ──
    // Colours / opacities / border dimensions / zone-number visibility that
    // override the global Snapping.Zones.* config for a matched context. One
    // slot per property so independent rules cascade. Resolved daemon-side by
    // LayoutRegistry::resolveContextOverlay; an unset property falls through to
    // the global config value (config stays authoritative). No Tag::Effect —
    // these are overlay-service reads, not KWin-effect window appearance.
    //
    // The three colour actions share a shape, so a small table keeps type and
    // slot in lockstep per row (mirroring the per-side gap loop). Colours use
    // the plain hex validator — NO accent sentinel, since the overlay consumer
    // resolves no token.
    struct OverlayColor
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const OverlayColor& c : {
             OverlayColor{ActionType::SetOverlayHighlightColor, ActionSlot::OverlayHighlightColor, 2},
             OverlayColor{ActionType::SetOverlayInactiveColor, ActionSlot::OverlayInactiveColor, 3},
             OverlayColor{ActionType::SetOverlayBorderColor, ActionSlot::OverlayBorderColor, 4},
         }) {
        const QString slot = QString(c.slot);
        registerAction(ActionDescriptor{
            .type = QString(c.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasHexColor(p, ActionParam::Value);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
            .category = QStringLiteral("overlay"),
            .displayOrder = c.order,
            .tags = {QString(Tag::Overlay)},
        });
    }
    // Active / inactive overlay opacity — [0, 1] double on the wire, edited as a
    // percent (scale 0.01), mirroring SetOpacity. Defaults match the global
    // config (activeOpacity 0.5, inactiveOpacity 0.3).
    struct OverlayOpacity
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        double defaultDisplay;
        int order;
    };
    for (const OverlayOpacity& o : {
             OverlayOpacity{ActionType::SetOverlayActiveOpacity, ActionSlot::OverlayActiveOpacity, 50.0, 5},
             OverlayOpacity{ActionType::SetOverlayInactiveOpacity, ActionSlot::OverlayInactiveOpacity, 30.0, 6},
         }) {
        const QString slot = QString(o.slot);
        registerAction(ActionDescriptor{
            .type = QString(o.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    const QJsonValue v = p.value(ActionParam::Value);
                    if (!v.isDouble()) {
                        return false;
                    }
                    const double d = v.toDouble();
                    return d >= 0.0 && d <= 1.0;
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value),
                         .kind = QStringLiteral("percent"),
                         .min = 0.0,
                         .max = 100.0,
                         .scale = 0.01,
                         .defaultDisplay = o.defaultDisplay}},
            .category = QStringLiteral("overlay"),
            .displayOrder = o.order,
            .tags = {QString(Tag::Overlay)},
        });
    }
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayBorderWidth),
        .slotFor = constantSlot(ActionSlot::OverlayBorderWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderWidth,
                     .defaultDisplay = 2.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 7,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayBorderRadius),
        .slotFor = constantSlot(ActionSlot::OverlayBorderRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxOverlayBorderRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxOverlayBorderRadius,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 8,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayShowZoneNumbers),
        .slotFor = constantSlot(ActionSlot::OverlayShowZoneNumbers),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 9,
        .tags = {QString(Tag::Overlay)},
    });

    // RestorePosition is window-domain but NOT a border/appearance slot — it is
    // consumed daemon-side (both engines' restore-position predicate), not by the
    // effect. Unlike the border bools below it seeds FALSE: the per-engine
    // `*RestoreFloatedWindowsOnLogin` settings default ON, so a fresh rule that
    // re-asserted true would be a no-op the user has to flip. Seeding false lands
    // the rule on its only meaningful value — opt this window OUT of restore.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RestorePosition),
        .slotFor = constantSlot(ActionSlot::RestorePosition),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 5,
    });

    // Two more per-window restore-policy overrides, same shape as RestorePosition
    // (bool, seeds FALSE because both governing settings default ON — the only
    // meaningful fresh-rule value is "opt this window OUT"). Consumed daemon-side
    // (the managed-restore predicate / the drag-out unsnap paths), not the effect.
    struct RestorePolicy
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const RestorePolicy& rp : {
             RestorePolicy{ActionType::SetRestoreToZoneOnLogin, ActionSlot::RestoreToZoneOnLogin, 6},
             RestorePolicy{ActionType::SetRestoreSizeOnUnsnap, ActionSlot::RestoreSizeOnUnsnap, 7},
         }) {
        const QString slot = QString(rp.slot);
        registerAction(ActionDescriptor{
            .type = QString(rp.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasBool(p, ActionParam::Value);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Window,
            .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
            .category = QStringLiteral("windowManagement"),
            .displayOrder = rp.order,
        });
    }

    // ── per-window border / title-bar appearance slots (domain Window) ──
    // One slot per property so independent rules cascade per-property. The
    // effect (resolveWindowAppearance) reads these slots and merges them over
    // the global snap/autotile border state for ANY matched window. Bool seeds
    // (defaultDisplay 1.0 = true) land a fresh "hide title bars" / "show
    // border" rule in its on state — the user adds the action to turn it on.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetHideTitleBar),
        .slotFor = constantSlot(ActionSlot::HideTitleBar),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 0,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderVisible),
        .slotFor = constantSlot(ActionSlot::BorderVisible),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 1,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderWidth),
        .slotFor = constantSlot(ActionSlot::BorderWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderWidth,
                     .defaultDisplay = 2.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 2,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderRadius),
        .slotFor = constantSlot(ActionSlot::BorderRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderRadius,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 3,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    // Two single-colour border actions, one per focus state, each its own slot.
    // The colour param is keyed ActionParam::Value: a hex shape or the accent
    // sentinel. Internal active/inactive naming matches KWin and the effect's
    // activeColor/inactiveColor; user-facing labels say focused/unfocused.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderColorActive),
        .slotFor = constantSlot(ActionSlot::BorderColorActive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 4,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderColorInactive),
        .slotFor = constantSlot(ActionSlot::BorderColorInactive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 5,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    // Per-window opacity+tint layer slots, feeding the plain layer's reserved
    // "opacity-tint" pack the way the border slots feed "border". Visible
    // mirrors SetBorderVisible (an engaged true turns the layer on for the
    // matched window even when the global toggle is off; false forces it
    // off). Strength is wire-encoded [0.0, 1.0] like SetOpacity (percent in
    // the editor); the colour accepts a hex shape or the accent sentinel. The
    // layer's opacity itself stays on the SetOpacity slot above — when the
    // layer renders, that rule's value folds into the pack's opacity param
    // (rule wins over config); in custom mode only a handlesOpacity pack
    // consumes it, and a chain without one does not honour it at all.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOpacityTintVisible),
        .slotFor = constantSlot(ActionSlot::OpacityTintVisible),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 0,
        .tags = {QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTintStrength),
        .slotFor = constantSlot(ActionSlot::TintStrength),
        .validate =
            [](const QJsonObject& p) {
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                return d >= 0.0 && d <= 1.0;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = 0.0,
                     .max = 100.0,
                     .scale = 0.01,
                     .defaultDisplay = 30.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 1,
        .tags = {QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTintColor),
        .slotFor = constantSlot(ActionSlot::TintColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 2,
        .tags = {QString(Tag::Effect)},
    });
    // Decoration-chain override: an ordered surface-pack list (empty array =
    // "no decoration" sentinel, so `Chain` must be PRESENT and an array but
    // may be empty) plus an optional per-pack params object riding the shared
    // `Params` key out-of-band, exactly like OverrideAnimationShader's
    // uniform map (the editor writes it; it is not in the params schema).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideDecorationChain),
        .slotFor = constantSlot(ActionSlot::DecorationChain),
        .validate =
            [](const QJsonObject& p) {
                return p.contains(ActionParam::Chain) && p.value(ActionParam::Chain).isArray();
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Chain), QString(ActionParam::Params)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Chain), .kind = QStringLiteral("decorationChain")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 6,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });

    // ── per-context gap slots (domain Context) ──
    // Resolved daemon-side at zone-geometry time (DaemonGeometryResolver) as
    // the highest-precedence gap layer. Per-property to mirror the
    // PerScreenSnappingKey set; the resolver maps these slots into a
    // per-screen-shaped override map and reuses the existing per-side logic.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetInnerGap),
        .slotFor = constantSlot(ActionSlot::InnerGap),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxGap,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 0,
        .tags = {QString(Tag::Gap)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOuterGap),
        .slotFor = constantSlot(ActionSlot::OuterGap),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxGap,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 1,
        .tags = {QString(Tag::Gap)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetUsePerSideOuterGap),
        .slotFor = constantSlot(ActionSlot::UsePerSideOuterGap),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 2,
        .tags = {QString(Tag::Gap)},
    });
    // Each per-side gap maps to its own slot. The {type, slot} pairs live in a
    // single table so type and slot stay in lockstep per row — adding a side is
    // one row, with no parallel mapping to keep in sync and no silent
    // fall-through to the wrong slot if a row is mistyped.
    struct PerSideGap
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const PerSideGap& perSide : {
             PerSideGap{ActionType::SetOuterGapTop, ActionSlot::OuterGapTop, 3},
             PerSideGap{ActionType::SetOuterGapBottom, ActionSlot::OuterGapBottom, 4},
             PerSideGap{ActionType::SetOuterGapLeft, ActionSlot::OuterGapLeft, 5},
             PerSideGap{ActionType::SetOuterGapRight, ActionSlot::OuterGapRight, 6},
         }) {
        const QString slot = QString(perSide.slot);
        registerAction(ActionDescriptor{
            .type = QString(perSide.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasNumberInRange(p, ActionParam::Value, kMaxGap);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value),
                         .kind = QStringLiteral("number"),
                         .min = 0.0,
                         .max = kMaxGap,
                         .defaultDisplay = 8.0}},
            .category = QStringLiteral("gap"),
            .displayOrder = perSide.order,
            .tags = {QString(Tag::Gap)},
        });
    }

    // ── per-context autotile parameter slots (domain Context) ──
    // Resolved daemon-side by LayoutRegistry::resolveContextTilingParams and
    // layered onto the per-screen autotile override map (config stays the base;
    // the rule wins where present). Category "layoutEngine" — these configure the
    // tiling engine for the matched context.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetMaxWindows),
        .slotFor = constantSlot(ActionSlot::MaxWindows),
        .validate =
            [](const QJsonObject& p) {
                // 1..12, integral; the consumer re-clamps to AutotileDefaults, so
                // this only enforces a sanity floor of 1 (0 and negatives are rejected
                // as grossly malformed) and the upper bound. Reject non-integral like
                // the sibling count validators (SnapToZone / RouteToDesktop) rather
                // than silently truncating a hand-edited fractional count.
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > kMaxTiledWindows) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 1.0,
                     .max = kMaxTiledWindows,
                     .defaultDisplay = 5.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 10,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetSplitRatio),
        .slotFor = constantSlot(ActionSlot::SplitRatio),
        .validate =
            [](const QJsonObject& p) {
                // Wire is the [kMinSplitRatio, kMaxSplitRatio] ratio; edited as a percent.
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                return d >= kMinSplitRatio && d <= kMaxSplitRatio;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinSplitPercent,
                     .max = kMaxSplitPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 11,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetMasterCount),
        .slotFor = constantSlot(ActionSlot::MasterCount),
        .validate =
            [](const QJsonObject& p) {
                // 1..kMaxMasterCount, integral (mirrors SetMaxWindows / SnapToZone).
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > kMaxMasterCount) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 1.0,
                     .max = kMaxMasterCount,
                     .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 12,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetInsertPosition),
        .slotFor = constantSlot(ActionSlot::InsertPosition),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == InsertPositionToken::End || v == InsertPositionToken::AfterFocused
                    || v == InsertPositionToken::AsMaster;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(InsertPositionToken::End), QString(InsertPositionToken::AfterFocused),
                                        QString(InsertPositionToken::AsMaster)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 13,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverflowBehavior),
        .slotFor = constantSlot(ActionSlot::OverflowBehavior),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == OverflowBehaviorToken::Float || v == OverflowBehaviorToken::Unlimited;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{
            .key = QString(ActionParam::Value),
            .kind = QStringLiteral("enum"),
            .enumWireValues = {QString(OverflowBehaviorToken::Float), QString(OverflowBehaviorToken::Unlimited)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 14,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDragBehavior),
        .slotFor = constantSlot(ActionSlot::DragBehavior),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == DragBehaviorToken::Float || v == DragBehaviorToken::Reorder;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(DragBehaviorToken::Float), QString(DragBehaviorToken::Reorder)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 15,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // SetAlgorithmParam mirrors OverrideOverlayShader: a picker param (the target
    // algorithm) plus a free-form `params` blob (the custom-parameter values,
    // validated against the algorithm's declared schema at apply time via
    // hasCustomParam — so the wire validator only requires the algorithm token).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetAlgorithmParam),
        .slotFor = constantSlot(ActionSlot::AlgorithmParams),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Algorithm);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Algorithm), QString(ActionParam::Params)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Algorithm), .kind = QStringLiteral("tilingAlgorithm")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 16,
        .tags = {QString(Tag::LayoutEngine)},
    });
}

} // namespace PhosphorRules
