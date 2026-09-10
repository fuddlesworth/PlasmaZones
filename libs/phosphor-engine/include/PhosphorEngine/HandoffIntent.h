// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QPoint>

#include <optional>

namespace PhosphorEngine {

/// Where a window handed to another placement context should land, as the
/// user expressed it (a drop point, or a slot the daemon already resolved
/// from one). Threaded from the workspace overview's verbs through the
/// daemon's cross-mode move into IPlacementEngine::HandoffContext, whose
/// dropPos / insertIndex / insertTileIndex fields it fills. An intent with
/// no drop point and no index leaves every arm on its direction-derived
/// default, which is what the directional verbs get.
struct HandoffIntent
{
    /// Drop point in GLOBAL logical pixels (the target output's origin plus
    /// the workspace-local point the overview sent). An optional rather than
    /// a null-checked point so a drop at the output's origin is a drop.
    std::optional<QPoint> dropPos;
    /// Scrolling: column index; autotile: window-order index. -1 = unset.
    int insertIndex = -1;
    /// Scrolling: tile index inside the column at insertIndex; -1 = a new
    /// column.
    int insertTileIndex = -1;

    bool isEmpty() const
    {
        return !dropPos && insertIndex < 0 && insertTileIndex < 0;
    }
};

} // namespace PhosphorEngine
