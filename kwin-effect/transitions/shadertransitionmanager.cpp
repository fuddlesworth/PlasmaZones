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

    // The animation/appearance evaluator resolves per-window OVERRIDES
    // (animation shader/timing/curve, opacity, border, layer). Its bound set
    // admits any rule carrying a Tag::Effect action, and such a rule may
    // co-carry a terminal exclusion from another scope (hand-edited
    // rules.json; the editor's validator blocks saving the shape). Honour
    // only the blanket Exclude and the animation-scoped ExcludeAnimations as
    // walk-stoppers here — a placement- or decoration-scoped exclusion must
    // not cancel animation/appearance resolution, per those actions'
    // documented scope. The dedicated exclusion evaluators still enforce
    // their own families regardless of what happens in this one.
    m_animationRuleEvaluator.setTerminalActionScope(
        {QString(PhosphorRules::ActionType::Exclude), QString(PhosphorRules::ActionType::ExcludeAnimations)});

    // The VERDICT evaluator (OpenFullscreen / ScrollFactor) honours the
    // BLANKET Exclude only. ExcludeAnimations is deliberately absent: neither
    // verdict is an animation, so a rule saying "no animations for this app"
    // must not stop the walk before the verdict slot fills — which is exactly
    // what happened while these two actions rode the animation evaluator.
    m_effectVerdictRuleEvaluator.setTerminalActionScope({QString(PhosphorRules::ActionType::Exclude)});
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
    m_hasAnimationShaderRules = false;
    for (const PhosphorRules::Rule& rule : m_ruleAnimationRules) {
        if (!rule.enabled) {
            continue;
        }
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (action.type == PhosphorRules::ActionType::SetOpacity) {
                m_hasOpacityRules = true;
            } else if (action.type == PhosphorRules::ActionType::SetWindowLayer) {
                m_hasWindowLayerRules = true;
            } else if (action.type == PhosphorRules::ActionType::OverrideAnimationShader) {
                m_hasAnimationShaderRules = true;
            }
        }
        if (m_hasOpacityRules && m_hasWindowLayerRules && m_hasAnimationShaderRules) {
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

void ShaderTransitionManager::rebuildEffectVerdictRuleSet()
{
    m_effectVerdictRuleSet.setRules(m_effectVerdictRules);

    // The two verdict presence gates live here, not in the animation rebuild:
    // an OpenFullscreen / ScrollFactor rule is admitted to the VERDICT set, so
    // scanning the animation list for them would answer false for every
    // verdict-only rule and permanently disable both features.
    m_hasOpenFullscreenRules = false;
    m_hasScrollFactorRules = false;
    for (const PhosphorRules::Rule& rule : m_effectVerdictRules) {
        if (!rule.enabled) {
            continue;
        }
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (action.type == PhosphorRules::ActionType::OpenFullscreen) {
                m_hasOpenFullscreenRules = true;
            } else if (action.type == PhosphorRules::ActionType::ScrollFactor) {
                m_hasScrollFactorRules = true;
            }
        }
        if (m_hasOpenFullscreenRules && m_hasScrollFactorRules) {
            break;
        }
    }
}

void ShaderTransitionManager::setEffectVerdictRules(QList<PhosphorRules::Rule> rules)
{
    if (m_effectVerdictRules == rules) {
        // No-op rewrite — keep the evaluator's match cache warm.
        return;
    }
    m_effectVerdictRules = std::move(rules);
    rebuildEffectVerdictRuleSet();
}

} // namespace PlasmaZones
