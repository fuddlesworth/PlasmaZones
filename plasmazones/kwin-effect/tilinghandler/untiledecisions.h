// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file untiledecisions.h
 * @brief The untile diff's pure decision: whether a window a tile batch no
 *        longer carries for its screen is untiled there. No KWin types, so the
 *        rows are unit tested; the caller reads the live window.
 */

namespace PlasmaZones::UntileDecisions {

/// A tile batch describes ONE (screen, desktop, activity) context, the one its
/// screen shows. A window the batch no longer carries is untiled on that screen
/// unless the batch has no say over it:
///  - it sits on a desktop the batch's OUTPUT is not showing (per-output
///    desktops, F211) or on another activity: a sibling context's retile
///    decides it (#808);
///  - the user is dragging it: the drag ends its tiling on its own terms, and
///    untiling it mid-drag strips its tiled chrome under the pointer (F185);
///  - it is moving off this screen: the daemon armed its output move from this
///    screen, so its destination's batch takes it over (F283).
/// A window that no longer resolves is untiled, so its tracking goes.
constexpr bool untilesOnBatchScreen(bool resolved, bool onDesktopShownOnBatchOutput, bool onCurrentActivity,
                                    bool beingDragged, bool movingOffThisScreen)
{
    if (!resolved) {
        return true;
    }
    return onDesktopShownOnBatchOutput && onCurrentActivity && !beingDragged && !movingOffThisScreen;
}

} // namespace PlasmaZones::UntileDecisions
