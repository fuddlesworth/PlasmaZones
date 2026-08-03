// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QStringList>

#include "phosphorrules_export.h"

/**
 * @file ExclusionRules.h
 * @brief Slicers that pull the exclusion-family rules out of a unified
 *        `RuleSet`, one slice per consumer concern:
 *         - `excludePlacementRulesFrom` (Exclude ∪ ExcludePlacement) —
 *           bound by the snap engine and the KWin effect's drag gate;
 *         - `excludeDecorationsRulesFrom` (Exclude ∪ ExcludeDecorations)
 *           — bound by the effect's `shouldDecorateWindow` gate;
 *         - `excludeAnimationsRulesFrom` — bound by the effect's
 *           `shouldAnimateWindow` gate;
 *         - `excludeRulesFrom` — the blanket-Exclude-only slice, retained
 *           for consumers that need exactly that shape.
 *        The flat-string variant (`applicationExcludePatternsFrom`)
 *        extracts the bare AppId patterns the WTA pending-restore prune
 *        walks, over the same placement-exclusion membership.
 *
 * After v4 (configmigration.cpp), exclusion rules live exclusively in
 * the unified Rule store — the legacy `excludedApplications` /
 * `excludedWindowClasses` and their animation-side siblings
 * `animationExcludedApplications` / `animationExcludedWindowClasses`
 * QStringList settings retired alongside the bridge that derived rules
 * from them. Consumers ask THIS header "give me the slice of the user's
 * unified rule store shaped for my exclusion concern".
 *
 * LIFETIME / REVISION CONTRACT: every slicer returns a derived `RuleSet`
 * BY VALUE whose revision is always 1. Sink it into a caller-owned
 * long-lived set via `setRules(slice.rules())` — which bumps the owner's
 * monotonic revision, invalidating any bound evaluator's caches — and
 * never bind a `RuleEvaluator` to the returned temporary (the evaluator
 * stores a reference; binding the temporary dangles).
 *
 * Declarations only — bodies live in `src/exclusionrules.cpp` so
 * consumers pay one link edge (not a per-TU inline cost) and the
 * internal predicates (`ruleHasAction`, `rulesWithAction`,
 * `rulesWithEitherAction`, `isPlacementExclusion`) stay file-local. A
 * previous shape had bodies inline in this header; that forced every
 * consumer TU through the full transitive include chain
 * (`MatchExpression.h`, `RuleAction.h`, `Rule.h`, …) and instantiated
 * the function bodies under hidden visibility per TU. The slicers are
 * not on a perf-critical path — the daemon calls them once per
 * `RuleStore::rulesChanged` emission, not per resolution — so the
 * inline win was zero and the include cost was real.
 */

namespace PhosphorRules {

class RuleSet;

namespace ExclusionRules {

/// Slice @p source down to rules with a terminal `Exclude` action — the
/// BLANKET-only shape. RETAINED PUBLIC API with no in-tree production
/// consumer: the snap engine and the effect's drag gate migrated to the
/// placement union slice below, so today only tests call this. It stays
/// exported for the same reason the unread Tag:: constants do (see
/// RuleAction.h's retained-API note) — this is installed LGPL API and a
/// third-party consumer may legitimately want exactly the blanket slice.
/// Disabled rules are skipped at slicing time so the derived set is the
/// minimum admitted by the user — carrying disabled rules through the
/// slice would inflate the downstream `RuleEvaluator`'s priority-order
/// index and would lie to `!isEmpty()` fast-path callers.
/// Rule ids, priorities, and matches are preserved verbatim.
PHOSPHORRULES_EXPORT RuleSet excludeRulesFrom(const RuleSet& source);

/// Slice @p source down to rules carrying a terminal `Exclude` OR
/// `ExcludePlacement` action — the full set of rules that make a window
/// unmanaged by the placement engines (snapping, autotile, scrolling).
/// This is the slice the daemon's engines and the KWin effect's drag
/// gate bind: blanket Exclude keeps its historical placement effect,
/// and the scoped ExcludePlacement joins it without also stripping
/// decorations. Same disabled-skip + verbatim-preservation contract as
/// @ref excludeRulesFrom, which remains the Exclude-only slice for
/// consumers that need the blanket shape alone.
PHOSPHORRULES_EXPORT RuleSet excludePlacementRulesFrom(const RuleSet& source);

/// Slice @p source down to rules carrying a terminal `Exclude` OR
/// `ExcludeDecorations` action — the set the KWin effect's
/// `shouldDecorateWindow` gate binds. Blanket Exclude keeps stripping
/// decorations (preserving the behavior from when the decoration path
/// reused the snapping slice); the scoped ExcludeDecorations strips
/// only decorations. Same disabled-skip + verbatim-preservation
/// contract as @ref excludeRulesFrom.
PHOSPHORRULES_EXPORT RuleSet excludeDecorationsRulesFrom(const RuleSet& source);

/// Slice @p source down to rules with a terminal `ExcludeAnimations`
/// action — the action the v4 fold introduced for the legacy
/// animationExcludedApplications / animationExcludedWindowClasses
/// lists. Used by the KWin effect's `shouldAnimateWindow` gate to
/// suppress animation overrides on matched windows. Same disabled-skip
/// + verbatim-preservation contract as @ref excludeRulesFrom.
PHOSPHORRULES_EXPORT RuleSet excludeAnimationsRulesFrom(const RuleSet& source);

/// Return the AppId pattern of every `AppId AppIdMatches <pattern>` leaf
/// that lives on an enabled `Exclude`- or `ExcludePlacement`-action rule
/// in @p source (both shapes make the window unmanaged by placement, and
/// the pending-restore prune this feeds is a placement concern). Mirrors
/// the deleted runtime bridge's flat-string output so a consumer that
/// needs a flat list of patterns (the WTA pending-restore prune) can
/// derive one from the unified store.
///
/// **Only the simple shape "single AppId AppIdMatches leaf" is
/// recognised** — the v4 migration produces exactly that shape, and a
/// hand-authored Exclude rule with a different match (`WindowClass
/// Contains "steam"`, a composite, an AppId Equals leaf) cannot map to
/// a single canonical AppId pattern and is silently skipped. The
/// snap-engine and drag gate still fire on the rule (they bind the
/// full Exclude slice to a RuleEvaluator, not the harvested string
/// list), but the pending-restore prune cannot see it. Practical
/// consequence: a user authoring a `WindowClass Contains "steam"`
/// Exclude rule keeps Steam windows out of layouts (good), but stale
/// queued pending-restores for Steam on disk are NOT pruned (the
/// queue keeps growing slowly across daemon restarts until a real
/// snap-engine matching cycle re-checks them and discards them as
/// excluded). A future widening of this harvest, or a switch to
/// evaluating the RuleSet directly against each queued
/// WindowQuery, would close that gap.
///
/// Empty / whitespace-only / disabled rules are dropped, and duplicate
/// patterns are collapsed (first occurrence wins) — a blanket Exclude and a
/// scoped ExcludePlacement rule for the same app harvest one entry.
PHOSPHORRULES_EXPORT QStringList applicationExcludePatternsFrom(const RuleSet& source);

} // namespace ExclusionRules

} // namespace PhosphorRules
