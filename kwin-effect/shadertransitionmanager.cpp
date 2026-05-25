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
    // Combine both animation rule sources: the bridge-converted legacy
    // AnimationAppRules and any rules from the unified `windowrules.json`
    // store that carry an OverrideAnimation* action. `setRules` always bumps
    // the rule set's revision, so the bound evaluator observes the change and
    // discards stale `(windowId, revision)` cache entries on next access.
    // The bridge drops empty `classPattern` / `eventPath` entries — defensive,
    // the App Rule loader already does.
    const PhosphorWindowRule::WindowRuleSet bridged =
        PhosphorWindowRule::AnimationAppRuleBridge::toRuleSet(m_animationAppRules);
    QList<PhosphorWindowRule::WindowRule> combined = bridged.rules();
    combined.reserve(combined.size() + m_windowRuleAnimationRules.size());
    for (const auto& rule : m_windowRuleAnimationRules) {
        combined.append(rule);
    }
    // setRules sorts by descending priority on read via the evaluator's
    // priority-order index, so the concatenation order is irrelevant — a
    // bridged rule and a unified-store rule with the same priority break ties
    // by list order, which is the existing first-matching-rule-wins-per-slot
    // contract.
    m_animationRuleSet.setRules(combined);
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
