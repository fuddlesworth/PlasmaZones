// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// What each KWin window-flag change edge runs, header-only so the table is
/// unit-testable without a compositor (same pattern as
/// handlers/instantrestoredecisions.h). wireOutputChangeHandlers connects one
/// edge per flag and acts on this table.
///
/// Every flag is a rule field on both ends, so every edge refreshes the
/// daemon registry's copy. The effect's own cached verdicts are dropped too,
/// except for three flags whose invalidation would re-assert or re-read a
/// write PlasmaZones itself made: keep-above and keep-below (the layer
/// reconcile would put a SetWindowLayer rule back over the user's own toggle)
/// and the decoration (SetHideTitleBar writes the very flag it would then
/// read). The flags-settle eviction runs for the four structural tiling
/// filters only: keep-above, skip-switcher, transient and modal.
namespace PlasmaZones::WindowFlagEdges {

enum class Flag {
    KeepAbove,
    KeepBelow,
    SkipTaskbar,
    SkipPager,
    SkipSwitcher,
    Transient,
    Modal,
    Maximizable,
    Decoration,
};

struct Actions
{
    bool reevaluateEligibility = false; ///< the flags-settle eviction (structural tiling filters)
    bool pushMetadata = false; ///< refresh the daemon registry's copy of the field
    bool invalidateRules = false; ///< drop the effect's per-window verdicts
};

constexpr Actions actionsFor(Flag flag)
{
    switch (flag) {
    case Flag::KeepAbove:
        return {true, true, false};
    case Flag::KeepBelow:
    case Flag::Decoration:
        return {false, true, false};
    case Flag::SkipTaskbar:
    case Flag::SkipPager:
    case Flag::Maximizable:
        return {false, true, true};
    case Flag::SkipSwitcher:
    case Flag::Transient:
    case Flag::Modal:
        return {true, true, true};
    }
    return {};
}

} // namespace PlasmaZones::WindowFlagEdges
