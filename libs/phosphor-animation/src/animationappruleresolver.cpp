// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationAppRuleResolver.h>

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

} // namespace PhosphorAnimationShaders
