// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationAppRuleResolver.h>

#include <PhosphorAnimation/CurveRegistry.h>

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
        return rule->durationMs;
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
        out.duration = static_cast<qreal>(rule->durationMs);
    }
    return out;
}

} // namespace PhosphorAnimationShaders
