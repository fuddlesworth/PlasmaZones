// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file placementstatement.h
 * @brief Pure decisions about what a placement does to a window's KWin
 *        fullscreen and maximize state. No KWin types, so the rows are unit
 *        tested; the effect gathers the inputs from the live window.
 */

namespace PlasmaZones::PlacementStatement {

/// Whether a geometry apply skips a fullscreen window. With a KWin window at
/// hand its REQUESTED state decides, because the committed bit lags a client
/// round trip both ways: a window that just asked for fullscreen already is
/// one, and a window whose fullscreen was just requested off (the
/// windowed-fullscreen exit) no longer is, so its restoring rect is not
/// swallowed. Without one the committed bit is all there is. A scrolling
/// windowed-fullscreen member holds fullscreen at its column rect on purpose,
/// so its applies are the feature and never bail.
constexpr bool fullscreenBails(bool hasKWinWindow, bool committedFullScreen, bool requestedFullScreen,
                               bool windowedFsMember)
{
    return !windowedFsMember && (hasKWinWindow ? requestedFullScreen : committedFullScreen);
}

} // namespace PlasmaZones::PlacementStatement
