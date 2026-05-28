// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleAction.h>

#include <QJsonValue>

#include "windowrulelogging.h"

namespace PhosphorWindowRule {

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
        qCDebug(lcWindowRule) << "RuleAction::toJson: params carry a reserved `type` key — it will be clobbered by "
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
        qCWarning(lcWindowRule) << "Action object has no `type` — dropping action.";
        return std::nullopt;
    }
    if (!ActionRegistry::instance().isRegistered(type)) {
        qCWarning(lcWindowRule) << "Action type is not registered — dropping action. type:" << type;
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
        // Iterate via const-iterator rather than `keys()` — `QJsonObject::keys()`
        // allocates a fresh QStringList of every key, which on the load hot
        // path (called once per action of every rule in the store) burns a
        // throwaway list per call. The iterator pair has zero overhead.
        for (auto it = action.params.constBegin(); it != action.params.constEnd(); ++it) {
            if (!descriptor->allowedKeys.contains(it.key())) {
                qCWarning(lcWindowRule) << "Action params carry an unexpected key — dropping action. type:" << type
                                        << "key:" << it.key();
                return std::nullopt;
            }
        }
    }

    if (!ActionRegistry::instance().validate(action)) {
        qCWarning(lcWindowRule) << "Action params failed descriptor validation — dropping action. type:" << type;
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
QStringList engineModeOptions()
{
    return {QStringLiteral("snapping"), QStringLiteral("autotile"), QStringLiteral("scrolling")};
}

} // namespace

void ActionRegistry::registerBuiltins()
{
    using P = ParamSchema;

    // ── engine-mode slot ──
    // SetEngineMode intentionally validates only that the `mode` param is
    // non-empty — vocabulary validation lives at consumers, NOT at load.
    // Two reasons: (a) the cascade-fidelity test suite uses arbitrary mode
    // strings as cascade-rule identifiers ("default", "screen", "exact",
    // etc.) so tightening to a closed set would force those tests to
    // re-route through a different action type; (b) the open vocabulary
    // mirrors the bridge's `disableRuleMode` contract — the wire token
    // round-trips verbatim and `PhosphorZones::modeFromWireString` is the
    // authoritative consumer-side check. DisableEngine is the asymmetric
    // case below: it gates on the closed set to fail malformed disable
    // rules early at load (per-mode disable lists never use synthetic
    // cascade-marker strings).
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
    });

    // ── engine-enable slot ──
    // `mode` records which engine the rule disables. The recognised tokens
    // are the wire vocabulary `PhosphorZones::modeFromWireString` accepts —
    // listed verbatim here because the LGPL PhosphorWindowRule lib does not
    // depend on PhosphorZones, and depending on it just for the string
    // vocabulary would couple the two libs over a stable wire format. New
    // tokens added here MUST mirror the Mode enum extension in
    // libs/phosphor-zones/include/PhosphorZones/AssignmentEntry.h; the
    // round-trip tests pin the contract.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DisableEngine),
        .slotFor = constantSlot(ActionSlot::EngineEnable),
        // Single source of truth — the picker enum and the validator both
        // read from `engineModeOptions()`. Adding a new token to that list
        // automatically updates both surfaces; the prior hand-rolled OR
        // chain would silently drift if someone updated only one side.
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
        // `params` is a free-form shader-uniform object — not authorable
        // through a flat key/kind descriptor, so it is intentionally omitted
        // from the structural schema; a shader-uniform editor would graduate
        // the rule to Advanced.
        .params = {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
                   P{.key = QString(ActionParam::EffectId), .kind = QStringLiteral("shaderEffect")}},
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
        // The descriptor still allows `curve` for back-compat with legacy
        // rules; the editor does not expose it on this action — curve lives
        // in `OverrideAnimationCurve` (separate slot) so the user can
        // override curve and duration independently per event.
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::Curve), QString(ActionParam::DurationMs)},
        .domain = ActionDomain::Window,
        .params =
            {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
             P{.key = QString(ActionParam::DurationMs), .kind = QStringLiteral("number"), .min = 0.0, .max = 60000.0}},
    });
    // Curve override — own slot so it can be combined with a Timing-slot
    // duration override on the same event without one shadowing the other.
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
    });

    // ── opacity slot ──
    // The wire value is a 0.0–1.0 fraction; the editor renders a 0–100
    // percentage, so the stored value is `display * scale`.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOpacity),
        .slotFor = constantSlot(ActionSlot::Opacity),
        .validate =
            [](const QJsonObject& p) {
                const QJsonValue v = p.value(ActionParam::Value);
                return v.isDouble() && v.toDouble() >= 0.0 && v.toDouble() <= 1.0;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = 0.0,
                     .max = 100.0,
                     .scale = 0.01,
                     // Seed at 100% so a fresh SetOpacity rule lands at "no
                     // visible change". `min = 0` is a valid wire value
                     // (fully transparent) but a seeded-zero rule would save
                     // immediately and dim the matched window to invisibility
                     // before the user adjusted the slider.
                     .defaultDisplay = 100.0}},
    });
}

} // namespace PhosphorWindowRule
