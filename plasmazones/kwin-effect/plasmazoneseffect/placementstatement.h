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

/// Why a window is placed on a snapping screen: a user verb about this window
/// (a snap key, a drop, a Snap Assist pick, Meta+F, a routed open), or a
/// re-statement of a placement it already has (PhosphorProtocol::PlacementPurpose
/// on the wire).
enum class Purpose {
    UserVerb,
    Restatement,
};

/// The window's KWin state a placement may have to hand back. Requested state
/// throughout: on Wayland the committed bits trail a client round trip.
struct Inputs
{
    Purpose purpose = Purpose::UserVerb;
    bool maximized = false; ///< any maximize mode other than Restore
    bool requestedFullScreen = false;
    bool windowedFsMember = false; ///< a scrolling windowed-fullscreen claim
    bool engineMaximizeClaim = false; ///< a monocle or maximize-to-edges claim
    bool gestureLive = false; ///< a user move or resize the placement does not own
};

/// What the placement does to that state before its apply. Every hand-back is
/// anchored at the placement rect (its restore rect is seated there first), and
/// fullscreen goes before the maximize: KWin drops a maximize(Restore) issued
/// while fullscreen is requested.
struct Verdict
{
    bool shedWindowedFullscreen = false; ///< drop the claim and end its fullscreen
    bool endFullScreen = false; ///< end a fullscreen the window holds itself
    bool shedEngineMaximize = false; ///< drop the claim and end its maximize
    bool endMaximize = false; ///< end a KWin maximize the window holds itself
};

/// A tiling engine's claim is shed for any purpose: the window is leaving the
/// strip or stack that owns it (F342, F529). A user verb ends the window's own
/// fullscreen and maximize, so the window lands where the verb put it (F505,
/// F524, F582). A re-statement hands back a maximize only, and never on a
/// fullscreen window. A live gesture the placement does not own writes nothing;
/// the deferred replay decides again once it ends.
constexpr Verdict decide(const Inputs& in)
{
    Verdict verdict;
    if (in.gestureLive) {
        return verdict;
    }
    verdict.shedWindowedFullscreen = in.windowedFsMember;
    verdict.shedEngineMaximize = in.engineMaximizeClaim;
    const bool ownFullScreen = in.requestedFullScreen && !in.windowedFsMember;
    const bool ownMaximize = in.maximized && !in.engineMaximizeClaim;
    if (in.purpose == Purpose::UserVerb) {
        verdict.endFullScreen = ownFullScreen;
        verdict.endMaximize = ownMaximize;
    } else {
        verdict.endMaximize = ownMaximize && !ownFullScreen;
    }
    return verdict;
}

} // namespace PlasmaZones::PlacementStatement
