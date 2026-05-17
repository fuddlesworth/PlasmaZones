// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadertransitionmanager.h"

#include <PhosphorWindowRule/AnimationAppRuleBridge.h>

namespace PlasmaZones {

ShaderTransitionManager::ShaderTransitionManager(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
    // Pool initialization happens in PlasmaZonesEffect's constructor
    // (lifecycle.cpp) where the setMaxThreadCount(1) call lives, since
    // the pool start logic references effect internals (QPointer<effect>).
}

ShaderTransitionManager::~ShaderTransitionManager() = default;

void ShaderTransitionManager::rebuildAnimationRuleSet()
{
    // `setRules` always bumps the rule set's revision, so the bound evaluator
    // observes the change and discards stale `(windowId, revision)` cache
    // entries on next access. The bridge drops empty `classPattern` /
    // `eventPath` entries — defensive, the App Rule loader already does.
    const PhosphorWindowRule::WindowRuleSet rebuilt =
        PhosphorWindowRule::AnimationAppRuleBridge::toRuleSet(m_animationAppRules);
    m_animationRuleSet.setRules(rebuilt.rules());
}

} // namespace PlasmaZones
