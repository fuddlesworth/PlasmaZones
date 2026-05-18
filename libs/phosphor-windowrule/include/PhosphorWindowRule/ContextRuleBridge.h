// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "WindowRule.h"

/**
 * @file ContextRuleBridge.h
 * @brief Header-only bridge: zone Assignments / per-mode disable entries →
 *        context-only `WindowRule`s.
 *
 * Zone Assignments and per-mode disable lists carry **no window-property
 * fields** — they answer "what does this screen/desktop/activity context
 * use?". They become context-only `WindowRule`s whose match expression
 * references only `ScreenId` / `VirtualDesktop` / `Activity`.
 *
 * The deterministic Assignment cascade (exact → activity → desktop → screen →
 * provider default) is reproduced by `priority` ordering. The formula:
 *
 * @code
 *   priority = 300
 *            + (activityPinned ? 200 : 0)
 *            + (desktopPinned  ? 100 : 0)
 *            + (screenPinned   ?  10 : 0)
 * @endcode
 *
 * | Cascade level                   | priority |
 * |---------------------------------|----------|
 * | Exact (screen+desktop+activity) |   610    |
 * | Screen + activity               |   510    |
 * | Screen + desktop                |   410    |
 * | Screen only (display default)   |   310    |
 * | Provider default (catch-all)    |     0    |
 *
 * "Activity beats desktop" is structurally guaranteed because the activity
 * weight (200) exceeds the desktop weight (100). The provider default is an
 * empty-`All{}` catch-all rule at priority 0.
 *
 * Connector-name and virtual-screen fallback are **NOT** priority bands —
 * they are recursive key rewrites handled query-side, not here.
 *
 * Header-only: a consumer that includes this bridge takes only an
 * include-time dependency; there is no link edge and no dependency cycle.
 */

namespace PhosphorWindowRule {

namespace ContextRuleBridge {

/// The base priority of any pinned context rule. A rule that pins at least
/// the screen sits at @c kBasePriority + the per-dimension weights below.
inline constexpr int kBasePriority = 300;
inline constexpr int kActivityWeight = 200;
inline constexpr int kDesktopWeight = 100;
inline constexpr int kScreenWeight = 10;
/// Provider-default catch-all rules carry priority 0 — strictly below every
/// pinned context rule.
inline constexpr int kProviderDefaultPriority = 0;

/**
 * @brief Compute the cascade priority for a context rule.
 *
 * @param screenPinned   true if the rule pins a non-empty screenId.
 * @param desktopPinned  true if the rule pins a virtualDesktop > 0.
 * @param activityPinned true if the rule pins a non-empty activity.
 *
 * A rule that pins nothing (the provider default) returns
 * @c kProviderDefaultPriority (0).
 */
inline int contextPriority(bool screenPinned, bool desktopPinned, bool activityPinned)
{
    if (!screenPinned && !desktopPinned && !activityPinned) {
        return kProviderDefaultPriority;
    }
    return kBasePriority + (activityPinned ? kActivityWeight : 0) + (desktopPinned ? kDesktopWeight : 0)
        + (screenPinned ? kScreenWeight : 0);
}

/**
 * @brief Build the context-only `MatchExpression` for a (screen, desktop,
 *        activity) tuple.
 *
 * Pins only the non-default dimensions: a non-empty @p screenId, a
 * @p virtualDesktop > 0, a non-empty @p activity. A tuple that pins nothing
 * yields the empty-`All{}` catch-all (the provider default's match).
 */
inline MatchExpression makeContextMatch(const QString& screenId, int virtualDesktop, const QString& activity)
{
    QList<MatchExpression> children;
    if (!screenId.isEmpty()) {
        children.append(MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, screenId));
    }
    if (virtualDesktop > 0) {
        children.append(MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, virtualDesktop));
    }
    if (!activity.isEmpty()) {
        children.append(MatchExpression::makeLeaf(Field::Activity, Operator::Equals, activity));
    }
    if (children.isEmpty()) {
        return MatchExpression(); // empty All{} — the catch-all
    }
    if (children.size() == 1) {
        return children.first();
    }
    return MatchExpression::makeAll(children);
}

/**
 * @brief Build the lossless action list for a migrated zone Assignment.
 *
 * The `AssignmentEntry` invariant is mode-toggle losslessness: it stores both
 * `snappingLayout` and `tilingAlgorithm` so flipping mode never drops the
 * other field. The migrated rule therefore carries up to **three** actions:
 *
 *   - `SetEngineMode` — always (the mode token, "snapping" or "autotile").
 *   - `SetSnappingLayout` — only when @p snappingLayout is non-empty (the
 *     action descriptor rejects an empty `layoutId`).
 *   - `SetTilingAlgorithm` — only when @p tilingAlgorithm is non-empty (the
 *     descriptor rejects an empty `algorithm`).
 *
 * A mode-only entry (both layout fields empty — the KCM "autotile, default
 * algorithm" shape) yields a single `SetEngineMode` action; the mode token
 * alone preserves the user's intent.
 *
 * @param autotileMode true → mode token "autotile"; false → "snapping".
 */
inline QList<RuleAction> makeAssignmentActions(bool autotileMode, const QString& snappingLayout,
                                               const QString& tilingAlgorithm)
{
    QList<RuleAction> actions;

    RuleAction modeAction;
    modeAction.type = QString(ActionType::SetEngineMode);
    modeAction.params.insert(QLatin1String("mode"),
                             autotileMode ? QLatin1String("autotile") : QLatin1String("snapping"));
    actions.append(modeAction);

    if (!snappingLayout.isEmpty()) {
        RuleAction layoutAction;
        layoutAction.type = QString(ActionType::SetSnappingLayout);
        layoutAction.params.insert(QLatin1String("layoutId"), snappingLayout);
        actions.append(layoutAction);
    }
    if (!tilingAlgorithm.isEmpty()) {
        RuleAction tilingAction;
        tilingAction.type = QString(ActionType::SetTilingAlgorithm);
        tilingAction.params.insert(QLatin1String("algorithm"), tilingAlgorithm);
        actions.append(tilingAction);
    }
    return actions;
}

/**
 * @brief Build a complete migrated zone-Assignment `WindowRule`.
 *
 * The match is context-only; the priority follows the cascade formula; the
 * actions are the lossless three-action set. @p name is a human-readable
 * label for the settings UI.
 */
inline WindowRule makeAssignmentRule(const QString& name, const QString& screenId, int virtualDesktop,
                                     const QString& activity, bool autotileMode, const QString& snappingLayout,
                                     const QString& tilingAlgorithm)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.enabled = true;
    rule.priority = contextPriority(!screenId.isEmpty(), virtualDesktop > 0, !activity.isEmpty());
    rule.match = makeContextMatch(screenId, virtualDesktop, activity);
    rule.actions = makeAssignmentActions(autotileMode, snappingLayout, tilingAlgorithm);
    return rule;
}

/**
 * @brief Build the provider-default catch-all `WindowRule`.
 *
 * An empty-`All{}` match at priority 0 — strictly the lowest. It carries the
 * global default mode/layout the cascade falls through to when no pinned
 * context rule matches.
 */
inline WindowRule makeProviderDefaultRule(const QString& name, bool autotileMode, const QString& snappingLayout,
                                          const QString& tilingAlgorithm)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.enabled = true;
    rule.priority = kProviderDefaultPriority;
    rule.match = MatchExpression(); // empty All{} catch-all
    rule.actions = makeAssignmentActions(autotileMode, snappingLayout, tilingAlgorithm);
    return rule;
}

/**
 * @brief Build a `DisableEngine` context rule for a per-mode disable entry.
 *
 * Per-mode disable lists ("snapping is off on monitor X") become context
 * rules carrying a single `DisableEngine` action. The `mode` param records
 * which engine the rule disables ("snapping" / "autotile") so the evaluator
 * can scope the gate.
 *
 * The disable rule shares the cascade priority formula with assignment rules
 * — a desktop-scoped disable outranks a monitor-scoped one, mirroring the
 * `contextDisabledReason` monitor > desktop > activity precedence when read
 * the other way (a more specific disable wins).
 */
inline WindowRule makeDisableRule(const QString& name, const QString& screenId, int virtualDesktop,
                                  const QString& activity, bool autotileMode)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.enabled = true;
    rule.priority = contextPriority(!screenId.isEmpty(), virtualDesktop > 0, !activity.isEmpty());
    rule.match = makeContextMatch(screenId, virtualDesktop, activity);

    RuleAction action;
    action.type = QString(ActionType::DisableEngine);
    action.params.insert(QLatin1String("mode"), autotileMode ? QLatin1String("autotile") : QLatin1String("snapping"));
    rule.actions.append(action);
    return rule;
}

// ── Readback: context rule → (screen, desktop, activity) ───────────────────
//
// The inverse of @ref makeContextMatch — decompose a context rule's match
// expression back into the (screenId, virtualDesktop, activity) tuple it
// pins. Used by consumers that need to enumerate stored context rules
// (the per-mode disable lists in Settings, the assignment introspection
// helpers in LayoutRegistry).

/// True if @p expr is an exact `Field == value` equality leaf for @p field.
inline bool isContextEqualsLeaf(const MatchExpression& expr, Field field)
{
    return expr.isLeaf() && expr.predicate().field == field && expr.predicate().op == Operator::Equals;
}

/**
 * @brief Decompose a context rule's match into its pinned dimensions.
 *
 * @p screenId / @p virtualDesktop / @p activity are reset, then filled from
 * whichever `ScreenId` / `VirtualDesktop` / `Activity` equality leaves the
 * match carries. An empty catch-all leaves all three at their defaults.
 * Window-property leaves (AppId etc.) are ignored — only context fields are
 * read, so a mixed rule still yields its context projection.
 */
inline void contextDimsOf(const MatchExpression& match, QString& screenId, int& virtualDesktop, QString& activity)
{
    screenId.clear();
    virtualDesktop = 0;
    activity.clear();
    if (match.isCatchAll()) {
        return;
    }
    const QList<MatchExpression> leaves = match.isLeaf() ? QList<MatchExpression>{match} : match.children();
    for (const MatchExpression& leaf : leaves) {
        if (isContextEqualsLeaf(leaf, Field::ScreenId)) {
            screenId = leaf.predicate().value.toString();
        } else if (isContextEqualsLeaf(leaf, Field::VirtualDesktop)) {
            virtualDesktop = leaf.predicate().value.toInt();
        } else if (isContextEqualsLeaf(leaf, Field::Activity)) {
            activity = leaf.predicate().value.toString();
        }
    }
}

/**
 * @brief If @p rule carries a single `DisableEngine` action, return the mode
 *        token ("snapping" / "autotile") it disables; otherwise nullopt.
 *
 * A rule is a per-mode disable rule iff exactly one of its actions is a
 * `DisableEngine` action; the `mode` param scopes which engine it gates.
 */
inline std::optional<bool> disableRuleAutotileMode(const WindowRule& rule)
{
    for (const RuleAction& action : rule.actions) {
        if (action.type == QString(ActionType::DisableEngine)) {
            return action.params.value(QLatin1String("mode")).toString() == QLatin1String("autotile");
        }
    }
    return std::nullopt;
}

} // namespace ContextRuleBridge

} // namespace PhosphorWindowRule
