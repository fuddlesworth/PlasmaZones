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
    // (SetEngineMode / SetSnappingLayout / SetTilingAlgorithm), i.e. the rule
    // is a PURE assignment. False on an empty action list as well — a context
    // match with no actions is not an assignment rule.
    //
    // NOT the claim predicate. findExactContextRule used to gate on this and
    // now uses hasAnyAssignmentSlotAction, because the rebuild paths carry
    // non-assignment actions across (carryOverNonAssignmentActions) and so no
    // longer strip a mixed rule. The remaining users are the ones that need
    // "pure" specifically: purgeSnappingLayoutFromAssignments' Shape-1 gate,
    // and the two batch family DROPS, which spare a mixed rule so the rebuild
    // can merge onto it rather than shadow it.
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

bool hasAnyAssignmentSlotAction(const PWR::Rule& rule)
{
    // True when at least ONE action fills an assignment slot, regardless of
    // what else the rule carries. isPureAssignmentRule is the stricter "and
    // nothing else" form; this is the claim predicate, used where a MIXED
    // rule is still legitimately the rule that assigns a context. Admits a
    // layout-only rule (SetSnappingLayout with no SetEngineMode), which the
    // batch rebuild relies on.
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)
            || action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
            || action.type == QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            return true;
        }
    }
    return false;
}

void carryOverNonAssignmentActions(PWR::Rule& rebuilt, const PWR::Rule& existing)
{
    // makeAssignmentRule emits ONLY the three assignment slot actions, so a
    // wholesale `rebuilt` would silently destroy anything else the rule
    // carried. The Monitors page and the Rules editor address the same rule
    // by its deterministic context id, so a user who adds SetOpacity (or
    // LockContext, or an animation override) to a context assignment rule and
    // later changes that context's layout would lose the extra action with no
    // diagnostic.
    //
    // Purity gates elsewhere refuse to CLAIM a mixed rule, which protects the
    // paths that can walk away. The rebuild paths cannot: the deterministic id
    // means `rebuilt` collides with the mixed rule whether or not it was
    // claimed, so the only safe rebuild is a merge. Slot actions come from
    // `rebuilt` (they are what the caller is changing); everything else rides
    // across in its original order.
    for (const PWR::RuleAction& action : existing.actions) {
        if (action.type != QLatin1String(PWR::ActionType::SetEngineMode)
            && action.type != QLatin1String(PWR::ActionType::SetSnappingLayout)
            && action.type != QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            rebuilt.actions.append(action);
        }
    }
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
            // libs/phosphor-rules/src/ruleaction_builtins_p.h — today
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

int nextAssignmentPriority(const QList<PWR::Rule>& rules)
{
    bool sawAny = false;
    int maxPriority = 0;
    // Scan only assignment rules (those carrying a SetEngineMode action). A
    // user-authored layout-only context rule is not counted, so creating an
    // assignment while such a rule outranks it is the one case the seed can
    // land below an existing rule; the user resolves it by dragging.
    for (const PWR::Rule& rule : rules) {
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        if (!sawAny || rule.priority > maxPriority) {
            maxPriority = rule.priority;
            sawAny = true;
        }
    }
    // No assignment rule yet — seed at the Context band top so the first
    // assignment lands inside the Settings Context band; later creates climb
    // from the running max. The unbounded `max + 1` climb is deliberate: it is
    // how "the newest assignment wins" is realised under priority-wins (a
    // clamp would tie the new rule with the current top and lose the tie by
    // list order). After enough concurrent assignments the climb can cross out
    // of the Context band; a Settings reorder renormalises it back, and the
    // band number only affects cross-section display order, never which
    // assignment a context resolves to.
    if (!sawAny) {
        return PWR::ContextRuleBridge::kContextBandBase + 99;
    }
    return maxPriority + 1;
}

} // namespace PhosphorZones::RuleHelpers
