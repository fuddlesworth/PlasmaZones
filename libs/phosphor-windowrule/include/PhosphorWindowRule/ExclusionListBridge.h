// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "WindowRule.h"
#include "WindowRuleSet.h"

/**
 * @file ExclusionListBridge.h
 * @brief Header-only in-memory bridge: legacy exclusion `QStringList`s →
 *        `WindowRuleSet`.
 *
 * The effect's `matchesExclusionLists()` checks two flat string lists — an
 * "applications" list matched (case-insensitively, substring) against the
 * window's `desktopFileName()`, and a "window classes" list matched the same
 * way against `windowClass()`. This bridge converts those two lists into an
 * equivalent `WindowRuleSet`, so the same evaluation pipeline that resolves
 * the animation App Rules can also drive exclusion filtering.
 *
 * Behaviour parity with `matchesExclusionLists()`:
 *   - applications → a leaf `DesktopFile Contains <pattern>` predicate,
 *   - window classes → a leaf `WindowClass Contains <pattern>` predicate,
 *   - `Contains` is case-insensitive (the `MatchExpression` contract),
 *   - empty patterns are dropped (a `Contains ""` predicate would otherwise
 *     match every window; the evaluator also rejects it, but dropping it here
 *     keeps the rule set canonical and the count meaningful).
 *
 * Each surviving pattern becomes one `WindowRule` carrying a single, terminal
 * `Exclude` action. Priority is irrelevant for a pure pass/fail check — every
 * rule shares priority 0 — because `Exclude` is terminal: the first match
 * stops the walk and marks the window unmanaged regardless of order.
 *
 * Header-only so `phosphor-windowrule` takes only an include-time dependency
 * from consumers that opt in to the bridge — there is no link edge and no
 * cycle. This deliberately mirrors the effect-side `Contains` semantics; the
 * daemon's segment-aware `appIdMatches` exclusion semantics are a separate
 * concern reconciled in a later phase.
 */

namespace PhosphorWindowRule {

namespace ExclusionListBridge {

namespace detail {

/// Fixed v5-UUID namespace for exclusion-rule identities. Deriving each rule's
/// id deterministically from its source identity (field + operator + pattern)
/// makes the conversion idempotent: converting the same exclusion lists twice
/// yields rule sets that compare equal, so `WindowRuleStore::setAllRules`
/// keeps its no-op fast path.
inline const QUuid& namespaceUuid()
{
    static const QUuid ns(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
    return ns;
}

/// Stable per-rule key. @p field and @p op disambiguate the rule families:
/// the same pattern can produce a `DesktopFile Contains` rule, a
/// `WindowClass Contains` rule (toRuleSet) and an `AppId AppIdMatches` rule
/// (toDaemonRuleSet) — all three must carry distinct ids.
inline QString exclusionIdentityKey(Field field, Operator op, const QString& pattern)
{
    return QString::number(static_cast<int>(field)) + QLatin1Char('|') + QString::number(static_cast<int>(op))
        + QLatin1Char('|') + pattern;
}

} // namespace detail

/// Builds one `Exclude` rule for a single @p field / @p pattern pair. The
/// caller guarantees @p pattern is non-empty.
inline WindowRule makeExclusionRule(Field field, const QString& pattern)
{
    WindowRule rule;
    // Deterministic id — identical (field, pattern) inputs yield identical
    // rules, keeping the conversion idempotent.
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
 * Empty / whitespace-only patterns are dropped. The returned set carries one
 * terminal `Exclude` rule per surviving pattern. An all-empty input yields an
 * empty set — letting callers keep a `!ruleSet.isEmpty()` fast path so a
 * no-exclusions user pays nothing.
 */
inline WindowRuleSet toRuleSet(const QStringList& excludedApplications, const QStringList& excludedWindowClasses)
{
    QList<WindowRule> rules;
    rules.reserve(excludedApplications.size() + excludedWindowClasses.size());

    // Whitespace-only patterns are dropped (not just exact-empty ones) — the
    // doc and the sibling toDaemonRuleSet() promise this, and a " " pattern
    // would otherwise become a substring rule that matches almost nothing
    // while still bloating the canonical rule count.
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

/**
 * @brief Convert the daemon's exclusion lists into a `WindowRuleSet`.
 *
 * The daemon's own exclusion enforcement (`SnapEngine`'s navigation gates and
 * the auto-snap lifecycle path) historically matched **both** the excluded-
 * applications and the excluded-window-classes list against the window's
 * resolved `appId` using the segment-aware reverse-DNS `appIdMatches()` —
 * unlike the effect's substring `Contains` semantics (@ref toRuleSet).
 *
 * This builder reproduces the daemon flavour: every surviving pattern becomes
 * one terminal `Exclude` rule with an `AppId AppIdMatches <pattern>` leaf, so
 * the same `RuleEvaluator` pipeline replaces the hand-rolled `appIdMatches`
 * loops. Both input lists feed `AppId` rules because both were always matched
 * against `appId` — the app/class split was a UI grouping, not a match-field
 * distinction.
 *
 * Empty / whitespace-only patterns are dropped. An all-empty input yields an
 * empty set so callers keep a `!ruleSet.isEmpty()` fast path.
 */
inline WindowRuleSet toDaemonRuleSet(const QStringList& excludedApplications, const QStringList& excludedWindowClasses)
{
    QList<WindowRule> rules;
    rules.reserve(excludedApplications.size() + excludedWindowClasses.size());

    const auto append = [&rules](const QStringList& patterns) {
        for (const QString& raw : patterns) {
            const QString pattern = raw.trimmed();
            if (pattern.isEmpty()) {
                continue;
            }
            WindowRule rule;
            // Deterministic id — identical patterns yield identical rules,
            // keeping the conversion idempotent.
            rule.id = QUuid::createUuidV5(detail::namespaceUuid(),
                                          detail::exclusionIdentityKey(Field::AppId, Operator::AppIdMatches, pattern));
            rule.enabled = true;
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern);
            RuleAction action;
            action.type = QString(ActionType::Exclude);
            rule.actions.append(action);
            rules.append(rule);
        }
    };
    append(excludedApplications);
    append(excludedWindowClasses);

    WindowRuleSet set;
    set.setRules(rules);
    return set;
}

} // namespace ExclusionListBridge

} // namespace PhosphorWindowRule
