// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IdentityKey.h"
#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "WindowRule.h"
#include "WindowRuleSet.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

/**
 * @file ExclusionRules.h
 * @brief Header-only helpers for the two directions of `Exclude`-action
 *        WindowRule conversion:
 *          - **build** an Exclude-action rule from a string pattern
 *            (legacy `(applications, windowClasses)` settings lists →
 *            `WindowRuleSet`),
 *          - **filter** an existing rule set down to its Exclude-shaped
 *            slice (unified `WindowRuleStore` → snap-engine / effect
 *            drag-gate exclusion evaluators).
 *
 * Replaces the earlier split into `ExclusionListBridge.h` (lists → rules,
 * used by the animation-page exclusion path and the daemon-side snap
 * navigation gate) and `ExcludeRuleFilter.h` (rules → exclude-only slice,
 * added when the snap-engine and effect moved off the lists onto the
 * unified rule store). Both files boiled down to "build / probe Exclude
 * rules"; folding them into one keeps the discovery surface honest —
 * future readers see ONE place to look, not two with overlapping intent.
 *
 * After the v4 exclusion-list fold (configmigration.cpp), the
 * snapping-page lists are gone from `ISettings`. The list-input helpers
 * here survive only for the animation-page's separate
 * `animationExcludedApplications` / `animationExcludedWindowClasses`
 * lists, which haven't migrated; once those move to unified rules too the
 * list-input helpers retire.
 *
 * Header-only so consumers take only an include-time dependency on
 * phosphor-windowrule — there is no link edge added by using these
 * helpers.
 */

namespace PhosphorWindowRule {

namespace ExclusionRules {

namespace detail {

/// Fixed v5-UUID namespace for derived exclusion-rule identities. Deriving
/// each rule's id deterministically from its source identity (field +
/// operator + pattern) makes the conversion idempotent: converting the
/// same exclusion lists twice yields rule sets that compare equal, so
/// `WindowRuleStore::setAllRules` keeps its no-op fast path. The constant
/// is BYTE-IDENTICAL to the one the v4 migration uses in configmigration.cpp
/// so a rule built by `makeExclusionRule` here and one written by the
/// migration there collide cleanly on the same pattern.
inline const QUuid& namespaceUuid()
{
    static const QUuid ns(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
    return ns;
}

/// Length-prefix a segment so concatenated identity keys are unambiguous.
/// Re-exported from the shared @ref IdentityKey.h.
using PhosphorWindowRule::Detail::encodeSegment;

/// Stable per-rule key. @p field and @p op disambiguate the rule families:
/// the same pattern can produce a `DesktopFile Contains` rule (the
/// animation-page bridge) or an `AppId AppIdMatches` rule (the daemon-side
/// migration) — both must carry distinct ids. Segments are length-prefixed
/// so no two distinct tuples can collide.
inline QString exclusionIdentityKey(Field field, Operator op, const QString& pattern)
{
    return encodeSegment(QString::number(static_cast<int>(field)))
        + encodeSegment(QString::number(static_cast<int>(op))) + encodeSegment(pattern);
}

} // namespace detail

/// Build one terminal `Exclude` rule for @p field / @p pattern. The caller
/// guarantees @p pattern is non-empty. The id is deterministic via UUIDv5
/// so identical (field, pattern) tuples collapse to the same rule.
inline WindowRule makeExclusionRule(Field field, const QString& pattern)
{
    WindowRule rule;
    rule.id =
        QUuid::createUuidV5(detail::namespaceUuid(), detail::exclusionIdentityKey(field, Operator::Contains, pattern));
    rule.enabled = true;
    rule.priority = 0;
    rule.match = MatchExpression::makeLeaf(field, Operator::Contains, pattern);

    RuleAction action;
    action.type = QString(ActionType::Exclude);
    rule.actions.append(action);
    return rule;
}

/**
 * @brief Convert a pair of exclusion lists into a `WindowRuleSet`.
 *
 * @param excludedApplications  patterns matched against `desktopFile`.
 * @param excludedWindowClasses patterns matched against `windowClass`.
 *
 * Empty / whitespace-only patterns are dropped. Each surviving pattern
 * becomes one terminal `Exclude` rule with a `Contains` leaf — the
 * legacy effect-side semantics (substring, case-insensitive via the
 * MatchExpression contract). An all-empty input yields an empty set so
 * callers keep a `!ruleSet.isEmpty()` fast path.
 *
 * Currently used only by the animation-page exclusion path; the snapping
 * counterpart migrated to unified WindowRules in v4. Retires once
 * animationExcludedApplications / animationExcludedWindowClasses migrate
 * the same way.
 */
inline WindowRuleSet toRuleSet(const QStringList& excludedApplications, const QStringList& excludedWindowClasses)
{
    QList<WindowRule> rules;
    rules.reserve(excludedApplications.size() + excludedWindowClasses.size());

    // Whitespace-only patterns are dropped (not just exact-empty ones): a
    // " " pattern would otherwise become a substring rule that matches
    // almost nothing while still bloating the canonical rule count.
    for (const QString& raw : excludedApplications) {
        const QString pattern = raw.trimmed();
        if (pattern.isEmpty()) {
            continue;
        }
        rules.append(makeExclusionRule(Field::DesktopFile, pattern));
    }
    for (const QString& raw : excludedWindowClasses) {
        const QString pattern = raw.trimmed();
        if (pattern.isEmpty()) {
            continue;
        }
        rules.append(makeExclusionRule(Field::WindowClass, pattern));
    }

    WindowRuleSet set;
    set.setRules(rules);
    return set;
}

/// True iff @p rule carries at least one terminal `Exclude` action. Used
/// internally and exposed for callers that want the same predicate
/// without rebuilding it inline.
inline bool ruleIsExclude(const WindowRule& rule)
{
    for (const RuleAction& action : rule.actions) {
        if (action.type == QString(ActionType::Exclude)) {
            return true;
        }
    }
    return false;
}

/// Walk @p source and return a derived `WindowRuleSet` containing only
/// the rules with at least one `Exclude` action. Rule ids, priorities and
/// matches are copied verbatim — the derived set gets bound to a
/// snapping- / effect-side `RuleEvaluator` and exposed as the "is this
/// window excluded?" probe, so it has to preserve the source rule's
/// resolution semantics exactly. An all-non-Exclude input yields an empty
/// set so callers keep a `!set.isEmpty()` fast path.
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
