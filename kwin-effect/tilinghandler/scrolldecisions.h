// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSize>
#include <QtGlobal>

/// Pure decision logic for the scroll-managed window mechanisms in
/// TilingHandler, extracted so the branch chains are unit-testable without a
/// compositor: kwin-effect has no linkable test target, but header-only
/// logic is covered by including this file directly (the established pattern
/// for the anchor-uniform and shader-timing helpers). The KWin-facing side
/// effects (setFullScreen, moveResize, layer demotion, D-Bus dispatch) stay
/// at the call sites in tiling.cpp — these functions decide, they do not
/// act.
namespace PlasmaZones::ScrollDecisions {

/// What the windowed-fullscreen half of a batch entry does for one window.
enum class WfsAction {
    None, ///< untouched (not flagged and not held, or adopt refused by the in-flight marker)
    Adopt, ///< flagged, not held: enter windowed fullscreen (also the effect-restart re-adoption)
    DeferredReconcile, ///< flagged + held but fullscreen not even REQUESTED: the client exited on
                       ///< its own while the daemon gate was closed — drop membership and tell the daemon
    Refresh, ///< flagged + held + requested: steady state, refresh the stored rect and the demotion
    Release, ///< un-flagged while held: leave windowed fullscreen
};

struct WfsDecision
{
    WfsAction action = WfsAction::None;
    /// A flag-off entry is the authoritative echo of a clearWindowedFullscreen
    /// this effect sent — the in-flight marker is consumed BEFORE the action
    /// so a completed clear cannot latch the adopt guard.
    bool consumeClearMarker = false;
};

/// The 5-way windowed-fullscreen batch decision over its four inputs.
/// @p flagOnWire      the batch entry's isWindowedFullscreen
/// @p inSet           effect-side membership (m_windowedFullscreenWindows)
/// @p requestedFullscreen KWin's isRequestedFullScreen() for the window
/// @p clearInFlight   the clear-in-flight marker is armed for this window
inline WfsDecision resolveWindowedFullscreenAction(bool flagOnWire, bool inSet, bool requestedFullscreen,
                                                   bool clearInFlight)
{
    WfsDecision d;
    if (!flagOnWire) {
        d.consumeClearMarker = clearInFlight;
        d.action = inSet ? WfsAction::Release : WfsAction::None;
        return d;
    }
    if (!inSet) {
        // The armed marker refuses re-adoption: a batch the daemon emitted
        // BEFORE processing our clear can still carry flag=true, and
        // adopting it would re-fullscreen the window the user just exited.
        //
        // SINGLE-SHOT. The marker only has to outlive the batches already in
        // flight when the clear was sent, which is normally one, and consuming
        // it here is the only thing that bounds it. Waiting for a flag=false
        // entry to consume it latches for the session whenever the user
        // re-enters windowed fullscreen before the daemon's flag-off batch
        // arrives: the flag goes true again, no flag-off entry is ever
        // emitted, and every later batch is refused. The successful reply
        // cannot consume it either, because the reply can land before the
        // stale batch, which is the whole race the marker exists for.
        if (clearInFlight) {
            d.consumeClearMarker = true;
            d.action = WfsAction::None;
            return d;
        }
        d.action = WfsAction::Adopt;
        return d;
    }
    // Requested state, not committed: during our OWN enter round-trip the
    // committed isFullScreen() lags a client round-trip while requested is
    // already true, and a batch landing in that window must not read the
    // lag as a client exit.
    d.action = requestedFullscreen ? WfsAction::Refresh : WfsAction::DeferredReconcile;
    return d;
}

/// What the column-maximize half of a batch entry does for one window.
enum class MaximizeAction {
    None, ///< the KWin bit already agrees with the engine
    Apply, ///< engine says maximized and KWin's bit is not set (or we are not yet a member)
    Release, ///< engine dropped the maximize while we hold the bit
};

/// The column-maximize batch decision over its three inputs.
///
/// TAKES AN IN-FLIGHT MARKER, like its windowed-fullscreen sibling above,
/// though for a different reason. That one needs one because its dispatch
/// (clearWindowedFullscreen) drops effect-side membership at once, opening a
/// window where a pre-clear batch still carrying flag=true would take the
/// Adopt arm and re-fullscreen the window the user just exited.
///
/// This one needs one because the interception no longer writes KWin's bit
/// before dispatching. It used to cancel the user's flip back to whatever the
/// engine last said, which meant effect-side state agreed with the pre-toggle
/// flag for the whole round trip and a stale batch genuinely did resolve to
/// None on its own terms. That is where the older "no marker is needed"
/// reasoning came from, and deleting the cancel took the guarantee with it.
///
/// What is exposed without a marker is the RESTORE direction only. The user's
/// click has already cleared KWin's bit, membership has not moved, and a batch
/// the daemon emitted before it dequeued the toggle still carries flag=true —
/// (1, 1, 0), which is Apply. The window is re-maximized mid-flight and the
/// same batch's geometry apply commits the stale maximized rect, so it flashes
/// back to full width until the answering batch lands. Maximize is (0, 0, 1)
/// and resolves to None, so the exposure is asymmetric.
///
/// Only the `inSet && !kwinMaximized` leg is suppressed. The adopt leg
/// (`!inSet`) and Release must both still run while armed, or the engine's own
/// answer would be dropped. The suppressed leg exists to repair an effect
/// restart or a KWin-dropped bit, neither of which is time-critical, so
/// deferring it by a batch costs nothing.
///
/// @p flagOnWire     the batch entry's maximizedToEdges
/// @p inSet          effect-side membership (m_maximizedToEdgesWindows)
/// @p kwinMaximized  whether KWin holds MaximizeFull. The batch arm passes
///                   requestedMaximizeMode(), not the committed maximizeMode():
///                   the committed bit trails a client round trip on Wayland,
///                   and reading it re-resolves to Apply on every batch that
///                   lands inside that window. The interception passes the
///                   COMMITTED mode instead, because there it is comparing
///                   against what actually landed.
/// @p toggleInFlight a dispatched toggleMaximizeToEdges for this window whose
///                   reply has not arrived. Deliberately has NO default
///                   argument: a defaulted parameter would let the call site
///                   keep compiling while silently never passing the marker,
///                   which is a fix present in the header and absent in
///                   production that no test would catch.
inline MaximizeAction resolveMaximizeToEdgesAction(bool flagOnWire, bool inSet, bool kwinMaximized, bool toggleInFlight)
{
    if (!flagOnWire) {
        return inSet ? MaximizeAction::Release : MaximizeAction::None;
    }
    // A stale pre-toggle batch on the restore direction lands exactly here.
    if (inSet && !kwinMaximized && toggleInFlight) {
        return MaximizeAction::None;
    }
    // Apply covers two situations that want the identical side effect: a
    // column that just became maximized, and a member whose bit went missing
    // (an effect restart with the daemon still holding the state, or KWin
    // dropping it during a screen change). kwinMaximized is what keeps the
    // steady state from re-calling maximize() on every batch of every tile of
    // a maximized column.
    return (!inSet || !kwinMaximized) ? MaximizeAction::Apply : MaximizeAction::None;
}

/// The counter-assert burst budget for scroll-managed X11 windows an
/// EXTERNAL mover relocated: RATE-LIMITED to @p maxPerWindow counters per
/// rolling window of @p windowMs, re-armed by every fresh batch command
/// (the caller resets the pair on batch insert). Mutates the per-window
/// bookkeeping in place and answers whether THIS frame change should be
/// countered. @p frameDiffers is the caller's `actual != commanded` test.
inline bool shouldCounterAssert(qint64& burstStartMs, int& burstCount, qint64 nowMs, bool frameDiffers,
                                qint64 windowMs = 1000, int maxPerWindow = 3)
{
    if (!frameDiffers) {
        return false;
    }
    if (nowMs - burstStartMs > windowMs) {
        burstStartMs = nowMs;
        burstCount = 0;
    }
    if (burstCount < maxPerWindow) {
        ++burstCount;
        return true;
    }
    return false;
}

/// Whether the scroll size-continuity cache may carry a client's committed
/// size forward as this batch's offer, instead of offering the column rect.
///
/// The cache (`m_scrollOfferedColumn`) exists to end a few-pixel size
/// renegotiation with clients that enforce their own geometry: once such a
/// client has answered for a given column SIZE, the batch offers the size it
/// actually holds, centred in the column, which is a pure move it cannot
/// renegotiate. Extracted here because the whole defect it caused was a
/// MISSING TERM in an inline guard, which is the one shape a truth table
/// catches and a reviewer does not.
///
/// @p declaredRect  the column's rect is DECLARED state — maximize-to-edges,
///                  monocle, or windowed fullscreen. Never carried forward:
///                  in those the gap between the declared rect and the
///                  client's preference is the whole point of the state, so
///                  honouring the client commits a maximized window at its
///                  pre-maximize size in the middle of the screen. (The
///                  mechanism's own premise — that the difference is a few
///                  pixels of renegotiation — is simply false for them.)
/// @p columnAnswered  a cache entry exists whose SIZE equals this column's.
/// @p committed     the client's current frame size.
/// @p column        this batch's column size.
///
/// A committed size LARGER than the column on either axis is refused too: it
/// is not an answer to this column but a stale frame from a bigger one the
/// client has not been reconfigured out of yet. That is reachable on the
/// un-maximize batch — the column is back at its stored width while the client
/// still holds the raw work area — where carrying it forward would commit the
/// maximized size at the restored column's origin.
inline bool mayCarryCommittedSize(bool declaredRect, bool columnAnswered, const QSize& committed, const QSize& column)
{
    if (declaredRect || !columnAnswered || committed.isEmpty() || committed == column) {
        return false;
    }
    return committed.width() <= column.width() && committed.height() <= column.height();
}

// ── Compositor-state claims ────────────────────────────────────────────────
//
// The effect imposes three kinds of compositor state that only it can hand
// back, each tracked in its own per-window ledger. Which of them a given exit
// path releases is decided HERE, as data, rather than by whether somebody
// remembered to write the call at that site.
//
// That is the whole point: a missing release is an ABSENCE, so it compiles,
// tests and reviews clean, and what it leaves behind is a window holding
// compositor state with nothing recording the debt. The third claim below
// arrived missing eight of its releases, and the two asymmetries introduced
// while fixing those were the same shape. A table cannot forget a cell.

/// A kind of compositor state the effect imposed on a window.
enum class Claim {
    MonocleMaximize, ///< KWin maximize held for a monocle tile
    WindowedFullscreen, ///< KWin fullscreen + a keep-flag layer demotion
    MaximizedToEdges, ///< KWin maximize held for a maximized-to-edges scroll column
};

/// Why the effect's authority over a window is ending. Each exit path names
/// itself, and the table below says which claims answer to that name — so a
/// deliberate blank is a cell a reader can see rather than a call nobody
/// wrote.
///
/// THREE OF THESE SIX HAVE NO PRODUCTION CALLER, and that is a decision, not
/// a gap. StripExit, UntrackFunnel and PassiveFloat route their releases
/// through releaseAllClaims. ModeFlip, FullscreenExitWhileFloating and
/// Teardown do not, because the funnel's shape is wrong for them:
///
///  - ModeFlip's release is deliberately SPLIT IN TWO. The demote pass sheds
///    before the engines re-place, and the pre-tile restores run after; one
///    funnel call at either end would collapse a split the ordering depends
///    on.
///  - Teardown releases in bulk across every tracked window and has to be
///    re-entrant (the daemon can drop mid-sweep). It handles that itself,
///    per claimReleaseOrder; funnelling it per-window would re-enter the
///    funnel's own guards N times to no benefit.
///  - FullscreenExitWhileFloating has no funnel call for no argued reason:
///    its repair site releases directly, and nobody has routed it. Unlike
///    the two above, this one is an OMISSION rather than a decision. It is
///    recorded as such instead of quietly fixed here, because routing it
///    changes which claims release on a fullscreen exit and that belongs in
///    its own commit with its own live check (the same standing as the
///    UntrackFunnel monocle blank below).
///
/// All three stay in the enum because the table is what the tests drive, and
/// because the answer to "does monocle release on a mode flip" has to be
/// written down somewhere a reader can find it. Do not "finish the wiring"
/// by pointing the first two at releaseAllClaims without re-deriving their
/// reasons.
enum class ClaimScope {
    /// The window is leaving the strip outright and no later batch will carry
    /// it: the untile diff, the active float channel, the leaving-scrolling
    /// loop.
    StripExit,
    /// The untrack funnel — a close, or a cross-output transfer to a screen
    /// this handler's engines do not manage.
    ///
    /// Identical to StripExit except that monocle does NOT release here, and
    /// that blank is RECORDED rather than filled because it is unresolved, not
    /// decided. Monocle rides cleanupClosedWindowState's bare scrub on this
    /// path instead, which is right for a close (a dying window has nothing to
    /// restore) and questionable for the cross-output half (the window
    /// survives on a screen this handler no longer manages, holding a maximize
    /// bit nothing will hand back).
    ///
    /// Filling it changes shipped monocle handling on a path the nested
    /// harness cannot drive — the engine re-places script-driven cross-output
    /// moves — so it wants a live check and its own argued commit. Encoded so
    /// the next reader sees a cell with a reason rather than an omission with
    /// none.
    UntrackFunnel,
    /// The daemon's passive float channel, whose producers never reach the
    /// active funnel.
    PassiveFloat,
    /// A screen changed mode, desktop or activity: the demote pass, the
    /// removed-screens sweep, the pre-tile restores.
    ModeFlip,
    /// A window left fullscreen while floating — the repair arm for a claim
    /// that was retained through the fullscreen hold.
    FullscreenExitWhileFloating,
    /// Engine disable, daemon loss, daemon bring-up, effect unload. Every
    /// claim answers, and the ORDER matters (see claimReleaseOrder).
    Teardown,
};

/// Whether @p claim releases on @p scope.
///
/// Every blank below is deliberate and load-bearing; none is an oversight.
/// Changing one is a behaviour change and belongs in its own commit with its
/// own argument.
inline constexpr bool claimReleasesOn(Claim claim, ClaimScope scope)
{
    switch (scope) {
    case ClaimScope::StripExit:
        // All three: the defining case. Nothing else will carry the window.
        return true;
    case ClaimScope::UntrackFunnel:
        // Monocle blank, and UNRESOLVED rather than decided — see the enum.
        return claim != Claim::MonocleMaximize;
    case ClaimScope::PassiveFloat:
        // Monocle is EXCLUDED, and documented at its site: re-driving a
        // maximize restore from a passive float signal has not been shown
        // safe against the monocle batch that owns that membership. The
        // other two have no such owner on this channel.
        return claim != Claim::MonocleMaximize;
    case ClaimScope::ModeFlip:
        return true;
    case ClaimScope::FullscreenExitWhileFloating:
        // Windowed fullscreen is not held here by construction — this arm
        // runs BECAUSE the window left fullscreen. The other two may have
        // retained a claim through the hold, and this is their repair.
        return claim != Claim::WindowedFullscreen;
    case ClaimScope::Teardown:
        return true;
    }
    return false;
}

/// Teardown release order, lowest first.
///
/// Windowed fullscreen goes BEFORE either maximize claim. Both maximize
/// releases skip a window that still holds (or has requested) fullscreen, and
/// on X11 setFullScreen(false) has already landed by the time the next claim
/// runs — so releasing fullscreen first is what lets a window holding both
/// get a real restore instead of a skip. Reversing this was a live regression
/// during PR #994's remediation.
inline constexpr int claimReleaseOrder(Claim claim)
{
    switch (claim) {
    case Claim::WindowedFullscreen:
        return 0;
    case Claim::MonocleMaximize:
        return 1;
    case Claim::MaximizedToEdges:
        return 2;
    }
    return 3;
}

/// Whether a claim keeps its ledger entry when its release is SKIPPED because
/// the window still holds fullscreen.
///
/// The two maximize claims retain: shedding an entry whose bit was never
/// handed back strands that bit with nothing recording it is owed, and a
/// later batch or the fullscreen-exit repair can still pay it. Windowed
/// fullscreen does not retain, because its membership is shed by its caller
/// (forgetWindowedFullscreen) before the compositor half runs at all.
inline constexpr bool claimRetainsOnFullscreenSkip(Claim claim)
{
    return claim != Claim::WindowedFullscreen;
}

} // namespace PlasmaZones::ScrollDecisions
