// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadertransitionmanager.h"

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
    // setRules always bumps the rule set's revision, so the bound evaluator
    // observes the change and discards stale `(windowId, revision)` cache
    // entries on next access.
    m_animationRuleSet.setRules(m_windowRuleAnimationRules);
}

void ShaderTransitionManager::setWindowRuleAnimationRules(QList<PhosphorWindowRule::WindowRule> rules)
{
    if (m_windowRuleAnimationRules == rules) {
        // No-op rewrite — keep the evaluator's match cache warm.
        return;
    }
    m_windowRuleAnimationRules = std::move(rules);
    rebuildAnimationRuleSet();
}

} // namespace PlasmaZones
