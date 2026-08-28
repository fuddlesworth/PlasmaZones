// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
        d.action = clearInFlight ? WfsAction::None : WfsAction::Adopt;
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
/// Deliberately has NO in-flight marker, unlike its windowed-fullscreen
/// sibling above. That one needs one because its dispatch (clearWindowedFull-
/// screen) drops effect-side membership at once, which opens a window where a
/// pre-clear batch still carrying flag=true would take the Adopt arm and
/// re-fullscreen the window the user just exited.
///
/// The maximize interception never speculatively changes membership: it
/// cancels KWin's unilateral flip back to whatever the engine last said and
/// dispatches a TOGGLE, leaving the engine to name the result. So during the
/// round trip effect-side state still agrees with the pre-toggle flag, a
/// stale batch resolves to None on its own terms, and there is nothing for a
/// marker to guard. A toggle is also not idempotent, so a marker that
/// suppressed one arm could not be applied symmetrically anyway.
///
/// Exercised on a live compositor: sixteen consecutive compositor-driven
/// maximize and restore edges each converged to the requested state, in both
/// directions, with no echo re-dispatching a second toggle.
///
/// @p flagOnWire     the batch entry's columnMaximized
/// @p inSet          effect-side membership (m_columnMaximizedWindows)
/// @p kwinMaximized  whether KWin holds MaximizeFull. The batch arm passes
///                   requestedMaximizeMode(), not the committed maximizeMode():
///                   the committed bit trails a client round trip on Wayland,
///                   and reading it re-resolves to Apply on every batch that
///                   lands inside that window. The interception passes the
///                   COMMITTED mode instead, because there it is comparing
///                   against what actually landed.
inline MaximizeAction resolveColumnMaximizeAction(bool flagOnWire, bool inSet, bool kwinMaximized)
{
    if (!flagOnWire) {
        return inSet ? MaximizeAction::Release : MaximizeAction::None;
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

// ── Compositor-state claims ────────────────────────────────────────────────
//
// The effect imposes three kinds of compositor state that only it can hand
// back, each tracked in its own per-window ledger. Which of them a given exit
// path releases is decided HERE, as data, rather than by whether somebody
// remembered to write the call at that site.
//
// That is the whole point: a missing release is an ABSENCE, so it compiles,
// tests and reviews clean, and what it leaves behind is a window holding
// compositor state with nothing recording the debt. PR #994 shipped a third
// claim missing eight of its releases, and the two asymmetries introduced
// while fixing those were the same shape. A table cannot forget a cell.

/// A kind of compositor state the effect imposed on a window.
enum class Claim {
    MonocleMaximize, ///< KWin maximize held for a monocle tile
    WindowedFullscreen, ///< KWin fullscreen + a keep-flag layer demotion
    ColumnMaximize, ///< KWin maximize held for a maximized scroll column
};

/// Why the effect's authority over a window is ending. Each exit path names
/// itself, and the table below says which claims answer to that name — so a
/// deliberate blank is a cell a reader can see rather than a call nobody
/// wrote.
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
    case Claim::ColumnMaximize:
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
