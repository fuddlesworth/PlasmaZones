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

    // Recompute the action-presence gates: SetOpacity for the per-frame
    // opacity resolve (see hasOpacityRules()) and SetWindowLayer for the
    // layer reconcile / bulk sweep (see hasWindowLayerRules()). Only enabled
    // rules count — a disabled rule resolves to no override, so it must not
    // force the per-window query build.
    m_hasOpacityRules = false;
    m_hasWindowLayerRules = false;
    for (const PhosphorRules::Rule& rule : m_ruleAnimationRules) {
        if (!rule.enabled) {
            continue;
        }
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (action.type == PhosphorRules::ActionType::SetOpacity) {
                m_hasOpacityRules = true;
            } else if (action.type == PhosphorRules::ActionType::SetWindowLayer) {
                m_hasWindowLayerRules = true;
            }
        }
        if (m_hasOpacityRules && m_hasWindowLayerRules) {
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
