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

namespace detail {

/// Fixed v5-UUID namespace for context-rule identities. Deriving each rule's
/// id deterministically from its source identity makes the migration
/// idempotent: converting the same context input twice yields rule sets that
/// compare equal, so `WindowRuleStore::setAllRules` keeps its no-op fast path.
inline const QUuid& namespaceUuid()
{
    static const QUuid ns(QStringLiteral("{c4e3d2b1-8a5f-6071-9cad-1e2f3a4b5c6d}"));
    return ns;
}

/// Stable per-rule key for the v5-UUID derivation. @p family distinguishes the
/// rule kinds that share a (screen, desktop, activity) tuple — an assignment
/// rule and a disable rule for the same context must not collide on id.
inline QString contextIdentityKey(QLatin1StringView family, const QString& screenId, int virtualDesktop,
                                  const QString& activity)
{
    return QString(family) + QLatin1Char('|') + screenId + QLatin1Char('|') + QString::number(virtualDesktop)
        + QLatin1Char('|') + activity;
}

} // namespace detail

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
    // Deterministic id derived from the source context identity — identical
    // assignments yield identical rules, keeping the migration idempotent.
    rule.id = QUuid::createUuidV5(
        detail::namespaceUuid(),
        detail::contextIdentityKey(QLatin1StringView("assignment"), screenId, virtualDesktop, activity));
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
    // The provider default is the single catch-all assignment — its identity
    // is fixed (no pinned dimensions), so the v5 key carries only the family.
    rule.id =
        QUuid::createUuidV5(detail::namespaceUuid(),
                            detail::contextIdentityKey(QLatin1StringView("provider-default"), QString(), 0, QString()));
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
    // Deterministic id — a disable rule's identity is its context tuple plus
    // which engine it disables (snapping/autotile disables for the same
    // context are distinct rules and must not collide).
    rule.id = QUuid::createUuidV5(detail::namespaceUuid(),
                                  detail::contextIdentityKey(autotileMode ? QLatin1StringView("disable-autotile")
                                                                          : QLatin1StringView("disable-snapping"),
                                                             screenId, virtualDesktop, activity));
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
 * read, so a mixed *flat* rule still yields its context projection.
 *
 * **Contract** — this is strictly the inverse of @ref makeContextMatch, so it
 * only understands the shapes that helper produces: an empty catch-all, a
 * single context-equality leaf, or a flat `All{}` whose children are
 * context-equality leaves. It walks **one level** of children; a nested
 * composite (`All{ Any{...} }`) is not a context rule this bridge authored
 * and decomposing it would be silently lossy. Such an input is left as
 * all-defaults — callers that build their own deeper match expressions must
 * not route them through this readback.
 */
inline void contextDimsOf(const MatchExpression& match, QString& screenId, int& virtualDesktop, QString& activity)
{
    screenId.clear();
    virtualDesktop = 0;
    activity.clear();
    if (match.isCatchAll()) {
        return;
    }
    // Only a leaf or a flat All{} is a context rule this bridge produced. A
    // composite of any other kind (Any / None) — or an All{} carrying a
    // nested composite child — is outside the contract; leave the defaults.
    if (!match.isLeaf() && match.kind() != MatchExpression::Kind::All) {
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
        // A non-context or non-equality leaf is ignored — a flat mixed rule
        // still yields its context projection, per the contract above.
    }
}

/**
 * @brief If @p rule is a per-mode disable rule, return whether it disables
 *        autotile (`true`) or snapping (`false`); otherwise nullopt.
 *
 * A rule qualifies iff **exactly one** of its actions is a `DisableEngine`
 * action *and* that action's `mode` param is a recognised token
 * ("snapping" / "autotile"). The exactly-one rule is enforced: a second
 * `DisableEngine` action makes the rule ambiguous — it is not a
 * bridge-authored disable rule — so the function bails to nullopt. An
 * unrecognised `mode` token is likewise rejected rather than silently
 * coerced to "snapping".
 */
inline std::optional<bool> disableRuleAutotileMode(const WindowRule& rule)
{
    const RuleAction* disableAction = nullptr;
    for (const RuleAction& action : rule.actions) {
        if (action.type != QString(ActionType::DisableEngine)) {
            continue;
        }
        if (disableAction != nullptr) {
            // A second DisableEngine action — the rule is ambiguous and was
            // not produced by makeDisableRule. Not a per-mode disable rule.
            return std::nullopt;
        }
        disableAction = &action;
    }
    if (disableAction == nullptr) {
        return std::nullopt;
    }
    const QString mode = disableAction->params.value(QLatin1String("mode")).toString();
    if (mode == QLatin1String("autotile")) {
        return true;
    }
    if (mode == QLatin1String("snapping")) {
        return false;
    }
    // An unrecognised mode token — reject rather than defaulting.
    return std::nullopt;
}

} // namespace ContextRuleBridge

} // namespace PhosphorWindowRule
