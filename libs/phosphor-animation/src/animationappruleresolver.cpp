// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationAppRuleResolver.h>

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>

#include <QtGlobal>

namespace PhosphorAnimationShaders {

ShaderProfile resolveAnimationShaderProfile(const AnimationAppRuleList& rules, const ShaderProfileTree& tree,
                                            const QString& windowClass, const QString& eventPath)
{
    if (!windowClass.isEmpty()) {
        if (const auto rule = rules.resolveShader(windowClass, eventPath)) {
            ShaderProfile profile;
            // Engaged-empty effectId is the rule's "block the per-event
            // default for matching windows" sentinel and matches
            // ShaderProfile's own engaged-empty contract verbatim.
            profile.effectId = rule->effectId;
            profile.parameters = rule->shaderParams;
            return profile;
        }
    }
    return tree.resolve(eventPath);
}

int resolveAnimationDuration(const AnimationAppRuleList& rules, const QString& windowClass, const QString& eventPath,
                             int defaultDurationMs)
{
    if (windowClass.isEmpty()) {
        return defaultDurationMs;
    }
    const auto rule = rules.resolveTiming(windowClass, eventPath);
    if (rule && rule->durationMs > 0) {
        // Clamp to the same `[Min, Max]AnimationDurationMs` envelope
        // the daemon-bringup loader applies to the global animation
        // duration. Without this, a malformed rule with a huge
        // `durationMs` would feed an unbounded value into the
        // kwin-effect's `QTimer::singleShot` teardown timer (and the
        // shader transition's per-frame elapsed-time math), producing
        // multi-second-to-day-long shader states. The motion-profile
        // path goes through `WindowAnimator::clampProfile` so it's
        // already capped; this brings the shader path in line.
        return qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, rule->durationMs,
                      PhosphorAnimation::Limits::MaxAnimationDurationMs);
    }
    return defaultDurationMs;
}

PhosphorAnimation::Profile resolveAnimationMotionProfile(const AnimationAppRuleList& rules,
                                                         const PhosphorAnimation::Profile& base,
                                                         const QString& windowClass, const QString& eventPath,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry)
{
    if (windowClass.isEmpty()) {
        return base;
    }
    const auto rule = rules.resolveTiming(windowClass, eventPath);
    if (!rule) {
        return base;
    }
    PhosphorAnimation::Profile out = base;
    if (!rule->curve.isEmpty()) {
        // tryCreate (NOT create) — malformed curve strings stay on the
        // base curve. CurveRegistry::create silently coerces failures to
        // OutCubic, which would mask typos in the rule's curve field
        // and silently swap out the user's configured global curve;
        // tryCreate returns nullptr on failure so we keep the base.
        if (auto curve = curveRegistry.tryCreate(rule->curve)) {
            out.curve = std::move(curve);
        }
    }
    if (rule->durationMs > 0) {
        // Clamp to the same `[Min, Max]AnimationDurationMs` envelope
        // `resolveAnimationDuration` applies. Without this, a Timing
        // rule with `durationMs == 8000` would produce a motion
        // override whose duration is bounded only by `WindowAnimator::
        // clampProfile`'s `kMaxDurationMs == 10000`, while the
        // matching shader cascade in the same `applySnapGeometry`
        // block would resolve through `resolveAnimationDuration` and
        // see ~2000ms — desyncing visual motion vs shader for the same
        // user-facing rule. Sub-50ms rule durations would also bypass
        // the daemon's `MinAnimationDurationMs` floor on this path.
        out.duration = static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, rule->durationMs,
                                                 PhosphorAnimation::Limits::MaxAnimationDurationMs));
    }
    return out;
}

} // namespace PhosphorAnimationShaders
