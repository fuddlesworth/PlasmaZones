// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Effect-local shims for the per-window animation cascade on top of
// PhosphorWindowRules::RuleEvaluator. They walk the event-scoped slots
// (`anim-shader:`, `anim-timing:`, `anim-curve:`) filled by WindowRules
// carrying OverrideAnimation* actions, with the duration clamp, the curve
// `tryCreate` fallback, the engaged-empty `effectId` sentinel, and the
// empty-input short-circuits localised here so the evaluator stays generic.
//
// SPDX is GPL-3 because this TU lives in the effect tree, but it deliberately
// touches no KWin type — it operates purely on the LGPL rule engine and
// animation library value types, so it could be promoted to an LGPL library
// in the future without source changes. Per-window context is threaded in
// via PhosphorWindowRules::WindowQuery built KWin-side by
// `windowRuleQueryFor()` (window_query.h).

#include "shader_resolve.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/RuleEvaluator.h>
#include <PhosphorWindowRules/WindowQuery.h>

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

namespace PlasmaZones {

namespace {

// OverrideAnimation* action param keys consumed by the resolvers below come
// from the shared PhosphorWindowRules::ActionParam vocabulary so the resolver,
// the action-registry validators in ruleaction.cpp, the rule-editor UI, and
// the v3→v4 migration in configmigration.cpp::buildAnimationAppRule all read
// from one source of truth. A future rename of any of these wire keys flows
// to every consumer in a single edit.
namespace ActionParam = PhosphorWindowRules::ActionParam;

/// The event-scoped shader slot id for @p eventPath. Concatenation through
/// the `QLatin1StringView + QString` overload allocates a single QString
/// directly rather than materialising the prefix into its own QString first.
QString shaderSlotFor(const QString& eventPath)
{
    return PhosphorWindowRules::ActionSlot::AnimShaderPrefix + eventPath;
}

/// The event-scoped timing slot id for @p eventPath.
QString timingSlotFor(const QString& eventPath)
{
    return PhosphorWindowRules::ActionSlot::AnimTimingPrefix + eventPath;
}

/// The event-scoped curve-override slot id for @p eventPath. Curve and timing
/// (duration) are independent overrides — a rule can fill one without the
/// other so the user can change just the easing or just the duration.
QString curveSlotFor(const QString& eventPath)
{
    return PhosphorWindowRules::ActionSlot::AnimCurvePrefix + eventPath;
}

} // namespace

ResolvedShaderAndDuration resolveAnimationShaderAndDuration(const PhosphorWindowRules::RuleEvaluator& evaluator,
                                                            const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                            const QString& windowId,
                                                            const PhosphorWindowRules::WindowQuery& query,
                                                            const QString& eventPath, int defaultDurationMs)
{
    // Empty-input short-circuit — preserves the standalone resolvers' header
    // contract for both a windowless query and an empty eventPath, and
    // avoids consuming a cache slot for an evaluator walk that cannot match
    // anything (no window attribute can satisfy any rule predicate).
    if (!query.hasWindow() || eventPath.isEmpty()) {
        return ResolvedShaderAndDuration{tree.resolve(eventPath), defaultDurationMs};
    }
    // ONE cached evaluator walk feeds both slot lookups. The historical
    // pair of standalone shader-profile + duration resolvers did two
    // independent `resolve()` walks per shader event — same query, same
    // priority-order traversal, both bypassing the per-window match
    // cache. `resolveCached` keyed by the composite windowId reuses the
    // result across both slot reads AND across subsequent shader events
    // for the same window until the rule set's revision changes.
    const PhosphorWindowRules::ResolvedActions resolved = evaluator.resolveCached(windowId, query);

    // Shader slot: rule wins verbatim (engaged-empty effectId is the
    // user's "block tree fallthrough for this app/event" sentinel and
    // is preserved by ShaderProfile's own contract). Unfilled slot →
    // fall through to the per-event tree.
    PhosphorAnimationShaders::ShaderProfile profile;
    bool shaderSlotFromRule = false;
    if (const auto action = resolved.slot(shaderSlotFor(eventPath))) {
        profile.effectId = action->params.value(ActionParam::EffectId).toString();
        profile.parameters = action->params.value(ActionParam::Params).toObject().toVariantMap();
        shaderSlotFromRule = true;
    } else {
        profile = tree.resolve(eventPath);
    }

    // Timing slot: a filled slot with durationMs > 0 wins (clamped to the
    // standard envelope). durationMs <= 0 is the "inherit" sentinel — fall
    // through to the caller-supplied default identically to the
    // resolveAnimationDuration shim.
    int durationMs = defaultDurationMs;
    if (const auto action = resolved.slot(timingSlotFor(eventPath))) {
        const int candidate = action->params.value(ActionParam::DurationMs).toInt(0);
        if (candidate > 0) {
            durationMs = qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, candidate,
                                PhosphorAnimation::Limits::MaxAnimationDurationMs);
        }
    }
    return ResolvedShaderAndDuration{std::move(profile), durationMs, shaderSlotFromRule};
}

PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorWindowRules::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const PhosphorWindowRules::WindowQuery& query,
                                                         const QString& eventPath, const QString& windowId,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry)
{
    if (!query.hasWindow() || eventPath.isEmpty()) {
        return base;
    }
    // Use the cached path so motion + shader + opacity lookups all share a
    // single per-window cascade walk. The previous uncached call did its
    // own resolve() against the same query as the sister combined
    // function — wasted work even though motion-profile lookups are
    // per-event, not per-frame.
    const PhosphorWindowRules::ResolvedActions resolved =
        windowId.isEmpty() ? evaluator.resolve(query) : evaluator.resolveCached(windowId, query);
    const auto curveAction = resolved.slot(curveSlotFor(eventPath));
    const auto timingAction = resolved.slot(timingSlotFor(eventPath));
    if (!curveAction && !timingAction) {
        return base;
    }
    PhosphorAnimation::Profile out = base;
    // Curve cascade: prefer the dedicated `anim-curve:` slot. Fall through to
    // the legacy `anim-timing:` slot's curve field so rules authored before
    // the curve/timing split (including those produced by the v3→v4
    // migration in configmigration.cpp::animationAppRuleToWindowRule) still
    // resolve.
    QString curve = curveAction ? curveAction->params.value(ActionParam::Curve).toString() : QString();
    if (curve.isEmpty() && timingAction) {
        curve = timingAction->params.value(ActionParam::Curve).toString();
    }
    if (!curve.isEmpty()) {
        // tryCreate (NOT create) — a malformed curve string stays on the
        // base curve rather than silently coercing to OutCubic and swapping
        // out the user's configured global curve.
        if (auto compiled = curveRegistry.tryCreate(curve)) {
            out.curve = std::move(compiled);
        }
    }
    if (timingAction) {
        const int durationMs = timingAction->params.value(ActionParam::DurationMs).toInt(0);
        if (durationMs > 0) {
            // Same clamp as resolveAnimationShaderAndDuration so the motion
            // and shader paths stay in lockstep for the same user-facing rule.
            out.duration = static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, durationMs,
                                                     PhosphorAnimation::Limits::MaxAnimationDurationMs));
        }
    }
    return out;
}

std::optional<qreal> resolveWindowOpacity(const PhosphorWindowRules::ResolvedActions& resolved)
{
    // The opacity-slot id is a constant; hoist its QString materialisation
    // out of the per-paint hot path so each `resolveWindowOpacity` call
    // reuses the same heap allocation. `static const` is thread-safe under
    // C++11+ [stmt.dcl] without further synchronisation.
    static const QString kOpacitySlot = QString(PhosphorWindowRules::ActionSlot::Opacity);
    const auto action = resolved.slot(kOpacitySlot);
    if (!action) {
        return std::nullopt;
    }
    // The action descriptor's validator in ruleaction.cpp already rejected
    // rules whose `value` falls outside [0.0, 1.0] at load time, but a
    // future loader path (legacy migration, hand-edited JSON) could land an
    // out-of-range value here. Validate at the consumer too — defence in
    // depth keeps the paint pipeline from setting a negative opacity (which
    // KWin renders as "invisible window the user can still focus through").
    //
    // Gate on the QVariant conversion path because the QJsonObject may
    // have been populated programmatically (rule editor builder, future
    // migration porter) with a `QVariant(int)` or numeric-string payload
    // that round-trips through `QJsonValue` as a non-Double type. JSON-
    // parsed numeric literals all surface as `QJsonValue::Double` (Qt6
    // collapses int/float at parse time), but construction paths that
    // bypass the parser don't.
    //
    // Extraction MUST go through `toVariant().toDouble(&ok)` rather than
    // `raw.toDouble()` — the latter is the QJsonValue method, which
    // returns 0.0 for any non-Double JSON type. A `"value": "0.5"`
    // payload would pass the canConvert gate but `raw.toDouble()` would
    // return 0.0, silently rendering the window completely invisible.
    const QJsonValue raw = action->params.value(PhosphorWindowRules::ActionParam::Value);
    if (raw.isNull() || raw.isUndefined()) {
        return std::nullopt;
    }
    // QVariant::toDouble on a bool returns 1.0/0.0 with ok=true — surfacing
    // a programmatically-built `"value": true` rule as a fully-visible /
    // completely-invisible window. The action descriptor's load-time
    // validator already rejects bools (it requires `isDouble`); reject
    // them at the consumer too so a runtime-constructed payload that
    // bypasses the parser can't silently change opacity behaviour.
    const QVariant v = raw.toVariant();
    if (v.typeId() == QMetaType::Bool) {
        return std::nullopt;
    }
    bool ok = false;
    const double value = v.toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    if (value < 0.0 || value > 1.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<ResolvedWindowAppearance> resolveWindowAppearance(const PhosphorWindowRules::ResolvedActions& resolved,
                                                                const QColor& accentColor)
{
    // Each reader re-validates the param type even though the load-time
    // descriptor validators already did — defence in depth against a
    // programmatically-built / hand-edited payload that bypassed the parser
    // (see the equivalent rationale in resolveWindowOpacity).
    const auto boolSlot = [&resolved](QLatin1StringView slot) -> std::optional<bool> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorWindowRules::ActionParam::Value);
        if (!v.isBool()) {
            return std::nullopt;
        }
        return v.toBool();
    };
    // Upper bounds mirror the load-time descriptor validators in
    // ruleaction.cpp (kMaxBorderWidth / kMaxBorderRadius) so the consumer
    // re-validation is genuinely symmetric — a programmatically-built /
    // hand-edited payload with width:5000 is rejected here, not drawn.
    constexpr double kMaxBorderWidth = 10.0;
    constexpr double kMaxBorderRadius = 20.0;
    const auto intSlot = [&resolved](QLatin1StringView slot, double maxValue) -> std::optional<int> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorWindowRules::ActionParam::Value);
        if (!v.isDouble()) {
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (d < 0.0 || d > maxValue) {
            return std::nullopt;
        }
        return static_cast<int>(d);
    };
    // Both colours live on the single BorderColor slot action (`active` /
    // `inactive` params). The accent sentinel resolves to the live accent;
    // anything else parses as a hex colour.
    const auto borderColorParam = [&resolved, &accentColor](QLatin1StringView paramKey) -> std::optional<QColor> {
        const auto action = resolved.slot(QString(PhosphorWindowRules::ActionSlot::BorderColor));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(QString(paramKey));
        if (!v.isString()) {
            return std::nullopt;
        }
        const QString s = v.toString();
        if (s == PhosphorWindowRules::BorderColorToken::Accent) {
            return accentColor.isValid() ? std::optional<QColor>(accentColor) : std::nullopt;
        }
        const QColor color(s);
        if (!color.isValid()) {
            return std::nullopt;
        }
        return color;
    };

    ResolvedWindowAppearance out;
    out.hideTitleBar = boolSlot(PhosphorWindowRules::ActionSlot::HideTitleBar);
    out.showBorder = boolSlot(PhosphorWindowRules::ActionSlot::BorderVisible);
    out.borderWidth = intSlot(PhosphorWindowRules::ActionSlot::BorderWidth, kMaxBorderWidth);
    out.borderRadius = intSlot(PhosphorWindowRules::ActionSlot::BorderRadius, kMaxBorderRadius);
    out.activeColor = borderColorParam(PhosphorWindowRules::ActionParam::Active);
    out.inactiveColor = borderColorParam(PhosphorWindowRules::ActionParam::Inactive);
    // An omitted `inactive` mirrors the active colour, matching the retired
    // global behaviour where a window with no distinct inactive colour kept its
    // active border when unfocused.
    if (!out.inactiveColor) {
        out.inactiveColor = out.activeColor;
    }
    if (!out.any()) {
        return std::nullopt;
    }
    return out;
}

} // namespace PlasmaZones
