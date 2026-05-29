// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "WindowRule.h"
#include "WindowRuleSet.h"

#include <QString>
#include <QStringList>

/**
 * @file ExcludeRuleFilter.h
 * @brief Header-only helpers that extract the Exclude-shaped slice of a
 *        unified `WindowRuleSet`.
 *
 * Replaces the legacy `ExclusionListBridge` (deleted with the v3→v4 fold):
 * the runtime no longer reads exclusions from two flat `QStringList`s on
 * `ISettings`, so consumers that previously asked the bridge "convert these
 * lists into an Exclude rule set" now ask THIS helper "give me the
 * Exclude-shaped slice of the user's unified rule store".
 *
 * The two helpers cover the two consumer shapes:
 *   - `excludeRulesFrom(set)` — for callers that already bind a
 *     `RuleEvaluator` to a small Exclude-only rule set (snap-engine,
 *     effect). Returns a derived set that resolves to `isExcluded()` for
 *     any window whose query matches one of the source set's
 *     Exclude-action rules. The evaluator over the FULL rule set would
 *     incorrectly surface non-Exclude resolutions for unrelated actions —
 *     filtering keeps the "first resolving rule is by construction an
 *     Exclude" invariant the callers' existing code already relies on.
 *   - `applicationExcludePatternsFrom(set)` — for the few legacy
 *     pattern-list consumers (WindowTrackingAdaptor's pending-restore
 *     prune) that aren't ready to swap their string-match path for a
 *     query-based evaluator. Returns the AppId pattern values of every
 *     `AppId AppIdMatches <pattern>` leaf carrying an Exclude action,
 *     mirroring what `ExclusionListBridge::toDaemonRuleSet` used to
 *     produce on the daemon side.
 *
 * Both helpers are header-only so consumers take only an include-time
 * dependency on phosphor-windowrule — there is no link edge added by
 * using them.
 */
namespace PhosphorWindowRule {

namespace ExcludeRuleFilter {

/// True iff @p rule carries at least one terminal `Exclude` action. Used by
/// both helpers below as the per-rule predicate.
inline bool ruleIsExclude(const WindowRule& rule)
{
    for (const RuleAction& action : rule.actions) {
        if (action.type == QString(ActionType::Exclude)) {
            return true;
        }
    }
    return false;
}

/// Walk @p source and return a derived `WindowRuleSet` containing only the
/// rules with at least one `Exclude` action. Rule ids, priorities and
/// matches are copied verbatim — the derived set is bound to a snapping /
/// effect-side `RuleEvaluator` and exposed as the "is this window
/// excluded?" probe, so it has to preserve the source rule's resolution
/// semantics exactly. An all-non-Exclude input yields an empty set so
/// callers keep a `!set.isEmpty()` fast path.
inline WindowRuleSet excludeRulesFrom(const WindowRuleSet& source)
{
    QList<WindowRule> kept;
    kept.reserve(source.count());
    for (const WindowRule& rule : source.rules()) {
        if (ruleIsExclude(rule)) {
            kept.append(rule);
        }
    }
    WindowRuleSet derived;
    derived.setRules(kept);
    return derived;
}

/// Return the AppId pattern of every `AppId AppIdMatches <pattern>` leaf
/// that lives on an `Exclude`-action rule in @p source. Mirrors what the
/// legacy `ExclusionListBridge::toDaemonRuleSet` used to feed into the
/// snap-engine's `m_exclusionCacheAppsKey` / `m_exclusionCacheClassesKey`
/// trackers, so a consumer that still needs a flat string list of patterns
/// (the pending-restore prune) can derive one from the unified store.
///
/// Only the simple shape "single AppId AppIdMatches leaf" is recognised —
/// the v4 migration produces exactly that shape, and a hand-authored
/// composite rule would not have a single canonical pattern to harvest.
/// Composite rules are silently skipped; the caller's existing semantics
/// were that the pattern list could only express bare-leaf rules anyway.
inline QStringList applicationExcludePatternsFrom(const WindowRuleSet& source)
{
    QStringList patterns;
    for (const WindowRule& rule : source.rules()) {
        if (!ruleIsExclude(rule)) {
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

} // namespace ExcludeRuleFilter

} // namespace PhosphorWindowRule
