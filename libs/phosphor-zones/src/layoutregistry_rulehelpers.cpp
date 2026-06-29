// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Rule-shape classification and context helpers for the LayoutRegistry
// assignment cascade — see layoutregistry_rulehelpers_p.h for the rationale
// behind the split out of layoutregistry_assignments.cpp.

#include "layoutregistry_rulehelpers_p.h"

#include "zoneslogging.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

namespace PhosphorZones::RuleHelpers {

namespace CRB = PhosphorRules::ContextRuleBridge;

PWR::WindowQuery makeContextQuery(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const QString& mode)
{
    PWR::WindowQuery query;
    query.screenId = screenId;
    query.virtualDesktop = virtualDesktop;
    query.activity = activity;
    query.mode = mode;
    return query;
}

QString contextRuleName(const QString& screenId, int virtualDesktop, const QString& activity)
{
    // Single formula lives in ContextRuleBridge — this private helper stays as
    // a thin forwarder so callers inside phosphor-zones don't need to reach
    // into the rule namespace directly.
    return CRB::contextRuleName(screenId, virtualDesktop, activity);
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
    // Delegate to the public bridge so there's exactly one implementation
    // of the context-shape predicate across the codebase.
    return CRB::matchIsExactContext(match, screenId, virtualDesktop, activity);
}

bool hasEngineModeAction(const PWR::Rule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            return true;
        }
    }
    return false;
}

bool hasSnappingLayoutAction(const PWR::Rule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)) {
            return true;
        }
    }
    return false;
}

bool hasTilingAlgorithmAction(const PWR::Rule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            return true;
        }
    }
    return false;
}

bool isPureAssignmentRule(const PWR::Rule& rule)
{
    // True when every action belongs to the three assignment slots
    // (SetEngineMode / SetSnappingLayout / SetTilingAlgorithm). Used by
    // the shape-based scan in findExactContextRule to refuse to claim a
    // user-authored rule that carries non-assignment actions
    // (SetOpacity, OverrideAnimation*, Float, Exclude, ...) — admitting
    // such a rule would silently strip those actions through the
    // assignment-rebuild path (upsertAssignmentRule, assignLayout,
    // applyBatchAssignments) since makeAssignmentActions emits only the
    // three slot actions. False on an empty action list as well — a
    // context match with no actions is not an assignment rule.
    if (rule.actions.isEmpty()) {
        return false;
    }
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type != QLatin1String(PWR::ActionType::SetEngineMode)
            && action.type != QLatin1String(PWR::ActionType::SetSnappingLayout)
            && action.type != QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            return false;
        }
    }
    return true;
}

bool matchIsExactContextBase(const PWR::MatchExpression& match)
{
    return CRB::matchIsExactContextBase(match);
}

bool matchIsExactContextDesktop(const PWR::MatchExpression& match)
{
    return CRB::matchIsExactContextDesktop(match);
}

bool matchIsExactContextActivity(const PWR::MatchExpression& match)
{
    return CRB::matchIsExactContextActivity(match);
}

bool isContextAssignmentRule(const PWR::Rule& rule)
{
    if (!hasEngineModeAction(rule) || rule.match.isCatchAll()) {
        return false;
    }
    return matchIsExactContextBase(rule.match) || matchIsExactContextDesktop(rule.match)
        || matchIsExactContextActivity(rule.match);
}

AssignmentEntry entryFromRuleMatchActions(const PWR::Rule& rule)
{
    AssignmentEntry entry;
    // A rule with no SetEngineMode action leaves the mode at the Snapping
    // default — make that explicit rather than relying on AssignmentEntry's
    // default member initializer.
    entry.mode = AssignmentEntry::Snapping;
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            // Decode through `modeFromWireString` so every token the
            // ActionRegistry validator accepts round-trips end-to-end.
            // The canonical vocabulary lives at `engineModeOptions()` in
            // libs/phosphor-rules/src/ruleaction.cpp — today
            // snapping / autotile / scrolling. The previous two-valued
            // `== "autotile"` ternary silently coerced every non-Autotile
            // token to Snapping — including the registered, picker-exposed
            // "scrolling" wire string written by Settings via
            // `modeToWireString(Scrolling)`. Unknown tokens
            // (`modeFromWireString` returns nullopt) leave the entry on
            // its prior value, which is the Snapping default initialized
            // above — matching the bridge's open-vocabulary contract
            // documented in ContextRuleBridge.h's makeAssignmentActions.
            const QString modeToken = action.params.value(PWR::ActionParam::Mode).toString();
            if (const auto mode = modeFromWireString(modeToken)) {
                entry.mode = *mode;
            } else if (!modeToken.isEmpty()) {
                // A non-empty token the closed mode vocabulary doesn't recognize
                // (a typo in a hand-edited rules.json, or a token from a newer
                // schema). We keep the Snapping default rather than reject the
                // rule, but log it so the silent degrade is diagnosable.
                qCWarning(PhosphorZones::lcZonesLib)
                    << "Assignment rule" << rule.id.toString() << "carries an unrecognized SetEngineMode token"
                    << modeToken << "— keeping the Snapping default";
            }
        } else if (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)) {
            entry.snappingLayout = action.params.value(PWR::ActionParam::LayoutId).toString();
        } else if (action.type == QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            entry.tilingAlgorithm = action.params.value(PWR::ActionParam::Algorithm).toString();
        }
    }
    return entry;
}

} // namespace PhosphorZones::RuleHelpers
