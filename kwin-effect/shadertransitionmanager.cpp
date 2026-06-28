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
    m_animationRuleSet.setRules(m_ruleAnimationRules);

    // Recompute the SetOpacity-presence gate for the per-frame opacity resolve
    // (see hasOpacityRules()). Only enabled rules count — a disabled opacity
    // rule resolves to no override, so it must not force the per-window query
    // build every frame.
    m_hasOpacityRules = false;
    for (const PhosphorRules::Rule& rule : m_ruleAnimationRules) {
        if (!rule.enabled) {
            continue;
        }
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (action.type == PhosphorRules::ActionType::SetOpacity) {
                m_hasOpacityRules = true;
                break;
            }
        }
        if (m_hasOpacityRules) {
            break;
        }
    }
}

void ShaderTransitionManager::setRuleAnimationRules(QList<PhosphorRules::Rule> rules)
{
    if (m_ruleAnimationRules == rules) {
        // No-op rewrite — keep the evaluator's match cache warm.
        return;
    }
    m_ruleAnimationRules = std::move(rules);
    rebuildAnimationRuleSet();
}

} // namespace PlasmaZones
