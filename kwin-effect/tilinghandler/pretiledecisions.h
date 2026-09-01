// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QRectF>
#include <QString>

/// Pure decision logic for the pre-autotile geometry restore that
/// TilingHandler::demoteWindowsForDesktopSwitch runs, extracted so the branch
/// chain is unit-testable without a compositor: kwin-effect has no linkable
/// test target, but header-only logic is covered by including this file
/// directly (the same pattern scrolldecisions.h documents). The KWin-facing
/// side effects (moveResize, maximize clear, D-Bus dispatch) stay at the call
/// site in screenschanged.cpp — this function decides, it does not act.
namespace PlasmaZones::PreTileDecisions {

/// Is a managedScreensChanged announce still describing the desktops the
/// compositor is actually showing?
///
/// The announce is asynchronous; KWin's own desktopChanged is not. So an
/// announce can arrive after the user has switched again, carrying a managed
/// set computed for the desktop they just left. Acting on it installs that set
/// while the catch-scan filters windows by the CURRENT desktop — the two halves
/// describe different desktops, and a window on the new desktop gets tracked
/// against the old desktop's screen set.
///
/// The effect cannot answer this from its own state: by the time the stale
/// announce lands, its last-reported desktop already equals the live one, so a
/// stale announce and a fresh one look identical. Hence the wire stamp.
///
/// @p announced   screenId -> desktop the announced set was resolved against
/// @p reported    screenId -> desktop this effect last reported (m_lastScreenDesktop)
///
/// A screen missing from either side is NOT a mismatch: a screen the effect has
/// never reported has nothing to disagree with. Only a screen present on both
/// sides with DIFFERENT desktops means the announce has been overtaken.
///
/// Keys are compared as given. The caller is responsible for handing both sides
/// the same ID FORM — the daemon stamps engine screen ids, which are virtual on
/// a subdivided output, while the effect reports desktops per physical screen,
/// so comparing them raw finds nothing in common and the gate silently accepts
/// everything. slotScreensChanged normalises to the physical id before calling.
///
/// An empty `announced` accepts vacuously (the loop has nothing to check), and
/// so does one whose keys the reported map does not carry. The caller skips the
/// gate entirely for a wholly empty stamp, but it does reach here with a
/// partially matching stamp, so vacuous acceptance is a real production
/// outcome and not merely a theoretical one.
inline bool announceMatchesReportedDesktops(const QHash<QString, int>& announced, const QHash<QString, int>& reported)
{
    for (auto it = announced.constBegin(); it != announced.constEnd(); ++it) {
        const auto mine = reported.constFind(it.key());
        if (mine != reported.constEnd() && mine.value() != it.value()) {
            return false;
        }
    }
    return true;
}

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
