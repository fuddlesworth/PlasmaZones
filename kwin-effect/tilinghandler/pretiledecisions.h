// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRectF>

/// Pure decision logic for the pre-autotile geometry restore that
/// TilingHandler::demoteWindowsForDesktopSwitch runs, extracted so the branch
/// chain is unit-testable without a compositor: kwin-effect has no linkable
/// test target, but header-only logic is covered by including this file
/// directly (the same pattern scrolldecisions.h documents). The KWin-facing
/// side effects (moveResize, maximize clear, D-Bus dispatch) stay at the call
/// site in screenschanged.cpp — this function decides, it does not act.
namespace PlasmaZones::PreTileDecisions {

/// The rect that may safely be APPLIED for a window now sitting on some output,
/// given the rect stored in a pre-autotile bucket.
///
/// @p saved        the stored rect (any bucket)
/// @p sameOutput   the bucket that held it names the window's CURRENT output.
///                 Compare PHYSICAL ids: virtual screens subdivide one output
///                 and share its coordinate space, so a VS re-key is `true`.
/// @p currentFrame the window's live frame, used when the origin must be dropped
///
/// Bucket rects are ABSOLUTE compositor coordinates. One measured on another
/// output therefore does not restore a size when applied whole — it MOVES the
/// window to that output, which users read as windows being thrown across
/// monitors (discussion #1028). The extents are coordinate-space-independent
/// and the origin is not, so the cross-output answer keeps the size and takes
/// the position from the live frame. Degrading rather than refusing matters:
/// every caller's purpose is to UN-TILE the window, and refusing leaves it
/// sitting at its tile rect still looking tiled.
inline QRectF applicablePreTileRect(const QRectF& saved, bool sameOutput, const QRectF& currentFrame)
{
    if (!saved.isValid()) {
        return {};
    }
    if (sameOutput) {
        return saved;
    }
    if (!currentFrame.isValid()) {
        return {};
    }
    return QRectF(currentFrame.topLeft(), saved.size());
}

/// What the desktop-switch demote pass does with a window's pre-autotile rect.
enum class PreTileRestore {
    None, ///< nothing to restore, or the window was never tile-managed here
    Apply, ///< restore the local bucket rect now
    QueueForWindowedFullscreen, ///< restore it after the deferred windowed-fullscreen release
    AskDaemon, ///< no local rect: fetch the pre-snap geometry from the placement store
};

/// The 4-way pre-tile restore decision.
///
/// @p haveLocalRect   TilingHandler::preTileRestoreRectFor answered a usable rect
/// @p wasTracked      the window was autotile-tracked on the desktop being left
/// @p wasWindowedFs   the window held windowed fullscreen (its release is deferred)
///
/// The cross-OUTPUT question is deliberately NOT decided here. Bucket rects are
/// absolute compositor coordinates, so one measured on another monitor would
/// move the window there rather than restore a size — but the right answer is
/// to keep its extents and drop its origin, not to refuse it, and that needs the
/// window's live frame. preTileRestoreRectFor owns that, so by the time this
/// function runs a valid rect is always safe to apply as-is.
///
/// AskDaemon is only for the genuinely-empty case: the window was snap-managed
/// when it entered autotile, so nothing was ever stored locally.
inline PreTileRestore resolvePreTileRestore(bool haveLocalRect, bool wasTracked, bool wasWindowedFs)
{
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
