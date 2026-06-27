// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/ExclusionRules.h>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

namespace PhosphorRules {
namespace ExclusionRules {

namespace {

// True iff @p rule carries at least one action whose `type` matches
// @p actionType. File-local: the two slicers below are the canonical
// callers, and the only external caller a future addition could justify
// is a "match against an arbitrary action type" predicate that we don't
// have today. Promoted only when a second caller appears.
bool ruleHasAction(const Rule& rule, QLatin1StringView actionType)
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

// Walk @p source and return a derived `RuleSet` containing only
// the ENABLED rules whose action list includes @p actionType. Rule ids,
// priorities, and matches are copied verbatim — the derived set gets
// bound to a `RuleEvaluator` downstream and has to preserve the
// source rule's resolution semantics exactly. An empty source yields
// an empty set so callers keep a `!set.isEmpty()` fast path.
RuleSet rulesWithAction(const RuleSet& source, QLatin1StringView actionType)
{
    QList<Rule> kept;
    kept.reserve(source.count());
    for (const Rule& rule : source.rules()) {
        if (rule.enabled && ruleHasAction(rule, actionType)) {
            kept.append(rule);
        }
    }
    RuleSet derived;
    derived.setRules(kept);
    return derived;
}

} // namespace

RuleSet excludeRulesFrom(const RuleSet& source)
{
    return rulesWithAction(source, ActionType::Exclude);
}

RuleSet excludeAnimationsRulesFrom(const RuleSet& source)
{
    return rulesWithAction(source, ActionType::ExcludeAnimations);
}

QStringList applicationExcludePatternsFrom(const RuleSet& source)
{
    QStringList patterns;
    for (const Rule& rule : source.rules()) {
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
} // namespace PhosphorRules
