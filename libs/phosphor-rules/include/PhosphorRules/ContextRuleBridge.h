// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QSet>
#include <QString>
#include <QUuid>

#include <optional>

#include "IdentityKey.h"
#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "Rule.h"
#include "RuleLogging.h"
#include "phosphorrules_export.h"

/**
 * @file ContextRuleBridge.h
 * @brief Header-only bridge: zone Assignments / per-mode disable entries →
 *        context-only `Rule`s.
 *
 * Zone Assignments and per-mode disable lists carry **no window-property
 * fields** — they answer "what does this screen/desktop/activity context
 * use?". They become context-only `Rule`s whose match expression
 * references only `ScreenId` / `VirtualDesktop` / `Activity`.
 *
 * Precedence is plain `priority`: the highest-priority matching rule wins per
 * action slot, ties broken by list order. There is no specificity formula and
 * no provider-default rule. The caller supplies each rule's `priority`
 * explicitly — context rules seed in the @ref kContextBandBase band and the
 * Settings list-order renormalizer / runtime upsert derive the rest.
 *
 * Header-only: a consumer that includes this bridge takes only an
 * include-time dependency; there is no link edge and no dependency cycle.
 */

namespace PhosphorRules {

namespace ContextRuleBridge {

/// Default precedence band base for context rules (zone assignments and
/// per-mode disables). With specificity removed, `priority` is the only
/// precedence value: callers seed new context rules in this band and the
/// Settings list-order renormalizer / runtime upsert derive concrete values
/// from it. The band is one source of truth shared with the Settings page.
inline constexpr int kContextBandBase = 300;

/**
 * @brief The cascade axis a context tuple resolves to.
 *
 * The four shapes the context bridge produces — `Combined` is the
 * `screen + desktop + activity` exact pin, `Activity` is `screen + activity`,
 * `Desktop` is `screen + desktop`, `Monitor` is `screen-only`. A catch-all or
 * a tuple with an empty `screenId` returns @c CatchAll and is not a pinned
 * axis. Callers that need to compare rule families (per-mode disable lists,
 * the per-screen / per-desktop / per-activity batch setters) discriminate on
 * this enum so the formula lives in one place.
 */
enum class ContextAxis {
    CatchAll, ///< empty `screenId` (the provider-default catch-all), OR a
              ///< context match that pins a non-dimension field beyond
              ///< screen/desktop/activity (e.g. TiledWindowCount) and so is not
              ///< an exact-context assignment (see contextAxisFor).
    Monitor, ///< screen only.
    Desktop, ///< screen + desktop.
    Activity, ///< screen + activity.
    Combined, ///< screen + desktop + activity (the exact-pin shape).
};

/**
 * @brief Classify a (screen, desktop, activity) tuple into its cascade axis.
 *
 * The classifier keys only on which of the three dimensions are present — it
 * does NOT distinguish empty-vs-missing in the dimensional sense. The axes
 * remain the batch reader/writer families; they no longer drive any priority.
 */
inline ContextAxis contextAxisOf(const QString& screenId, int virtualDesktop, const QString& activity)
{
    if (screenId.isEmpty()) {
        return ContextAxis::CatchAll;
    }
    const bool hasDesktop = virtualDesktop > 0;
    const bool hasActivity = !activity.isEmpty();
    if (hasDesktop && hasActivity) {
        return ContextAxis::Combined;
    }
    if (hasActivity) {
        return ContextAxis::Activity;
    }
    if (hasDesktop) {
        return ContextAxis::Desktop;
    }
    return ContextAxis::Monitor;
}

/**
 * @brief Human-readable label for a context rule's tuple.
 *
 * The single canonical formula for the " · Desktop N" / " · Activity" suffix
 * — every caller that needs to log or display a context-rule identity routes
 * through here so the string shape stays consistent across the daemon,
 * settings UI, and migration code.
 */
inline QString contextRuleName(const QString& screenId, int virtualDesktop, const QString& activity)
{
    return screenId + (virtualDesktop > 0 ? QStringLiteral(" · Desktop ") + QString::number(virtualDesktop) : QString())
        + (activity.isEmpty() ? QString() : QStringLiteral(" · Activity"));
}

namespace detail {

/// Fixed v5-UUID namespace for context-rule identities. Deriving each rule's
/// id deterministically from its source identity makes the migration
/// idempotent: converting the same context input twice yields rule sets that
/// compare equal, so `RuleStore::setAllRules` keeps its no-op fast path.
inline const QUuid& namespaceUuid()
{
    static const QUuid ns(QStringLiteral("{c4e3d2b1-8a5f-6071-9cad-1e2f3a4b5c6d}"));
    return ns;
}

/// Length-prefix a segment so concatenated identity keys are unambiguous: a
/// `screenId` / `activity` may itself contain a `|`, so a plain `|`-joined key
/// could collide for two distinct tuples. `"<len>:<segment>"` makes every
/// tuple's encoding unique. Re-exported from the shared @ref IdentityKey.h so
/// the three bridges share one implementation.
using PhosphorRules::Detail::encodeSegment;

/// Stable per-rule key for the v5-UUID derivation. @p family distinguishes the
/// rule kinds that share a (screen, desktop, activity) tuple — an assignment
/// rule and a disable rule for the same context must not collide on id.
/// Segments are length-prefixed so no two distinct tuples can collide.
inline QString contextIdentityKey(QLatin1StringView family, const QString& screenId, int virtualDesktop,
                                  const QString& activity)
{
    return encodeSegment(QString(family)) + encodeSegment(screenId) + encodeSegment(QString::number(virtualDesktop))
        + encodeSegment(activity);
}

} // namespace detail

/**
 * @brief The deterministic v5 rule id @ref makeAssignmentRule would assign
 *        for a given (screen, desktop, activity) tuple.
 *
 * Exposing the id derivation as a public helper lets callers look up a stored
 * assignment rule by deterministic id derivation rather than evaluating each
 * rule's match shape — `RuleSet::ruleById` itself is still a linear
 * scan, but the per-rule comparison collapses to a single QUuid equality
 * instead of a tuple-shape predicate. Two callers deriving the same tuple
 * necessarily land on the same id, so the lookup stays correct across
 * processes and across persistence round-trips.
 */
inline QUuid assignmentRuleIdFor(const QString& screenId, int virtualDesktop, const QString& activity)
{
    return QUuid::createUuidV5(
        detail::namespaceUuid(),
        detail::contextIdentityKey(QLatin1StringView("assignment"), screenId, virtualDesktop, activity));
}

/**
 * @brief The deterministic v5 rule id @ref makeDisableRule would assign for a
 *        given `(screen, desktop, activity, modeToken)` tuple.
 *
 * The disable rule's id includes the engine the rule disables — different
 * engines' disables for the same context are distinct rules, so the id helper
 * mirrors that distinction. @p modeToken is the wire string for the engine
 * (e.g. @c "snapping" / @c "autotile" / @c "scrolling") — supplying it as a
 * token (not a `bool`) means a future engine extends the identity key namespace
 * without rewriting existing UUIDs (the @c "disable-snapping" and
 * @c "disable-autotile" identity keys are preserved verbatim, so every rule
 * already on disk keeps its id).
 */
inline QUuid disableRuleIdFor(const QString& screenId, int virtualDesktop, const QString& activity,
                              const QString& modeToken)
{
    // contextIdentityKey() takes a QLatin1StringView, but the source bytes
    // are produced via `toUtf8()` so non-Latin1 tokens (a future
    // non-ASCII mode wire string) injectively distinguish their UUIDs —
    // `toLatin1()` replaces every non-Latin1 codepoint with '?', collapsing
    // distinct tokens onto identical UUIDs. For the current ASCII-only
    // vocabulary ("snapping" / "autotile" / "scrolling") UTF-8 and
    // Latin1 produce identical bytes, so existing rule UUIDs are preserved
    // verbatim; only future non-ASCII tokens diverge. The view's source
    // bytes must live for the full call — naming the QByteArray local
    // keeps it from expiring before createUuidV5 reads through the view.
    const QByteArray tag = QByteArray("disable-") + modeToken.toUtf8();
    return QUuid::createUuidV5(
        detail::namespaceUuid(),
        detail::contextIdentityKey(QLatin1StringView(tag.constData(), static_cast<qsizetype>(tag.size())), screenId,
                                   virtualDesktop, activity));
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
 *   - `SetEngineMode` — always (the mode token, e.g. "snapping" / "autotile"
 *     / "scrolling").
 *   - `SetSnappingLayout` — only when @p snappingLayout is non-empty (the
 *     action descriptor rejects an empty `layoutId`).
 *   - `SetTilingAlgorithm` — only when @p tilingAlgorithm is non-empty (the
 *     descriptor rejects an empty `algorithm`).
 *
 * A mode-only entry (both layout fields empty — the KCM "autotile, default
 * algorithm" shape) yields a single `SetEngineMode` action; the mode token
 * alone preserves the user's intent.
 *
 * Open-vocabulary by design: the `SetEngineMode` descriptor's validator
 * checks only that `modeToken` is non-empty — vocabulary validation lives
 * at consumers via `PhosphorZones::modeFromWireString`, NOT at load time.
 * Settings routes `PhosphorZones::Mode → QString` via `modeToWireString`
 * (the authoritative mapping), so a production caller never produces an
 * unrecognised token. An empty token writes a malformed rule that load
 * does reject (the validator's non-empty check fires there). An unknown
 * token like `"bogus"` survives load and is silently inert at consumption:
 * the assignment-side consumer `PhosphorZones::RuleHelpers::
 * entryFromRuleMatchActions` decodes via `modeFromWireString`, which
 * returns `nullopt` for unknown tokens and leaves the entry on its
 * Snapping default. Every token the validator accepts — today
 * `snapping`/`autotile`/`scrolling`, see `engineModeOptions()` in
 * `ruleaction.cpp` — round-trips end-to-end through the consumer.
 */
inline QList<RuleAction> makeAssignmentActions(const QString& modeToken, const QString& snappingLayout,
                                               const QString& tilingAlgorithm)
{
    QList<RuleAction> actions;

    RuleAction modeAction;
    modeAction.type = QString(ActionType::SetEngineMode);
    modeAction.params.insert(ActionParam::Mode, modeToken);
    actions.append(modeAction);

    if (!snappingLayout.isEmpty()) {
        RuleAction layoutAction;
        layoutAction.type = QString(ActionType::SetSnappingLayout);
        layoutAction.params.insert(ActionParam::LayoutId, snappingLayout);
        actions.append(layoutAction);
    }
    if (!tilingAlgorithm.isEmpty()) {
        RuleAction tilingAction;
        tilingAction.type = QString(ActionType::SetTilingAlgorithm);
        tilingAction.params.insert(ActionParam::Algorithm, tilingAlgorithm);
        actions.append(tilingAction);
    }
    return actions;
}

/**
 * @brief Build a complete migrated zone-Assignment `Rule`.
 *
 * The match is context-only; the actions are the lossless three-action set.
 * @p name is a human-readable label for the settings UI. @p modeToken is the
 * wire string for the assignment's mode (see `makeAssignmentActions` for
 * vocabulary contract). @p priority is the rule's precedence verbatim — the
 * caller owns it (highest priority wins per slot, ties by list order).
 */
inline Rule makeAssignmentRule(const QString& name, const QString& screenId, int virtualDesktop,
                               const QString& activity, const QString& modeToken, const QString& snappingLayout,
                               const QString& tilingAlgorithm, int priority)
{
    Rule rule;
    // Deterministic id derived from the source context identity — identical
    // assignments yield identical rules, keeping the migration idempotent.
    rule.id = assignmentRuleIdFor(screenId, virtualDesktop, activity);
    rule.name = name;
    rule.enabled = true;
    rule.priority = priority;
    rule.match = makeContextMatch(screenId, virtualDesktop, activity);
    rule.actions = makeAssignmentActions(modeToken, snappingLayout, tilingAlgorithm);
    return rule;
}

/**
 * @brief Build a `DisableEngine` context rule for a per-mode disable entry.
 *
 * Per-mode disable lists ("snapping is off on monitor X") become context
 * rules carrying a single `DisableEngine` action. @p modeToken records which
 * engine the rule disables (the wire string, e.g. @c "snapping" /
 * @c "autotile" / @c "scrolling") so the evaluator can scope the gate.
 * @p priority is the rule's precedence verbatim (highest priority wins).
 *
 * Validation: this helper trusts the caller to pass a token recognised by the
 * `DisableEngine` descriptor's validator. The Settings layer routes Mode →
 * token through `PhosphorZones::modeToWireString`, so an unrecognised token
 * here is a programmer error and the resulting rule would be rejected at
 * load by `RuleAction::fromJson`. Passing an empty token writes a malformed
 * rule with action params `{mode: ""}` — also caught at load.
 */
inline Rule makeDisableRule(const QString& name, const QString& screenId, int virtualDesktop, const QString& activity,
                            const QString& modeToken, int priority)
{
    Rule rule;
    // Deterministic id — a disable rule's identity is its context tuple plus
    // which engine it disables (per-mode disables for the same context are
    // distinct rules and must not collide).
    rule.id = disableRuleIdFor(screenId, virtualDesktop, activity, modeToken);
    rule.name = name;
    rule.enabled = true;
    rule.priority = priority;
    rule.match = makeContextMatch(screenId, virtualDesktop, activity);

    RuleAction action;
    action.type = QString(ActionType::DisableEngine);
    action.params.insert(ActionParam::Mode, modeToken);
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
 * match carries. An empty catch-all leaves all three at their defaults
 * and returns true. Window-property leaves (AppId etc.) are ignored — only
 * context fields are read, so a mixed *flat* rule still yields its context
 * projection. When no context-equality leaf was found (e.g. an `All{}` of
 * only window-property leaves), the function returns false; the empty
 * catch-all remains the only success case with all-default outputs.
 *
 * **Contract** — this is strictly the inverse of @ref makeContextMatch, so it
 * only understands the shapes that helper produces: an empty catch-all, a
 * single context-equality leaf, or a flat `All{}` whose children are
 * context-equality leaves. It walks **one level** of children; a nested
 * composite (`All{ Any{...} }`) is not a context rule this bridge authored
 * and decomposing it would be silently lossy. Such an input is left as
 * all-defaults — callers that build their own deeper match expressions must
 * not route them through this readback.
 *
 * A duplicate context-dimension leaf under the same flat `All{}` (e.g. two
 * `ScreenId Equals` leaves) is **refused** rather than silently coerced via
 * last-write-wins: @ref makeContextMatch can never produce duplicates, so a
 * duplicate is undefined input. The dims are reset to defaults and the
 * function returns @c false; a successful decomposition returns @c true.
 * Most callers ignore the return — falling back to all-defaults is the safe
 * "not a context rule we authored" treatment.
 */
inline bool contextDimsOf(const MatchExpression& match, QString& screenId, int& virtualDesktop, QString& activity)
{
    screenId.clear();
    virtualDesktop = 0;
    activity.clear();
    if (match.isCatchAll()) {
        return true;
    }
    // Only a leaf or a flat All{} is a context rule this bridge produced. A
    // composite of any other kind (Any / None) — or an All{} carrying a
    // nested composite child — is outside the contract; leave the defaults.
    if (!match.isLeaf() && match.kind() != MatchExpression::Kind::All) {
        return false;
    }
    const QList<MatchExpression> leaves = match.isLeaf() ? QList<MatchExpression>{match} : match.children();
    bool sawScreen = false;
    bool sawDesktop = false;
    bool sawActivity = false;
    for (const MatchExpression& leaf : leaves) {
        if (isContextEqualsLeaf(leaf, Field::ScreenId)) {
            if (sawScreen) {
                qCWarning(lcRule) << "ContextRuleBridge::contextDimsOf: duplicate ScreenId leaf — refusing to coerce.";
                screenId.clear();
                virtualDesktop = 0;
                activity.clear();
                return false;
            }
            sawScreen = true;
            screenId = leaf.predicate().value.toString();
        } else if (isContextEqualsLeaf(leaf, Field::VirtualDesktop)) {
            if (sawDesktop) {
                qCWarning(lcRule)
                    << "ContextRuleBridge::contextDimsOf: duplicate VirtualDesktop leaf — refusing to coerce.";
                screenId.clear();
                virtualDesktop = 0;
                activity.clear();
                return false;
            }
            sawDesktop = true;
            // Refuse a malformed VirtualDesktop value — `QVariant::toInt()`
            // silently returns 0 for "abc" or a non-positive int, which the
            // bridge contract reads as "no desktop pinned" and would
            // misclassify the rule as a monitor-axis match. `makeContextMatch`
            // only stores a strictly-positive int here, so any other source
            // is a hand-edit or migration error — bail to the same
            // refuse-to-coerce shape the duplicate-leaf guards use.
            bool ok = false;
            const int desktopValue = leaf.predicate().value.toInt(&ok);
            if (!ok || desktopValue <= 0) {
                qCWarning(lcRule)
                    << "ContextRuleBridge::contextDimsOf: malformed VirtualDesktop value — refusing to coerce."
                    << leaf.predicate().value;
                screenId.clear();
                virtualDesktop = 0;
                activity.clear();
                return false;
            }
            virtualDesktop = desktopValue;
        } else if (isContextEqualsLeaf(leaf, Field::Activity)) {
            if (sawActivity) {
                qCWarning(lcRule) << "ContextRuleBridge::contextDimsOf: duplicate Activity leaf — refusing to coerce.";
                screenId.clear();
                virtualDesktop = 0;
                activity.clear();
                return false;
            }
            sawActivity = true;
            activity = leaf.predicate().value.toString();
        }
        // Any leaf that is not a ScreenId / VirtualDesktop / Activity equality
        // leaf is ignored here — a flat mixed rule still yields its context
        // projection, per the contract above. This includes the Mode and
        // TiledWindowCount leaves: both ARE context fields, but neither is one of
        // the three decomposed dimensions, so they are deliberately projected out
        // of contextDimsOf.
    }
    // If no context-equality leaf was matched, the input was either a
    // context-axis-empty mixed rule (e.g. `ScreenId NotEquals "DP-1"`) or
    // a structurally-malformed All{}. Either way, no decomposition succeeded
    // — return false so downstream classifiers don't read this as a
    // successful catch-all decode. The catch-all case is handled at the
    // top of the function (`isCatchAll() → return true`); reaching here
    // with all three flags false means the leaves were all non-equality
    // or non-context.
    if (!sawScreen && !sawDesktop && !sawActivity) {
        return false;
    }
    return true;
}

/// True if @p match pins a context field that is NOT one of the three cascade
/// dimensions (currently only @c TiledWindowCount). Such a match is more
/// specific than the (screen, desktop, activity) shape @c makeContextMatch
/// emits, so it must not be treated as an exact-context assignment: the batch
/// reader/writers and exact-rule upsert rebuild the context base from the decoded
/// dims alone, which would silently drop the extra leaf (the same hazard
/// @c matchIsExactContextActivityStrict guards against for Combined rules). The
/// field set is a function-local static so the per-rule batch classifiers do not
/// reconstruct it on every call.
inline bool pinsNonDimensionContextField(const MatchExpression& match)
{
    static const QSet<Field> kNonDimensionContextFields{Field::TiledWindowCount};
    return match.referencesAnyField(kNonDimensionContextFields);
}

/**
 * @brief Classify @p match's pinned-dimension shape as a @ref ContextAxis.
 *
 * A non-context-only match (one carrying a window-property leaf) returns
 * @c CatchAll, and so does a context-only match that pins a non-dimension
 * context field (e.g. TiledWindowCount) — callers that need to distinguish a
 * window-property rule from a true catch-all must combine this with
 * `match.isContextOnly()`.
 */
inline ContextAxis contextAxisFor(const MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return ContextAxis::CatchAll;
    }
    // A match that ALSO pins a non-dimension context field (TiledWindowCount) is
    // not the (screen, desktop, activity) shape makeContextMatch emits, so it is
    // not an exact-context assignment; treat it as CatchAll-axis so the batch
    // projections skip it. It still resolves normally through the evaluator,
    // which sees the count via the WindowQuery.
    if (pinsNonDimensionContextField(match)) {
        return ContextAxis::CatchAll;
    }
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
    contextDimsOf(match, screenId, virtualDesktop, activity);
    return contextAxisOf(screenId, virtualDesktop, activity);
}

/// True if @p match is a context-only "screen-only" pin — screen present,
/// no desktop, no activity. The per-screen-base family classifier.
inline bool matchIsExactContextBase(const MatchExpression& match)
{
    return contextAxisFor(match) == ContextAxis::Monitor;
}

/// True if @p match is a context-only "screen + desktop" pin (no activity).
/// The per-desktop family classifier — a screen+desktop+activity tuple is
/// the Combined axis and does NOT match this family.
inline bool matchIsExactContextDesktop(const MatchExpression& match)
{
    return contextAxisFor(match) == ContextAxis::Desktop;
}

/// BROAD per-activity family classifier — true if @p match is a context-only
/// rule pinning an activity (with screen), whether or not it ALSO pins a
/// desktop. Used by the cascade-family union @ref isContextAssignmentRule
/// (in layoutregistry_rulehelpers.cpp) to admit Combined rules into the
/// cascade-family union.
///
/// DO NOT use as a per-activity batch reader/writer family classifier — the
/// Activity batch API ((screen, activity) → layout) cannot represent a
/// desktop-pinned Combined rule, so a round-trip through this predicate
/// silently drops the desktop pin. Use @ref matchIsExactContextActivityStrict
/// for the strict per-Activity classifier.
inline bool matchIsExactContextActivity(const MatchExpression& match)
{
    const ContextAxis axis = contextAxisFor(match);
    return axis == ContextAxis::Activity || axis == ContextAxis::Combined;
}

/// STRICT per-activity classifier — screen+activity ONLY, no desktop pin.
/// Used by the Activity batch reader/writer (activityAssignments() /
/// setAllActivityAssignments) so a Combined rule is NOT projected through
/// the (screen, activity) key (would otherwise overwrite a pure-Activity
/// entry or get its desktop pin dropped on round-trip). Combined rules
/// live outside the Activity API's projection and only round-trip through
/// the dedicated Combined batch.
inline bool matchIsExactContextActivityStrict(const MatchExpression& match)
{
    return contextAxisFor(match) == ContextAxis::Activity;
}

/// STRICT Combined classifier — screen + desktop + activity all pinned.
/// Used by the Combined batch API's family classifier so the round-trip
/// touches ONLY Combined rules and leaves the other axes (Monitor,
/// Desktop, Activity) untouched.
inline bool matchIsExactContextCombined(const MatchExpression& match)
{
    return contextAxisFor(match) == ContextAxis::Combined;
}

/// True if @p match is the exact-shape context for @p screenId / @p
/// virtualDesktop / @p activity — i.e. the match @ref makeContextMatch would
/// emit for that tuple. A match pinning different dimensions or carrying any
/// window-property leaf is rejected. Equivalent to `matchIsExactContext*`
/// plus an explicit value check.
inline bool matchIsExactContext(const MatchExpression& match, const QString& screenId, int virtualDesktop,
                                const QString& activity)
{
    if (!match.isContextOnly()) {
        return false;
    }
    // A pinned non-dimension context leaf (TiledWindowCount) means the match is
    // more specific than the bare (screen, desktop, activity) shape, so it is NOT
    // exact — see contextAxisFor for why dropping it here would lose the leaf on
    // rebuild.
    if (pinsNonDimensionContextField(match)) {
        return false;
    }
    QString s;
    int d = 0;
    QString a;
    contextDimsOf(match, s, d, a);
    return s == screenId && d == virtualDesktop && a == activity;
}

/**
 * @brief If @p rule is a per-mode disable rule, return its mode wire token
 *        verbatim; otherwise nullopt.
 *
 * A rule qualifies iff **exactly one** of its actions is a `DisableEngine`
 * action *and* that action's `mode` param is a non-empty wire token. The
 * exactly-one rule is enforced: a second `DisableEngine` action makes the
 * rule ambiguous — it is not a bridge-authored disable rule — so the
 * function bails to nullopt. An empty token is treated as a malformed
 * payload and also returns nullopt.
 *
 * Open-vocabulary within the bridge itself. At persistence boundaries the
 * DisableEngine action descriptor's load-time validator already enforces
 * the closed `engineModeOptions()` set (see ruleaction.cpp), so a rule
 * that survived load has a vocabulary-valid token. The bridge keeps the
 * verbatim contract so an in-memory caller building a rule programmatically
 * — or a test pinning the bridge's unrecognised-token behaviour — can
 * carry any non-empty token without coupling PhosphorRules to
 * PhosphorZones, and so a future mode addition round-trips through the
 * bridge without a coordinated edit.
 */
inline std::optional<QString> disableRuleMode(const Rule& rule)
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
    const QString mode = disableAction->params.value(ActionParam::Mode).toString();
    if (mode.isEmpty()) {
        // An empty token is a malformed payload, not a recognised mode.
        return std::nullopt;
    }
    // The wire vocabulary is the open set the DisableEngine descriptor's
    // validator recognises — Settings does the wire → Mode mapping via
    // `PhosphorZones::modeFromWireString`, which is the authoritative
    // recognised-tokens list. This helper stays string-typed so the
    // PhosphorRules lib does not need to depend on PhosphorZones.
    return mode;
}

} // namespace ContextRuleBridge

} // namespace PhosphorRules
