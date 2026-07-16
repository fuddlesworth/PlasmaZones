// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Private (non-installed) header — rule-shape classification and context
// helpers for the LayoutRegistry assignment cascade.
//
// These are pure functions with no LayoutRegistry-member dependency: they
// classify and decode Rule / MatchExpression shapes, build the
// windowless context query, and read an AssignmentEntry straight off a
// rule's action list. Split out of layoutregistry_assignments.cpp so that
// translation unit stays under the project's 1000-line guideline, and so any
// sibling .cpp in phosphor-zones can share the one classifier set rather
// than duplicating it.
//
// The functions live in the named PhosphorZones::RuleHelpers namespace
// (not an anonymous one) precisely so they can be defined once in
// layoutregistry_rulehelpers.cpp and referenced from other TUs.

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <PhosphorRules/WindowQuery.h>

#include <QList>
#include <QString>

namespace PhosphorRules {
class MatchExpression;
class Rule;
} // namespace PhosphorRules

namespace PhosphorZones::RuleHelpers {

namespace PWR = PhosphorRules;

// The decoded (screenId, virtualDesktop, activity) context a context-rule's
// match expression pins. A non-context / nested-composite match leaves all
// three at their defaults (empty / 0).
struct ContextDims
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
};

// Build the windowless context query for a (screen, desktop, activity) tuple.
// No window attributes are set — window-property predicates evaluate false,
// so only context-only rules contribute. This reproduces the old cascade.
// @p mode is the placement-mode wire token ("snapping" / "tiling"); it is set
// only on the gap-cascade query (the snap engine vs. the autotile engine each
// know which they are) so a per-mode `Mode Equals "…"` rule resolves. Left
// empty for the mode-agnostic resolvers (assignment / lock / overlay), where
// it stays a non-match for any Mode leaf.
PWR::WindowQuery makeContextQuery(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const QString& mode = QString());

// Human-readable label for a context assignment rule's (screen, desktop,
// activity) tuple — the single place the " · Desktop N" / " · Activity"
// suffix shape is constructed (used by upsert + every batch setter).
QString contextRuleName(const QString& screenId, int virtualDesktop, const QString& activity);

// Decode a match expression's pinned (screenId, desktop, activity) context
// tuple via the shared ContextRuleBridge::contextDimsOf — the one classifier
// for context-rule shape.
ContextDims decodeDims(const PWR::MatchExpression& match);

// True if @p match is exactly the context shape for the pinned dimensions of
// (screenId, virtualDesktop, activity) — i.e. the match ContextRuleBridge
// would emit for that tuple. A match pinning more/fewer dimensions, pinning
// different values, or carrying ANY window-property leaf is NOT an exact
// match. This is what hasExplicitAssignment relies on to distinguish a stored
// entry from a wider cascade entry or the gated default.
//
// contextDimsOf ignores window-property leaves, so a flat rule mixing a
// window predicate with the context leaves (e.g. All{ ScreenId==DP-1,
// AppId==konsole }) would still decode to the same tuple — the
// isContextOnly() gate is the discriminator that rejects it, mirroring
// isContextAssignmentRule's "context-only" contract so upsert / clear never
// clobber a window-property rule.
bool matchIsExactContext(const PWR::MatchExpression& match, const QString& screenId, int virtualDesktop,
                         const QString& activity);

// True if @p rule carries a SetEngineMode action. Context assignment rules
// carry one; the matchIsExactContext* shape filters reject a catch-all, so a
// user-authored catch-all engine rule is handled by priority alone.
bool hasEngineModeAction(const PWR::Rule& rule);

// True if @p rule carries a SetSnappingLayout / SetTilingAlgorithm action. The
// per-slot assignment resolver reads each layout slot independently of the
// engine-mode slot, so a layout-only rule (no SetEngineMode) sets the layout
// for its engine in a context without forcing the engine mode.
bool hasSnappingLayoutAction(const PWR::Rule& rule);
bool hasTilingAlgorithmAction(const PWR::Rule& rule);

// True when every action on @p rule is one of the three assignment slots
// (SetEngineMode / SetSnappingLayout / SetTilingAlgorithm). False on an
// empty action list. Used by the shape-based fallback in
// findExactContextRule to refuse to claim a user-authored rule that
// carries non-assignment actions (SetOpacity, OverrideAnimation*, Float,
// Exclude, ...) — admitting it would silently strip those actions
// through the assignment-rebuild path.
bool isPureAssignmentRule(const PWR::Rule& rule);

// Shape predicates for the per-screen-base / per-desktop / per-activity
// context rule families — used by the batch setters to drop one family
// before writing the new entries, and by the introspection helpers to keep
// their family filter identical to the batch setters'. Each gates on
// MatchExpression::isContextOnly() before decoding (see matchIsExactContext
// for why a window-property leaf must never classify as a context family
// member), then decomposes via the shared decodeDims (contextDimsOf).
bool matchIsExactContextBase(const PWR::MatchExpression& match);
bool matchIsExactContextDesktop(const PWR::MatchExpression& match);
bool matchIsExactContextActivity(const PWR::MatchExpression& match);

// True if @p rule is a pure context-assignment rule for one of the cascade
// families (per-screen-base / per-desktop / per-activity) — i.e. it carries a
// SetEngineMode action AND its match is exactly a pinned context shape (not
// the catch-all, not a window-property rule that happens to carry an
// engine-mode action). The batch purge / clear loops gate on this so a
// legitimate window-property rule carrying SetSnappingLayout / SetEngineMode
// actions is never rebuilt — rebuilding force-injects SetEngineMode and
// drops every other action, which would clobber a window-property rule.
bool isContextAssignmentRule(const PWR::Rule& rule);

// Build the AssignmentEntry encoded directly by a rule's action list (no
// evaluation — used by introspection helpers like desktopAssignments()).
AssignmentEntry entryFromRuleMatchActions(const PWR::Rule& rule);

// The lowest priority that strictly outranks every existing context-assignment
// rule in @p rules — (max isContextAssignmentRule priority) + 1, or the Context
// band top (kContextBandBase + 99) when none exist. A freshly CREATED runtime
// assignment seeds from this so it wins over any prior assignment (the
// priority-wins model); an UPDATE preserves its own stored priority instead.
int nextAssignmentPriority(const QList<PWR::Rule>& rules);

} // namespace PhosphorZones::RuleHelpers
