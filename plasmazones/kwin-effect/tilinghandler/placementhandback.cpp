// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The placement statement on a snapping screen: what a zone or free placement
// does to the window's KWin fullscreen and maximize, and to a tiling engine's
// claim on it, before its apply. The decision is the pure
// PlacementStatement::decide; this file gathers its inputs from the live window
// and performs the KWin writes under the usual brackets.

#include "tilinghandler.h"
#include "plasmazoneseffect/placementstatement.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <effect/effectwindow.h>
#include <window.h>

#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

PlacementStatement::Verdict TilingHandler::preparePlacement(KWin::EffectWindow* w, const QRect& rect,
                                                            PlacementStatement::Purpose purpose)
{
    if (!w || w->isDeleted() || !rect.isValid()) {
        return {};
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return {};
    }
    const QString windowId = m_effect->getWindowId(w);
    PlacementStatement::Inputs in;
    in.purpose = purpose;
    in.maximized = kw->requestedMaximizeMode() != KWin::MaximizeRestore;
    in.requestedFullScreen = kw->isRequestedFullScreen();
    in.windowedFsMember = !windowId.isEmpty() && m_effect->m_windowedFullscreenWindows.contains(windowId);
    in.engineMaximizeClaim = !windowId.isEmpty()
        && (m_monocleMaximizedWindows.contains(windowId) || m_maximizedToEdgesWindows.contains(windowId));
    // A gesture this placement does not own: the ApplySnap caller cancels its
    // own interactive move first, so a still-set flag means the user's. A
    // write would snap the window out from under the pointer, so nothing is
    // written and the deferred replay prepares again once the gesture ends.
    in.gestureLive = w->isUserMove() || w->isUserResize();
    // KWin outputs, never screen ids (F577): two virtual screens of one
    // monitor are one output, and a re-statement between them keeps state.
    in.sameOutput = KWin::effects->screenAt(rect.center()) == m_effect->windowOutput(w);
    const PlacementStatement::Verdict verdict = PlacementStatement::decide(in);

    if (!verdict.apply) {
        // A re-statement of a maximized or fullscreen window keeps that state
        // and does not move it; the placement becomes the rect it returns to.
        qCInfo(lcEffect) << "Placement of" << windowId << "into" << rect << "keeps its"
                         << (verdict.seatMaximizeRestore ? "maximize" : "fullscreen")
                         << "and seats the placement as its restore rect";
        if (verdict.seatMaximizeRestore) {
            kw->setGeometryRestore(KWin::RectF(rect));
        }
        if (verdict.seatFullScreenRestore) {
            kw->setFullscreenGeometryRestore(KWin::RectF(rect));
        }
        // Still this window's latest geometry command, like an apply that
        // bails: an older pending apply or replay must not land over it, and
        // nothing is coming to reposition a window held back from compositing.
        m_effect->beginGeometryCommand(w);
        m_effect->endRestoreSuppression(w);
    }

    if (verdict.shedWindowedFullscreen || verdict.endFullScreen || verdict.shedEngineMaximize || verdict.endMaximize) {
        qCInfo(lcEffect) << "Placement of" << windowId << "into" << rect << "hands back"
                         << (verdict.shedWindowedFullscreen ? "windowed fullscreen" : "")
                         << (verdict.endFullScreen ? "fullscreen" : "")
                         << (verdict.shedEngineMaximize ? "an engine maximize" : "")
                         << (verdict.endMaximize ? "maximize" : "");
        // Every write below moves the window synchronously on X11 and lands
        // on the placement's own rect, so the VS-crossing detectors stay
        // out; the apply that follows re-stamps the tracked screen.
        // Save/restore so the bracket nests inside an ApplySnap's.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        // Fullscreen first: KWin drops a maximize(Restore) issued while
        // fullscreen is requested. Each restore rect is seated at the
        // placement before the state goes, so the window comes back where it
        // is being placed, never on a stale rect on another output.
        if (verdict.shedWindowedFullscreen || verdict.endFullScreen) {
            kw->setFullscreenGeometryRestore(KWin::RectF(rect));
        }
        if (verdict.shedWindowedFullscreen) {
            // releaseAllClaims' shape: membership first, so a re-entry from
            // setFullScreen finds the entry gone, then the compositor half.
            m_windowedFsClearInFlight.remove(windowId);
            forgetWindowedFullscreen(windowId);
            releaseWindowedFullscreenState(windowId);
        } else if (verdict.endFullScreen) {
            applyFullScreenSuppressed(kw, false);
        }
        if (verdict.shedEngineMaximize || verdict.endMaximize) {
            kw->setGeometryRestore(KWin::RectF(rect));
        }
        if (verdict.shedEngineMaximize) {
            // The claims' own releases, which keep their ledgers and echo
            // bookkeeping right; both restore to the rect seated above.
            unmaximizeMonocleWindow(windowId);
            releaseMaximizedToEdges(windowId, w);
        } else if (verdict.endMaximize) {
            // On Wayland the committed echo of this restore arrives with the
            // suppression counter back at 0 and would read as a genuine
            // unmaximize edge, replaying a placeOut morph from the
            // full-monitor frame over the placement's own leg. Pre-writing
            // the edge tracker makes the echo take the no-edge branch, and the
            // two writes that branch skips are paid here: drop a pending
            // morph a just-prior maximize edge armed, refresh the IsMaximized
            // rule verdict.
            m_effect->m_shaderManager.noteMaximizeDemotedForSnap(w);
            m_effect->invalidateRuleCacheForStateChange(windowId);
            applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
        }
    }

    // The placement takes the window off the strip or stack it held on a
    // tiling screen: untracked now, so the late untile diff of the screen it
    // left never sees it (F344, F149, F530). Its claims were handed back above,
    // anchored here, so the funnel releases none (ClaimScope::SnapPlacement).
    // After the hand-back, which reads the ledgers the funnel scrubs. Not under
    // a live gesture: nothing was handed back, and the replay comes back here.
    if (!in.gestureLive && !windowId.isEmpty() && m_notifiedWindows.contains(windowId)) {
        cleanupAutotileTracking(windowId, ScrollDecisions::ClaimScope::SnapPlacement);
    }
    return verdict;
}

} // namespace PlasmaZones
