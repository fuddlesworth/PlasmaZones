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

/// Builds one `Exclude` rule for a single @p field / @p pattern pair. The
/// caller guarantees @p pattern is non-empty.
inline WindowRule makeExclusionRule(Field field, const QString& pattern)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
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

    for (const QString& pattern : excludedApplications) {
        if (pattern.isEmpty()) {
            continue;
        }
        rules.append(makeExclusionRule(Field::DesktopFile, pattern));
    }
    for (const QString& pattern : excludedWindowClasses) {
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
            rule.id = QUuid::createUuid();
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
