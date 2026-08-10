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

} // namespace PlasmaZones::ScrollDecisions
