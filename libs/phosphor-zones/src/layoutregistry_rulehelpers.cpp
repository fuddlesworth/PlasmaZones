// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Rule-shape classification and context helpers for the LayoutRegistry
// assignment cascade — see layoutregistry_rulehelpers_p.h for the rationale
// behind the split out of layoutregistry_assignments.cpp.

#include "layoutregistry_rulehelpers_p.h"

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>

namespace PhosphorZones::RuleHelpers {

namespace CRB = PhosphorWindowRule::ContextRuleBridge;

PWR::WindowQuery makeContextQuery(const QString& screenId, int virtualDesktop, const QString& activity)
{
    PWR::WindowQuery query;
    query.screenId = screenId;
    query.virtualDesktop = virtualDesktop;
    query.activity = activity;
    return query;
}

QString contextRuleName(const QString& screenId, int virtualDesktop, const QString& activity)
{
    return screenId + (virtualDesktop > 0 ? QStringLiteral(" · Desktop ") + QString::number(virtualDesktop) : QString())
        + (activity.isEmpty() ? QString() : QStringLiteral(" · Activity"));
}

ContextDims decodeDims(const PWR::MatchExpression& match)
{
    ContextDims dims;
    CRB::contextDimsOf(match, dims.screenId, dims.virtualDesktop, dims.activity);
    return dims;
}

bool matchIsExactContext(const PWR::MatchExpression& match, const QString& screenId, int virtualDesktop,
                         const QString& activity)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return dims.screenId == screenId && dims.virtualDesktop == virtualDesktop && dims.activity == activity;
}

bool hasEngineModeAction(const PWR::WindowRule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            return true;
        }
    }
    return false;
}

bool matchIsExactContextBase(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && dims.virtualDesktop == 0 && dims.activity.isEmpty();
}

bool matchIsExactContextDesktop(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && dims.virtualDesktop > 0 && dims.activity.isEmpty();
}

bool matchIsExactContextActivity(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && !dims.activity.isEmpty();
}

bool isContextAssignmentRule(const PWR::WindowRule& rule)
{
    if (!hasEngineModeAction(rule) || rule.match.isCatchAll()) {
        return false;
    }
    return matchIsExactContextBase(rule.match) || matchIsExactContextDesktop(rule.match)
        || matchIsExactContextActivity(rule.match);
}

AssignmentEntry entryFromRuleMatchActions(const PWR::WindowRule& rule)
{
    AssignmentEntry entry;
    // A rule with no SetEngineMode action leaves the mode at the Snapping
    // default — make that explicit rather than relying on AssignmentEntry's
    // default member initializer.
    entry.mode = AssignmentEntry::Snapping;
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            entry.mode = action.params.value(QLatin1String("mode")).toString() == QLatin1String("autotile")
                ? AssignmentEntry::Autotile
                : AssignmentEntry::Snapping;
        } else if (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)) {
            entry.snappingLayout = action.params.value(QLatin1String("layoutId")).toString();
        } else if (action.type == QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            entry.tilingAlgorithm = action.params.value(QLatin1String("algorithm")).toString();
        }
    }
    return entry;
}

} // namespace PhosphorZones::RuleHelpers
