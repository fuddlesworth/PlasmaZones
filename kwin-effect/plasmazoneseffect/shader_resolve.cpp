// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Effect-local shims that reimplement the animation App Rule cascade on top
// of PhosphorWindowRule::RuleEvaluator. The bridge (AnimationAppRuleBridge.h)
// converts the App Rule list into a WindowRuleSet; these shims walk the
// resolved event-scoped slots and reproduce, byte-identically, the behaviour
// the standalone `PhosphorAnimationShaders::resolveAnimation*` resolvers
// produced — including the duration clamp, the curve `tryCreate` fallback,
// the engaged-empty `effectId` sentinel, and the empty-input short-circuits.
//
// This translation unit never sees a KWin type — it works purely off the
// rule engine and the animation library value types, so the LGPL boundary
// stays clean.

#include "shader_resolve.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowQuery.h>

#include <QtGlobal>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kKeyEffectId{"effectId"};
constexpr QLatin1String kKeyParams{"params"};
constexpr QLatin1String kKeyCurve{"curve"};
constexpr QLatin1String kKeyDurationMs{"durationMs"};

/// A per-window query carrying only the window class — the animation App
/// Rules match exclusively on `WindowClass Contains <pattern>`, so no other
/// attribute needs populating. The context fields stay at their defaults
/// (they are never referenced by an animation rule's match expression).
PhosphorWindowRule::WindowQuery animationQuery(const QString& windowClass)
{
    PhosphorWindowRule::WindowQuery query;
    query.windowClass = windowClass;
    return query;
}

/// The event-scoped shader slot id for @p eventPath.
QString shaderSlotFor(const QString& eventPath)
{
    return QString(PhosphorWindowRule::ActionSlot::AnimShaderPrefix) + eventPath;
}

/// The event-scoped timing slot id for @p eventPath.
QString timingSlotFor(const QString& eventPath)
{
    return QString(PhosphorWindowRule::ActionSlot::AnimTimingPrefix) + eventPath;
}

} // namespace

PhosphorAnimationShaders::ShaderProfile
resolveAnimationShaderProfile(const PhosphorWindowRule::RuleEvaluator& evaluator,
                              const PhosphorAnimationShaders::ShaderProfileTree& tree, const QString& windowClass,
                              const QString& eventPath)
{
    // Empty-input short-circuit — mirrors the standalone resolver's header
    // contract for both an empty windowClass and an empty eventPath. An empty
    // eventPath also maps to no slot (the bridge drops such rules), so the
    // lookup below would fall through anyway; the explicit guard keeps the
    // documented behaviour pinned independent of the bridge.
    if (!windowClass.isEmpty() && !eventPath.isEmpty()) {
        const PhosphorWindowRule::ResolvedActions resolved = evaluator.resolve(animationQuery(windowClass));
        const auto action = resolved.slot(shaderSlotFor(eventPath));
        if (action) {
            // A filled slot — a Shader rule matched. effectId is taken
            // verbatim: an engaged-empty effectId is the rule's "block the
            // per-event default for matching windows" sentinel, and
            // ShaderProfile's own engaged-empty contract preserves it.
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = action->params.value(kKeyEffectId).toString();
            profile.parameters = action->params.value(kKeyParams).toObject().toVariantMap();
            return profile;
        }
    }
    // No rule matched (slot unfilled) — fall through to the per-event tree.
    return tree.resolve(eventPath);
}

int resolveAnimationDuration(const PhosphorWindowRule::RuleEvaluator& evaluator, const QString& windowClass,
                             const QString& eventPath, int defaultDurationMs)
{
    if (windowClass.isEmpty() || eventPath.isEmpty()) {
        return defaultDurationMs;
    }
    const PhosphorWindowRule::ResolvedActions resolved = evaluator.resolve(animationQuery(windowClass));
    const auto action = resolved.slot(timingSlotFor(eventPath));
    if (action) {
        // A Timing rule omits `durationMs` when it is <= 0 (the "inherit"
        // sentinel), so an absent key resolves to 0 here and falls through.
        const int durationMs = action->params.value(kKeyDurationMs).toInt(0);
        if (durationMs > 0) {
            // Clamp to the same envelope the standalone resolver applied — a
            // malformed rule must not feed an unbounded duration into the
            // teardown timer or the per-frame elapsed-time math.
            return qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, durationMs,
                          PhosphorAnimation::Limits::MaxAnimationDurationMs);
        }
    }
    return defaultDurationMs;
}

PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorWindowRule::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const QString& windowClass, const QString& eventPath,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry)
{
    if (windowClass.isEmpty() || eventPath.isEmpty()) {
        return base;
    }
    const PhosphorWindowRule::ResolvedActions resolved = evaluator.resolve(animationQuery(windowClass));
    const auto action = resolved.slot(timingSlotFor(eventPath));
    if (!action) {
        return base;
    }
    PhosphorAnimation::Profile out = base;
    const QString curve = action->params.value(kKeyCurve).toString();
    if (!curve.isEmpty()) {
        // tryCreate (NOT create) — a malformed curve string stays on the
        // base curve rather than silently coercing to OutCubic and swapping
        // out the user's configured global curve.
        if (auto compiled = curveRegistry.tryCreate(curve)) {
            out.curve = std::move(compiled);
        }
    }
    const int durationMs = action->params.value(kKeyDurationMs).toInt(0);
    if (durationMs > 0) {
        // Same clamp as resolveAnimationDuration so the motion and shader
        // paths stay in lockstep for the same user-facing rule.
        out.duration = static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, durationMs,
                                                 PhosphorAnimation::Limits::MaxAnimationDurationMs));
    }
    return out;
}

} // namespace PlasmaZones
