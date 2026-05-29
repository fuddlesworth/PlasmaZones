// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "WindowRule.h"
#include "WindowRuleSet.h"

#include <QList>
#include <QString>
#include <QStringList>

/**
 * @file ExclusionRules.h
 * @brief Header-only helpers that slice a unified `WindowRuleSet` down to
 *        the `Exclude`- or `ExcludeAnimations`-action rules that the
 *        snap-engine, the KWin effect's drag gate, and the effect's
 *        `shouldAnimateWindow` gate respectively bind to their
 *        evaluators.
 *
 * After v4 (configmigration.cpp), exclusion rules live exclusively in
 * the unified WindowRule store — the legacy
 * `(animation)excludedApplications` / `(animation)excludedWindowClasses`
 * QStringList settings retired along with the bridge that derived rules
 * from them. Consumers ask THIS header "give me the Exclude- or
 * ExcludeAnimations-shaped slice of the user's unified rule store".
 *
 * Header-only so consumers take only an include-time dependency on
 * phosphor-windowrule — there is no link edge added by using these
 * helpers.
 */

namespace PhosphorWindowRule {

namespace ExclusionRules {

/// True iff @p rule carries at least one action whose `type` matches
/// @p actionType. The two `*RulesFrom` helpers below build on this; it
/// is exposed so callers that want the same predicate without
/// re-deriving a slice get the canonical shape.
inline bool ruleHasAction(const WindowRule& rule, QLatin1StringView actionType)
{
    for (const RuleAction& action : rule.actions) {
        if (action.type == QString(actionType)) {
            return true;
        }
    }
    return false;
}

/// True iff @p rule carries at least one terminal `Exclude` action.
/// Thin alias over @ref ruleHasAction for the snapping / drag-gate
/// call sites that don't need to spell the action type explicitly.
inline bool ruleIsExclude(const WindowRule& rule)
{
    return ruleHasAction(rule, ActionType::Exclude);
}

/// Walk @p source and return a derived `WindowRuleSet` containing only
/// the rules whose action list includes @p actionType. Rule ids,
/// priorities and matches are copied verbatim — the derived set gets
/// bound to a `RuleEvaluator` downstream and has to preserve the
/// source rule's resolution semantics exactly. An empty match yields
/// an empty set so callers keep a `!set.isEmpty()` fast path.
inline WindowRuleSet rulesWithAction(const WindowRuleSet& source, QLatin1StringView actionType)
{
    QList<WindowRule> kept;
    kept.reserve(source.count());
    for (const WindowRule& rule : source.rules()) {
        if (ruleHasAction(rule, actionType)) {
            kept.append(rule);
        }
    }
    WindowRuleSet derived;
    derived.setRules(kept);
    return derived;
}

/// Slice @p source down to rules with a terminal `Exclude` action.
/// Used by SnapEngine and the KWin effect's drag gate.
inline WindowRuleSet excludeRulesFrom(const WindowRuleSet& source)
{
    return rulesWithAction(source, ActionType::Exclude);
}

/// Slice @p source down to rules with a terminal `ExcludeAnimations`
/// action — the action the v4 fold introduced for the legacy
/// animationExcludedApplications / animationExcludedWindowClasses
/// lists. Used by the KWin effect's `shouldAnimateWindow` gate to
/// suppress animation overrides on matched windows.
inline WindowRuleSet excludeAnimationsRulesFrom(const WindowRuleSet& source)
{
    return rulesWithAction(source, ActionType::ExcludeAnimations);
}

/// Return the AppId pattern of every `AppId AppIdMatches <pattern>` leaf
/// that lives on an `Exclude`-action rule in @p source. Mirrors what the
/// legacy bridge's daemon-flavour builder used to feed into the
/// snap-engine's exclusion cache, so a consumer that still needs a flat
/// string list of patterns (the WTA pending-restore prune) can derive one
/// from the unified store.
///
/// Only the simple shape "single AppId AppIdMatches leaf" is recognised
/// — the v4 migration produces exactly that shape, and a hand-authored
/// composite rule has no single canonical pattern to harvest. Composite
/// rules are silently skipped; the caller's existing semantics were that
/// the pattern list could only express bare-leaf rules anyway.
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

} // namespace ExclusionRules

} // namespace PhosphorWindowRule
