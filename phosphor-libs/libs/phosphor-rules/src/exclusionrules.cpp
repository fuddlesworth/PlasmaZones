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
// @p actionType. File-local, with five call sites across the three helper
// predicates below (rulesWithAction, rulesWithEitherAction,
// isPlacementExclusion), which the public slicers and the pattern harvester
// reach transitively; it stays here because every caller is in this file
// and the predicate is meaningless without a concrete action-type
// vocabulary to apply it to.
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

// Two-action variant for the decoration union slice (Exclude ∪
// ExcludeDecorations). Kept separate from rulesWithAction rather than
// generalised to a list parameter: one caller with exactly two types, and
// the flat second check keeps the per-rule cost at two non-allocating
// comparisons. (The placement union walks its own loop through
// isPlacementExclusion below, because its membership predicate is shared
// with the pattern harvester.)
RuleSet rulesWithEitherAction(const RuleSet& source, QLatin1StringView actionTypeA, QLatin1StringView actionTypeB)
{
    QList<Rule> kept;
    kept.reserve(source.count());
    for (const Rule& rule : source.rules()) {
        if (rule.enabled && (ruleHasAction(rule, actionTypeA) || ruleHasAction(rule, actionTypeB))) {
            kept.append(rule);
        }
    }
    RuleSet derived;
    derived.setRules(kept);
    return derived;
}

// The ONE placement-exclusion membership predicate, shared by
// `excludePlacementRulesFrom` and `applicationExcludePatternsFrom` so the
// slice the engines bind and the AppId harvest the pending-restore prune
// walks can never diverge — a third placement-exclusion action added to
// one and not the other would let the engines refuse windows whose stale
// queued restores are never pruned.
bool isPlacementExclusion(const Rule& rule)
{
    return ruleHasAction(rule, ActionType::Exclude) || ruleHasAction(rule, ActionType::ExcludePlacement);
}

} // namespace

RuleSet excludeRulesFrom(const RuleSet& source)
{
    return rulesWithAction(source, ActionType::Exclude);
}

RuleSet excludePlacementRulesFrom(const RuleSet& source)
{
    // Membership must stay in lockstep with applicationExcludePatternsFrom's
    // harvest filter — both go through isPlacementExclusion.
    QList<Rule> kept;
    kept.reserve(source.count());
    for (const Rule& rule : source.rules()) {
        if (rule.enabled && isPlacementExclusion(rule)) {
            kept.append(rule);
        }
    }
    RuleSet derived;
    derived.setRules(kept);
    return derived;
}

RuleSet excludeDecorationsRulesFrom(const RuleSet& source)
{
    return rulesWithEitherAction(source, ActionType::Exclude, ActionType::ExcludeDecorations);
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
        // disabled-rule skip in the slicers above. Every in-tree caller
        // currently hands this helper the ALREADY-sliced placement set, so
        // the enabled and membership re-checks are defensive against a
        // future caller passing the unfiltered store — redundant today,
        // idempotent, and the shared isPlacementExclusion predicate keeps
        // them in lockstep with the slice regardless.
        if (!rule.enabled || !isPlacementExclusion(rule)) {
            continue;
        }
        const MatchExpression& match = rule.match;
        // KNOWN LIMITATION (EXCL-3): only a single `AppId AppIdMatches` leaf
        // is harvested into the pattern list. WindowClass / Equals /
        // composite Exclude or ExcludePlacement rules contribute no pattern,
        // so their stale queued pending-restores aren't pruned through THIS
        // path. Not a leak — the snap engine re-checks each queued placement
        // against the live RuleSet at restore time and discards excluded
        // ones, so growth self-heals (just delayed). A full fix evaluates
        // the placement-exclusion RuleSet against each queued WindowQuery
        // instead of harvesting strings — deferred.
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
    // De-duplicate: a blanket Exclude rule and a scoped ExcludePlacement rule
    // for the same app (the natural result of the excludeApp template
    // retarget landing next to a v4-migrated rule) would otherwise walk the
    // prune's removeIf twice for one pattern. The prune is idempotent, so
    // this only saves the redundant sweep.
    patterns.removeDuplicates();
    return patterns;
}

} // namespace ExclusionRules
} // namespace PhosphorRules
