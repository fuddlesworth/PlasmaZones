// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRules/ExclusionRules.h>

#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/MatchTypes.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>
#include <PhosphorWindowRules/WindowRuleSet.h>

namespace PhosphorWindowRules {
namespace ExclusionRules {

namespace {

// True iff @p rule carries at least one action whose `type` matches
// @p actionType. File-local: the two slicers below are the canonical
// callers, and the only external caller a future addition could justify
// is a "match against an arbitrary action type" predicate that we don't
// have today. Promoted only when a second caller appears.
bool ruleHasAction(const WindowRule& rule, QLatin1StringView actionType)
{
    for (const RuleAction& action : rule.actions) {
        // Compare via Qt6's `QString::operator==(QLatin1StringView)` overload
        // — wrapping in `QString(actionType)` would heap-allocate per rule
        // for a comparison the overload performs without allocation.
        if (action.type == actionType) {
            return true;
        }
    }
    return false;
}

// Walk @p source and return a derived `WindowRuleSet` containing only
// the ENABLED rules whose action list includes @p actionType. Rule ids,
// priorities, and matches are copied verbatim — the derived set gets
// bound to a `RuleEvaluator` downstream and has to preserve the
// source rule's resolution semantics exactly. An empty source yields
// an empty set so callers keep a `!set.isEmpty()` fast path.
WindowRuleSet rulesWithAction(const WindowRuleSet& source, QLatin1StringView actionType)
{
    QList<WindowRule> kept;
    kept.reserve(source.count());
    for (const WindowRule& rule : source.rules()) {
        if (rule.enabled && ruleHasAction(rule, actionType)) {
            kept.append(rule);
        }
    }
    WindowRuleSet derived;
    derived.setRules(kept);
    return derived;
}

} // namespace

WindowRuleSet excludeRulesFrom(const WindowRuleSet& source)
{
    return rulesWithAction(source, ActionType::Exclude);
}

WindowRuleSet excludeAnimationsRulesFrom(const WindowRuleSet& source)
{
    return rulesWithAction(source, ActionType::ExcludeAnimations);
}

QStringList applicationExcludePatternsFrom(const WindowRuleSet& source)
{
    QStringList patterns;
    for (const WindowRule& rule : source.rules()) {
        // Skip disabled rules — the daemon's pending-restore prune
        // consumes the returned patterns to discard queued restores for
        // matching apps, so harvesting from a disabled rule would prune
        // restores the user explicitly opted into keeping. Mirrors the
        // disabled-rule skip in `rulesWithAction` above for symmetry,
        // since callers may hand this helper an unfiltered set
        // (e.g. straight from the unified store) rather than the
        // already-sliced exclude set.
        if (!rule.enabled || !ruleHasAction(rule, ActionType::Exclude)) {
            continue;
        }
        const MatchExpression& match = rule.match;
        if (match.kind() != MatchExpression::Kind::Leaf) {
            continue;
        }
        const MatchExpression::Predicate& leaf = match.predicate();
        if (leaf.field != Field::AppId || leaf.op != Operator::AppIdMatches) {
            continue;
        }
        const QString pattern = leaf.value.toString().trimmed();
        if (pattern.isEmpty()) {
            continue;
        }
        patterns.append(pattern);
    }
    return patterns;
}

} // namespace ExclusionRules
} // namespace PhosphorWindowRules
