// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "phosphorwindowrules_export.h"

/**
 * @file ExclusionRules.h
 * @brief Slicers that pull the `Exclude`- or `ExcludeAnimations`-action
 *        rules out of a unified `WindowRuleSet`. The snap-engine and the
 *        KWin effect's drag gate bind the Exclude slice to their match
 *        evaluators; the KWin effect's `shouldAnimateWindow` gate binds
 *        the ExcludeAnimations slice. The flat-string variant
 *        (`applicationExcludePatternsFrom`) extracts the bare AppId
 *        patterns the WTA pending-restore prune walks.
 *
 * After v4 (configmigration.cpp), exclusion rules live exclusively in
 * the unified WindowRule store — the legacy `excludedApplications` /
 * `excludedWindowClasses` and their animation-side siblings
 * `animationExcludedApplications` / `animationExcludedWindowClasses`
 * QStringList settings retired alongside the bridge that derived rules
 * from them. Consumers ask THIS header "give me the Exclude- or
 * ExcludeAnimations-shaped slice of the user's unified rule store".
 *
 * Declarations only — bodies live in `src/exclusionrules.cpp` so
 * consumers pay one link edge (not a per-TU inline cost) and the
 * internal `ruleHasAction` / `rulesWithAction` predicates stay
 * file-local. A previous shape had bodies inline in this header; that
 * forced every consumer TU through the full transitive include chain
 * (`MatchExpression.h`, `RuleAction.h`, `WindowRule.h`, …) and
 * instantiated three function bodies under hidden visibility per TU.
 * The slicers are not on a perf-critical path — the daemon calls them
 * once per `WindowRuleStore::rulesChanged` emission, not per
 * resolution — so the inline win was zero and the include cost was
 * real.
 */

namespace PhosphorWindowRules {

class WindowRuleSet;

namespace ExclusionRules {

/// Slice @p source down to rules with a terminal `Exclude` action.
/// Used by SnapEngine and the KWin effect's drag gate. Disabled rules
/// are skipped at slicing time so the derived set is the minimum
/// admitted by the user — carrying disabled rules through the slice
/// would inflate the downstream `RuleEvaluator`'s priority-order index
/// and would lie to `!isEmpty()` fast-path callers (e.g. users who
/// have disabled all of one shape — all snapping Excludes off).
/// Rule ids, priorities, and matches are preserved verbatim.
PHOSPHORWINDOWRULES_EXPORT WindowRuleSet excludeRulesFrom(const WindowRuleSet& source);

/// Slice @p source down to rules with a terminal `ExcludeAnimations`
/// action — the action the v4 fold introduced for the legacy
/// animationExcludedApplications / animationExcludedWindowClasses
/// lists. Used by the KWin effect's `shouldAnimateWindow` gate to
/// suppress animation overrides on matched windows. Same disabled-skip
/// + verbatim-preservation contract as @ref excludeRulesFrom.
PHOSPHORWINDOWRULES_EXPORT WindowRuleSet excludeAnimationsRulesFrom(const WindowRuleSet& source);

/// Return the AppId pattern of every `AppId AppIdMatches <pattern>` leaf
/// that lives on an enabled `Exclude`-action rule in @p source. Mirrors
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
/// evaluating the WindowRuleSet directly against each queued
/// WindowQuery, would close that gap.
///
/// Empty / whitespace-only / disabled rules are dropped.
PHOSPHORWINDOWRULES_EXPORT QStringList applicationExcludePatternsFrom(const WindowRuleSet& source);

} // namespace ExclusionRules

} // namespace PhosphorWindowRules
