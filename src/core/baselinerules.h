// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorRules/Rule.h>

#include <QString>
#include <QUuid>

namespace PlasmaZones {

// The managed baseline rules: border, title bar, gap, zone overlay, general
// min-size (Exclude), and animation min-size (ExcludeAnimations). Single source
// of truth for BOTH the daemon's startup seeding (ensureManagedRule) AND the
// settings app's per-page "Reset to defaults" — keeping the two from drifting.
// The gap and overlay rules are true catch-alls; the border and title-bar rules
// are narrowed to tiled/snapped windows on a fresh install; the min-size rules
// match Width/Height LessThan their threshold. All are managed = true, pinned
// to the lowest priority so any user rule overrides them per slot. The owning
// settings pages rewrite their action values (or, for the min-size rules, the
// match threshold); a reset restores exactly these definitions.

/// Common skeleton: empty catch-all match, managed, lowest priority.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineSkeleton(const QUuid& id, const QString& name);

/// Baseline BORDER rule (id ConfigDefaults::baselineBorderRuleId()) — carries the
/// "show border" parent action (default OFF), scoped to tiled/snapped windows.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineBorderRule();

/// Baseline TITLE BAR rule (id baselineTitleBarRuleId()) — carries the default
/// hide-title-bar value, scoped to tiled/snapped windows.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineTitleBarRule();

/// Baseline GAP rule (id baselineGapRuleId()) — the catch-all source of truth for
/// the shared inner/outer gap model. Seeds only the parent actions (inner gap,
/// outer gap, per-side toggle); the four per-side outer-gap actions are added by
/// the Appearance page on demand, so a reset naturally drops them.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineGapRule();

/// Baseline ZONE-OVERLAY appearance rule (id baselineOverlayRuleId()) — the
/// catch-all source of truth for the global drag-overlay appearance (fill /
/// inactive / border colours, active + inactive opacity, border width + radius).
/// Settings reads these actions back as its highlightColor()/activeOpacity()/…
/// getters. Seeds all seven appearance actions at their ConfigDefaults values.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineOverlayRule();

/// Baseline general MIN-WIDTH exclusion rule (id generalMinWidthRuleId()) — a
/// managed, lowest-priority Exclude rule matching Width LessThan the default
/// minimum window width. Keeps sub-threshold windows unmanaged out of the box; the
/// General page edits the threshold in the match (0 = never matches = disabled).
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineGeneralMinWidthRule();

/// Baseline general MIN-HEIGHT exclusion rule (id generalMinHeightRuleId()) — the
/// Height LessThan sibling of makeBaselineGeneralMinWidthRule.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineGeneralMinHeightRule();

/// Baseline animation MIN-WIDTH filter rule (id animationMinWidthRuleId()) — a
/// managed, lowest-priority ExcludeAnimations rule matching Width LessThan the
/// threshold. Defaults to 0 (never matches = filter off); the Animations page
/// edits the threshold in the match.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineAnimationMinWidthRule();

/// Baseline animation MIN-HEIGHT filter rule (id animationMinHeightRuleId()) —
/// the Height LessThan sibling of makeBaselineAnimationMinWidthRule.
PLASMAZONES_EXPORT PhosphorRules::Rule makeBaselineAnimationMinHeightRule();

} // namespace PlasmaZones
