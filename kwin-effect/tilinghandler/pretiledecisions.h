// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// Pure decision logic for the pre-autotile geometry restore that
/// TilingHandler::demoteWindowsForDesktopSwitch runs, extracted so the branch
/// chain is unit-testable without a compositor: kwin-effect has no linkable
/// test target, but header-only logic is covered by including this file
/// directly (the same pattern scrolldecisions.h documents). The KWin-facing
/// side effects (moveResize, maximize clear, D-Bus dispatch) stay at the call
/// site in screenschanged.cpp — this function decides, it does not act.
namespace PlasmaZones::PreTileDecisions {

/// What the desktop-switch demote pass does with a window's pre-autotile rect.
enum class PreTileRestore {
    None, ///< nothing to restore, or the window was never tile-managed here
    Apply, ///< restore the local bucket rect now
    QueueForWindowedFullscreen, ///< restore it after the deferred windowed-fullscreen release
    AskDaemon, ///< no local rect: fetch the pre-snap geometry from the placement store
    DeclineCrossScreen, ///< a rect exists but belongs to ANOTHER monitor's bucket — do nothing
};

/// The 5-way pre-tile restore decision.
///
/// @p haveLocalRect   a rect was found in some screen's m_preTileGeometries bucket
/// @p rectIsThisOutput  the bucket that held it names the window's CURRENT OUTPUT.
///                     Compare PHYSICAL ids: virtual screens subdivide one output
///                     and share its coordinate space, so a VS re-key must still apply.
/// @p wasTracked      the window was autotile-tracked on the desktop being left
/// @p wasWindowedFs   the window held windowed fullscreen (its release is deferred)
///
/// The cross-screen term is the load-bearing one. Bucket rects are in absolute
/// compositor coordinates, so applying one keyed under a different OUTPUT does
/// not restore a size, it MOVES the window to that output. Users see a window
/// dragged onto this monitor thrown back to the one it came from on the next
/// desktop switch. The sibling desktop-move path declines for the same reason
/// (restorePreTileForDesktopMove), and savePreTileForDesktopMove stamps the
/// bucket screen precisely so the comparison can be made.
///
/// DeclineCrossScreen is distinct from None because it must NOT fall through to
/// AskDaemon: the daemon resolves the geometry against its own possibly-stale
/// screenForWindow(), so that fallback can hand back the very rect declined
/// here. AskDaemon is for the genuinely-empty case (the window was snap-managed
/// when it entered autotile, so nothing was ever stored locally).
inline PreTileRestore resolvePreTileRestore(bool haveLocalRect, bool rectIsThisOutput, bool wasTracked,
                                            bool wasWindowedFs)
{
    if (haveLocalRect && !rectIsThisOutput) {
        return PreTileRestore::DeclineCrossScreen;
    }
    if (!wasTracked) {
        // Every restore arm is gated on tracked-ness: the buckets survive
        // non-destructively, so a window already restored by an earlier switch
        // would otherwise be re-teleported to the stale rect on every later
        // switch onto this desktop.
        return PreTileRestore::None;
    }
    if (!haveLocalRect) {
        return PreTileRestore::AskDaemon;
    }
    return wasWindowedFs ? PreTileRestore::QueueForWindowedFullscreen : PreTileRestore::Apply;
}

} // namespace PlasmaZones::PreTileDecisions
