// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window output wiring for PlasmaZonesEffect: the cross-output move
// (KWin outputChanged), the virtual-screen crossing detector that runs off
// frame geometry because outputChanged cannot see a crossing inside one
// monitor, the tracked-screen stamp both diff against, the flags-settle
// re-evaluation (eviction or adoption) and the rule-field flag edges. Called once per window from
// setupWindowConnections. The
// crossing bodies live in ScreenChangeHandler, which also replays a crossing
// deferred during a screen change at its settle.

#include "plasmazoneseffect.h"
#include "gestureenddecisions.h"
#include "windowcontexthold.h"
#include "windowflagedges.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"
#include "tilinghandler/tilinghandler.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

#include <utility>

namespace PlasmaZones {

void PlasmaZonesEffect::wireOutputChangeHandlers(KWin::EffectWindow* w)
{
    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and report the move, whose meaning for a snapped window is the daemon's (reportCrossing).
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens). Where
        // it is going, when a move is in flight: a window wired by a reloaded
        // effect between a move request and its ack sits on the output it is
        // leaving.
        m_trackedScreenPerWindow[w] = pendingWindowScreenId(w);
        // Flags-settle backstop: a client can set keep-above,
        // skip-switcher or its transient parent AFTER mapping (Yakuake
        // queues the first two in its map-time request burst; another client
        // may flip one seconds later). Each of those flips a structural
        // placement filter, and without these the map-time tileability
        // verdict was permanent — the pre-settle window got inserted,
        // focused and column-sized. The one-tick routing defer in
        // slotWindowAdded harvests the same-burst case before any insert;
        // these catch the late case both ways: a window that stops being
        // tileable is released, and one that becomes tileable on a managed
        // screen in view is tiled at once (late_tileable_adopt, F991).
        //
        // transientChanged / modalChanged are the arms the keep-above pair
        // could not reach: on Wayland an xdg_toplevel's set_parent and
        // set_modal arrive as their own requests after the initial commit,
        // so a dialog can map as a parentless normal toplevel and only
        // become transient a beat later. Both are structural rejects in
        // shouldHandleWindow and isTileableWindow, and window TYPE has no
        // signal of its own, so transientChanged is also the only handle on
        // the isDialog() reject for clients whose dialog type KWin derives
        // from the transient relationship.
        //
        // What this does NOT cover, so nobody re-derives it from the
        // Yakuake bug report: a dialog whose parent toplevel is destroyed
        // BEFORE the dialog maps. Measured live 2026-08-30 — Yakuake's
        // dropdown closed 32 ms before its First Run dialog arrived, so
        // transientFor() was null permanently rather than late, and the
        // dialog presented as a plain normal toplevel (resizable,
        // unbounded maxSize, not special, not modal) that KWin never
        // revises. No signal fires because no state changes, so neither
        // these arms nor a longer settle defer can catch it; an Exclude
        // rule is the only lever.
        //
        // Each flag is also a rule field on both ends: the daemon's registry
        // copy is refreshed by the push, and the effect's verdicts are dropped
        // except for keep-above, keep-below and the decoration, whose
        // invalidation would re-assert or re-read PlasmaZones' own write
        // (windowflagedges.h). The eviction runs first, as it always did, and
        // the push before the invalidation, so a flush that re-resolves reads
        // the field the daemon now holds. The exclusion verdicts go before all
        // of it, synchronously: the eviction and the adopt read them, and the
        // coalesced invalidation lands a turn later (F1067).
        const auto flagEdge = [this, safeW](WindowFlagEdges::Flag flag) {
            KWin::EffectWindow* const window = safeW.data();
            if (!window || window->isDeleted()) {
                return;
            }
            evictExclusionVerdicts(getWindowId(window));
            const WindowFlagEdges::Actions actions = WindowFlagEdges::actionsFor(flag);
            if (actions.reevaluateEligibility) {
                m_tilingHandler->reevaluateWindowEligibility(window);
            }
            if (actions.pushMetadata) {
                pushWindowMetadata(window);
            }
            if (actions.invalidateRules) {
                invalidateRuleCacheForStateChange(getWindowId(window));
            }
        };
        using WindowFlagEdges::Flag;
        connect(kw, &KWin::Window::keepAboveChanged, this, [flagEdge](bool) {
            flagEdge(Flag::KeepAbove);
        });
        connect(kw, &KWin::Window::keepBelowChanged, this, [flagEdge](bool) {
            flagEdge(Flag::KeepBelow);
        });
        connect(kw, &KWin::Window::skipTaskbarChanged, this, [flagEdge]() {
            flagEdge(Flag::SkipTaskbar);
        });
        connect(kw, &KWin::Window::skipPagerChanged, this, [flagEdge]() {
            flagEdge(Flag::SkipPager);
        });
        connect(kw, &KWin::Window::skipSwitcherChanged, this, [flagEdge]() {
            flagEdge(Flag::SkipSwitcher);
        });
        connect(kw, &KWin::Window::transientChanged, this, [flagEdge]() {
            flagEdge(Flag::Transient);
        });
        connect(kw, &KWin::Window::modalChanged, this, [flagEdge]() {
            flagEdge(Flag::Modal);
        });
        // KWin emits this on every min or max size change of a Wayland client,
        // whether or not the answer moved, so only a real change is an edge
        // (F1066).
        connect(kw, &KWin::Window::maximizeableChanged, this,
                [flagEdge, last = kw->isMaximizable()](bool maximizable) mutable {
                    if (std::exchange(last, maximizable) != maximizable) {
                        flagEdge(Flag::Maximizable);
                    }
                });
        connect(kw, &KWin::Window::decorationChanged, this, [flagEdge]() {
            flagEdge(Flag::Decoration);
        });
        // The declared minimum reaches the engine as a FRAME size, the client's
        // minimum with the decoration around it, so a decoration change moves
        // it, and the effect flips decorations itself (the title-bar rules).
        // The empty-cache test spares a session with no tiling engine the id
        // lookup (F105).
        connect(kw, &KWin::Window::decorationChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_lastReportedMinSize.isEmpty()) {
                return;
            }
            m_tilingHandler->noteMinSizeDecorationChanged(safeW.data(), getWindowId(safeW.data()));
        });
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // A window being resized stays on the screen it started on until
            // the resize ends, where drainResizeHold settles it once (F486):
            // KWin moves its output by the frame centre on every resize step.
            // The stamp is not written either, so a resize that crosses and
            // comes back has crossed nothing. A move tail is held the same way
            // until drainMoveTail (F751).
            if (m_resizeHold.window == safeW || m_dragActivation.tail.window == safeW) {
                return;
            }
            // A client that grew or shrank in place has not moved (F1007, the
            // output twin of F248): KWin re-picks the output by the frame's
            // centre, which an oversized frame can carry over the edge. The
            // window stays where KWin put it and on the screen it was tracked
            // on, as the virtual-screen arm below does.
            if (!safeW->isUserMove() && m_sizeOnlyFrames.value(safeW) == QRectF(safeW->frameGeometry())) {
                return;
            }
            // PlasmaZones' own move landed on an output showing another desktop:
            // the window joins that desktop as KWin's Move to Screen would (F161).
            // Ahead of the apply gate below, since an X11 or same-size move lands
            // inside the apply. The edit it emits waits until this lambda's exit,
            // after the crossing body, so the arms see the crossing's result (F717).
            // Inside an apply it waits a turn more: the daemon apply's own
            // bookkeeping after the commit (the snapped mark the arrival arm's
            // owed-placement pay reads) has not run yet.
            const bool reDesked = reDeskPzCrossing(safeW);
            const auto pzCrossingDone = qScopeGuard([this, safeW, reDesked, inApply = m_daemonGate.inGeometryApply]() {
                if (!reDesked || !safeW) {
                    return;
                }
                if (inApply) {
                    QTimer::singleShot(0, this, [this, safeW]() {
                        if (safeW) {
                            releaseContextHold(safeW, WindowContextHold::PzCrossing);
                        }
                    });
                    return;
                }
                releaseContextHold(safeW, WindowContextHold::PzCrossing);
            });
            // The ack of a move KWin re-desked the window for before the client
            // committed it (F650): an edit held meanwhile runs at this lambda's
            // exit, after the crossing notice below, or waits on for the settle
            // when the crossing is deferred. An intermediate ack of a move KWin
            // has since replaced keeps holding.
            const auto inFlightDone = qScopeGuard([this, safeW]() {
                const KWin::Window* const win = safeW ? safeW->window() : nullptr;
                if (win && win->moveResizeOutput() == win->output()) {
                    releaseContextHold(safeW, WindowContextHold::OutputInFlight);
                }
            });
            // The window's screen, and where it is going when this output
            // change is the ack of a move KWin has since been asked to
            // replace: a held "move to output" key (or a move reversed inside
            // one round trip) acks the intermediate output after the daemon
            // stored the final one, and reading the frame unsnapped the window
            // there (see pendingWindowScreenId). A strip tile KWin moved to
            // another output itself answers by position (F738).
            const QString newScreenId = m_tilingHandler->isKWinMoveOfStripTile(safeW, getWindowId(safeW))
                ? getWindowScreenId(safeW, QString())
                : pendingWindowScreenId(safeW);
            // The daemon's focused window moved: report it (see
            // reportActiveWindowScreen). Ahead of the apply gate below on
            // purpose: a move the daemon drives is still a move of the focused
            // window, and the screen answers from the engine for a strip tile,
            // so a parked column crossing outputs does not flip the record (a
            // window a snap placement took off its strip is untracked by then
            // and answers by position, and so does one KWin moved itself).
            // Not onto KWin's placeholder output, which the daemon has no
            // screen for (F729).
            if (const KWin::Window* const win = safeW->window();
                !win || !win->moveResizeOutput() || !win->moveResizeOutput()->isPlaceholder()) {
                reportActiveWindowScreen(safeW, newScreenId);
            }
            // Daemon-driven geometry applies must not be mistaken for user
            // moves (symmetric with the frameGeometryChanged VS-crossing
            // handler below): the apply's own pre-seed and bookkeeping place
            // the window. A parked strip column whose frame centre lands on
            // the neighbouring output is not what this guards: the screen
            // answers from the engine for a strip tile (above), so that hop
            // reads as no crossing either way. The tile apply tracks a daemon
            // arrival this hides itself (TilingHandler::adoptsDaemonArrival).
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;
            // During a screen change the crossing waits for the settle. KWin
            // moves every window off an output that goes away, and back onto
            // one that returns, and neither is the user's move: the settle
            // reports what KWin moved to the daemon, which floats an evacuee
            // where it landed or re-seats a returned one in its parked place,
            // and replays only the crossings it calls user moves. Nothing is
            // re-homed and nothing is skipped (the old involuntary-move skip
            // lost a genuine move made during the debounce, and misread every
            // move off a split monitor as involuntary). Outside one, the body
            // runs at once and the daemon hears of every crossing that is the
            // window's own, whichever modes the two screens run.
            if (m_screenChangeHandler->isScreenChangeInProgress()) {
                m_screenChangeHandler->deferCrossing(safeW, oldScreenId);
                // A held edit waits with the crossing, released by its replay.
                if (const auto hold = m_contextHold.holds.find(safeW.data());
                    hold != m_contextHold.holds.end() && m_screenChangeHandler->crossingAwaitsSettle(safeW)) {
                    hold->reasons |= WindowContextHold::SettlePending;
                }
                return;
            }
            m_screenChangeHandler->applyOutputCrossing(safeW, oldScreenId, newScreenId);
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing()
        // (<PhosphorIdentity/VirtualScreenId.h>) — the same predicate used by
        // tilinghandler/tiling.cpp.
        const auto onFrameChanged = [this, safeW](KWin::EffectWindow*, const QRectF& oldGeometry) {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // An edit held for a move to another output, which KWin then took
            // back onto the output the window is committed to: no outputChanged
            // comes for it, so its release is here. A move that lands emits
            // outputChanged right after this signal and releases there.
            if (const auto hold = m_contextHold.holds.constFind(safeW.data());
                hold != m_contextHold.holds.cend() && (hold->reasons & WindowContextHold::OutputInFlight)) {
                const KWin::Window* const win = safeW->window();
                if (win && win->output() == hold->committedAtHold && win->moveResizeOutput() == win->output()) {
                    releaseContextHold(safeW, WindowContextHold::OutputInFlight);
                }
            }
            // KWin emits this before outputChanged in the same commit
            // (WaylandWindow::updateGeometry), so the output arm reads this
            // record for the change that moved the output.
            if (GestureEndDecisions::isSizeOnlyChange(oldGeometry, QRectF(safeW->frameGeometry()))) {
                m_sizeOnlyFrames.insert(safeW, QRectF(safeW->frameGeometry()));
            } else {
                m_sizeOnlyFrames.remove(safeW);
            }
            // Rule verdicts owed by a daemon move to another screen, dropped once
            // the frame lands there: inside the apply on X11, at the client's ack
            // on Wayland (F160, F333). Ahead of the returns below, which both of
            // those reach. The coalesced flush resolves after the apply returns.
            if (const auto owed = m_ruleScreenOwed.constFind(safeW.data());
                owed != m_ruleScreenOwed.constEnd() && getWindowScreenId(safeW) == *owed) {
                m_ruleScreenOwed.erase(owed);
                const QString windowId = getWindowId(safeW);
                if (m_dragTracker->isDragging()) {
                    m_dragSuppressedRuleInvalidations.insert(windowId);
                } else {
                    invalidateRuleCacheForStateChange(windowId);
                }
            }
            if (m_virtualScreenDefs.isEmpty() || !m_daemonGate.virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applyWindowGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            // Held through a resize or a move tail, like the output arm.
            if (m_resizeHold.window == safeW || m_dragActivation.tail.window == safeW) {
                return;
            }
            // A client that only changed its size has not moved (F248): an
            // oversized ack of a snap apply grows the frame from the zone's
            // corner, and an application resizing itself does the same, and
            // either can carry the centre over a virtual-screen edge.
            if (!safeW->isUserMove()
                && GestureEndDecisions::isSizeOnlyChange(oldGeometry, QRectF(safeW->frameGeometry()))) {
                return;
            }
            // Pending-aware like the output arm: a frame change that acks a
            // superseded move must not read as a crossing to where the window
            // is no longer going.
            const QString newScreenId = pendingWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;
            // A virtual-screen crossing moves the focused window between the
            // screens the daemon's shortcuts act on, exactly like an output
            // change, and outputChanged never fires for one.
            reportActiveWindowScreen(safeW, newScreenId);

            // Inside a screen change the virtual-screen definitions may still
            // describe the old layout (they refresh on the daemon's replies),
            // so the crossing is deferred and resolved against the new ones
            // at the settle.
            if (m_screenChangeHandler->isScreenChangeInProgress()) {
                m_screenChangeHandler->deferCrossing(safeW, oldScreenId);
                return;
            }
            m_screenChangeHandler->applyVirtualScreenCrossing(safeW, oldScreenId, newScreenId);
        };
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, onFrameChanged);

        // Clean up the tracked screen entry when the window is destroyed. Capture the RAW
        // pointer value, not the QPointer: inside a destroyed() slot the QPointer has already
        // been nulled, so removing `safeW` would remove the null key and leave the real entry
        // to leak. The pointer is only ever a lookup key here, never dereferenced, so its
        // value is exactly what remove() needs.
        KWin::EffectWindow* const rawW = safeW;
        connect(safeW, &QObject::destroyed, this, [this, rawW]() {
            m_trackedScreenPerWindow.remove(rawW);
            m_sizeOnlyFrames.remove(rawW);
            m_ruleScreenOwed.remove(rawW);
        });
    }
}

} // namespace PlasmaZones
