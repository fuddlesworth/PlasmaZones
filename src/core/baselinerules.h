// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorRules/Rule.h>

#include <QString>
#include <QUuid>

namespace PlasmaZones {

// The three managed baseline appearance/gap rules. Single source of truth for
// BOTH the daemon's startup seeding (ensureManagedRule) AND the settings app's
// per-page "Reset to defaults" on the Windows appearance page — keeping the two
// from drifting. The gap rule is a true catch-all; the border and title-bar
// rules are narrowed to tiled/snapped windows on a fresh install. All three are
// managed = true, pinned to the lowest priority so any user rule overrides them
// per slot. The Appearance page rewrites their action values; a reset restores
// exactly these definitions.

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

} // namespace PlasmaZones
