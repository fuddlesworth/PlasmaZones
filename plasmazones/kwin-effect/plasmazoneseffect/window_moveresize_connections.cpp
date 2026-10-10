// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window interactive move/resize wiring for PlasmaZonesEffect: the
// gesture start and finish handlers KWin fires once per drag or resize.
// Called once per window from setupWindowConnections. The resize hold and its
// drain live here too.

#include "plasmazoneseffect.h"
#include "gestureenddecisions.h"
#include "shader_internal.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>
#include <QTimer>

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"

namespace PlasmaZones {

void PlasmaZonesEffect::wireUserMoveResizeHandlers(KWin::EffectWindow* w)
{
    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
        // Open the resize hold (ResizeHold). It is latched here, at the start,
        // because KWin clears isUserResize() before windowFinishUserMovedResized
        // fires. The pre-resize frame is the neighbour-reflow report's baseline
        // (GitHub #652), and the context is what every metadata push reports
        // while an edit waits for the resize to end.
        if (window && window->isUserResize()) {
            if (m_resizeHold.window) {
                // A new gesture settles the one before it.
                drainResizeHold(m_resizeHold.window->frameGeometry().toRect());
            }
            const quint64 generation = m_resizeHold.generation + 1;
            m_resizeHold = ResizeHold{};
            m_resizeHold.generation = generation;
            m_resizeHold.window = window;
            m_resizeHold.startGeometry = window->frameGeometry().toRect();
            m_resizeHold.context = liveContextFields(window);
        }
        // window.movement.move shader transition: KWin's interactive move is
        // its own animation system (Window::moveResize via pointer drag), but
        // we layer an effect-side shader for visual feedback.
        // windowStartUserMovedResized doesn't disambiguate move from resize;
        // w->isUserResize() does — interactive resize sets it, plain move
        // leaves it false. Interactive RESIZE deliberately starts NO shader
        // event: it is a held gesture with no discrete before/after until
        // release (the compositor repaints the re-laid content live the whole
        // time), so a crossfade pack has nothing meaningful to play, and the
        // soft-body sim omits KWin's resize edge-lock logic (mesh_sim.cpp) so
        // the move-physics packs have no real story there either. Discrete
        // resizes are covered by the placeIn / placeOut / layoutSwitch events.
        // tryBeginShaderForEvent silently no-ops if the user didn't assign a
        // shader to the path.
        if (window && !window->isUserResize()) {
            tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMove, animationDurationMs());
            // Genuine old-content capture for cross-fade legs: the drag
            // begins with the window ALIVE and its pre-drag content still
            // current, so a move pack that declares uOldWindow gets a real
            // decorated snapshot to fade FROM — matching the drag-snap morph
            // path — instead of leaning on the iHasOldWindow fallback (which
            // collapses the old side to the live content). The !oldSnapshot
            // guard preserves an existing capture on a retargeted transition,
            // mirroring window_geometry_apply; a failed capture clears needsSnapshot and
            // the shader-side fallback covers it.
            // `heldMove`, NOT liveness. window.movement.move is opt-in with no
            // default shader, so the stock config installs nothing here and
            // findTransition would hand back an unrelated leg — most reachably the
            // window.focus leg the click that began this drag installed moments ago.
            // Pinning THAT at progress 1, bumping its generation (killing its
            // teardown timer) and ramping it 1→0 on release plays the focus
            // animation backward after the drop; a maximize pack that declares
            // iFromRect would freeze the window at its pre-drag rect for the whole
            // drag. See ShaderTransition::heldMove.
            if (auto* st = m_shaderManager.findTransition(window); st && st->cached && st->heldMove) {
                if (st->cached->iOldWindowLoc >= 0 && !st->oldSnapshot) {
                    st->needsSnapshot = true;
                }
                // Anchor iFromRect at the grab frame for rect-driven packs.
                // Under the opt-in `move` class, only a pack declaring BOTH
                // move and geometry can reach this leg with iFromRect
                // declared (pure crossfade packs are refused by the
                // resolvedShaderAppliesToEvent gate, and wobble reads no
                // rects), but the anchor keeps such a hybrid correct:
                // unseeded, rect-driven packs derive their drawn rect from
                // iFromRect unconditionally, so the first `durationMs` of the
                // drag would play mix(0-rect, live, t) — the window sweeping
                // in from the screen origin. Seeded at the grab, the ramp is
                // a short catch-up ease toward the live frame and the pinned
                // tail (progress held at 1) draws the live rect exactly as
                // before. The !isValid guard preserves a retargeted
                // transition's original anchor.
                if (st->cached->iFromRectLoc >= 0 && !st->fromGeometry.isValid()) {
                    st->fromGeometry = window->frameGeometry();
                }
                // Re-grab during a release leg: resume from the current
                // (descending) progress rather than snapping back to pinned-1.
                // Freeze the accrued down-ramp and hand it to the decaying
                // re-grab offset, which paintWindow subtracts from the painted
                // progress and ramps to 0 over durationMs. startTimeMs is left
                // ALONE on purpose — rewinding it cannot reconstruct the
                // resumed value once iTime is curve-eased, and does nothing at
                // all for a stateful spring. See ShaderTransition::regrabStartMs.
                // A fresh grab (releaseStartMs still -1) skips this and keeps
                // its normal ramp.
                if (st->releaseStartMs >= 0 && st->durationMs > 0) {
                    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
                    const qreal downP = qMax<qreal>(0.0, qreal(nowMs - st->releaseStartMs) / qreal(st->durationMs));
                    st->regrabDownOffset = qMin<qreal>(1.0, downP);
                    st->regrabStartMs = nowMs;
                    st->releaseStartMs = -1;
                }
                // HELD transition: the drag is open-ended, so the shader
                // stays active (progress clamped at 1) until the release
                // handler below schedules the settle-tail teardown; the
                // duration timer stands down for held transitions. The
                // grab origin anchors iMoveOffset, and the velocity spring
                // integrates from here (see the paint pipeline).
                st->holdUntilRelease = true;
                // Fresh epoch for the (re-)hold: a re-grab inside the prior
                // drag's settle window rides the same-effect short-circuit
                // (beginShaderTransition installs nothing new), so the prior
                // release's tail / safety-cap timer still carries this
                // transition's generation and would fire mid-drag, killing
                // the shader for the rest of the new drag. Bumping here
                // invalidates it. For a fresh install the bump is harmless:
                // the just-scheduled duration timer stands down on the hold
                // flag anyway, and every later consumer captures the live
                // generation at its own schedule time.
                st->generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
                st->grabOrigin = window->frameGeometry().topLeft();
                st->lastMovePos = st->grabOrigin;
                st->lastMoveSampleMs = -1;
                // Seed the generic soft-body lattice (iMoveMesh) so a
                // mesh-consuming pack (wobble, ...) gets neighbour-coupled
                // physics from the first frame. The grip is the node
                // nearest the cursor at grab; physics constants use KWin's
                // middle preset (per-pack tuning can layer on later). A
                // re-grab while the previous release is still ringing out
                // (this leg kept by the same-effect short-circuit) keeps the
                // deformation and moves only the grip; seeding it flat jumped
                // the sheet to a flat rect on the re-grab frame.
                if (st->cached->iMoveMeshLoc >= 0 && KWin::effects) {
                    if (st->meshSim.initialized && !st->meshSim.settled) {
                        ShaderInternal::regripMeshSim(st->meshSim, window->frameGeometry(), KWin::effects->cursorPos(),
                                                      st->meshParams);
                    } else {
                        ShaderInternal::initMeshSim(st->meshSim, window->frameGeometry(), KWin::effects->cursorPos(),
                                                    st->meshParams);
                    }
                }
            }
        }
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        // Release a HELD move transition with a settle tail (interactive
        // resize starts no shader transition, see the start handler): the
        // velocity spring decays through zero over the next fraction of a
        // second, letting wobble/tilt shaders relax to rest before the
        // teardown lands. Generation-guarded exactly like the duration
        // timer so an interrupting transition owns its own lifetime.
        if (window) {
            // `heldMove &&` guards the mirror of the drag-start defect: releasing
            // must only ever act on the leg the drag itself installed, never on
            // whatever is live. holdUntilRelease alone is not that test — it is the
            // flag the old drag-start bug wrongly set on an unrelated leg.
            if (auto* st = m_shaderManager.findTransition(window); st && st->heldMove && st->holdUntilRelease) {
                QPointer<KWin::EffectWindow> safeWindow(window);
                if (st->meshSim.initialized) {
                    // Soft-body lattice: hand teardown to the settle gate.
                    // Clearing holdUntilRelease drops the transition into
                    // "active while the lattice still has energy" mode (see
                    // the paint pipeline), so the wobble rings out for as
                    // long as it physically takes rather than a fixed tail.
                    // The timer is only a generous SAFETY cap in case the
                    // sim never reaches its settle threshold.
                    //
                    // Fresh epoch for the handoff: the start-scheduled
                    // duration timer in tryBeginShaderForEvent captured the
                    // install generation and only stands down while
                    // holdUntilRelease is set. On a drag SHORTER than the
                    // nominal duration that timer fires after this clear,
                    // sees a matching generation, and would cut the ring-out
                    // off mid-settle — so bump the generation to invalidate
                    // it. The paint pipeline's expiry teardown captures the
                    // live generation at queue time, so the settle gate and
                    // the safety cap below both own the new epoch.
                    st->holdUntilRelease = false;
                    st->generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
                    const quint64 myGeneration = st->generation;
                    constexpr int kMeshSettleSafetyCapMs = 4000;
                    QTimer::singleShot(kMeshSettleSafetyCapMs, this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                } else if (st->releaseStartMs < 0) {
                    // Velocity / trail packs: the springLag decays over the
                    // next fraction of a second, so keep the fixed tail.
                    // holdUntilRelease stays SET here, so the start-scheduled
                    // duration timer keeps standing down and this tail timer
                    // (guarded on the install generation) owns the teardown.
                    // Stamp the release leg: paintWindow ramps the pinned
                    // progress back toward 0 from this moment. The ramp is
                    // scaled by the transition's OWN durationMs (a per-event
                    // duration or an OverrideAnimationTiming rule can differ
                    // from the global default), so the tail timer must grant
                    // exactly that many ms — a shorter tail would tear down
                    // mid-ramp and snap, the artifact the release leg exists
                    // to prevent. The releaseStartMs < 0 guard on this branch
                    // makes a duplicate finish signal a no-op instead of
                    // restarting the ramp and double-scheduling teardown.
                    //
                    // Fold any in-flight RE-GRAB offset into this release rather
                    // than leaving both live. A release during a still-decaying
                    // re-grab leaves paintWindow subtracting two offsets whose
                    // slopes are equal and opposite (+1/durationMs and
                    // -1/durationMs), so they cancel and the progress FREEZES on a
                    // plateau until the re-grab offset expires — the dissolve
                    // visibly stalls before it starts. Rebasing releaseStartMs by
                    // the residual makes `down` start at exactly that residual, so
                    // the ramp is continuous at this frame and descends at the
                    // normal rate; the tail is shortened to match so the teardown
                    // timer still lands when the ramp reaches 0 rather than cutting
                    // it mid-flight.
                    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
                    qreal residual = 0.0;
                    if (st->regrabStartMs >= 0 && st->durationMs > 0) {
                        residual = qBound<qreal>(
                            0.0, st->regrabDownOffset - qreal(nowMs - st->regrabStartMs) / qreal(st->durationMs), 1.0);
                        st->regrabStartMs = -1;
                        st->regrabDownOffset = 0.0;
                    }
                    st->releaseStartMs = nowMs - qint64(residual * st->durationMs);
                    const quint64 myGeneration = st->generation;
                    const int rampMs = qMax(1, qRound((1.0 - residual) * st->durationMs));
                    QTimer::singleShot(rampMs, this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                }
            }
        }
        const bool wasResize = (window && m_resizeHold.window == window && !m_resizeHold.finished);
        if (wasResize) {
            // The end-of-resize work runs from the frame the client commits for
            // the size it was dragged to. KWin does not wait for that commit at
            // the end of an interactive resize on Wayland, so when the frame
            // does not answer the request yet, the drain waits for it, for a
            // newer command, or for the deadline (F701).
            KWin::Window* const kw = window->window();
            const QRectF requested = kw ? QRectF(kw->moveResizeGeometry()) : QRectF();
            m_resizeHold.finished = true;
            m_resizeHold.requestedSize = requested.size();
            m_resizeHold.commandStampAtFinish = m_daemonGate.commandStamps.current(window);
            if (!requested.isValid()
                || GestureEndDecisions::frameAnswersRequest(window->frameGeometry().size(), requested.size())) {
                drainResizeHold(window->frameGeometry().toRect());
            } else {
                armResizeAckWatch(window);
            }
        } else if (window && !window->isDeleted() && shouldHandleWindow(window)) {
            // The free geometry the effect remembers follows a hand placement,
            // so a later desktop move or float-back returns to where the user
            // left it.
            m_tilingHandler->noteFreeGeometryAfterGesture(window, /*resized=*/false);
        }
        m_dragTracker->handleWindowFinishMoveResize(window);
        // Now that the COMPOSITOR's move is over (this signal, not forceEnd,
        // is when compositorMoveResizeActive() clears), re-drive the pill
        // hover: the dragStopped re-drive fires on LMB release and is
        // suppressed while KWin still holds the move for other buttons, so a
        // multi-button drop onto the pill band would otherwise stay unlit
        // until the next pointer twitch.
        // KWin::effects, not m_tilingHandler: cursorPos() needs the former, while
        // the latter is constructed with the effect and outlives every window
        // connection — the tail call below dereferences it unguarded, as does
        // the rest of this file.
        if (KWin::effects) {
            m_tilingHandler->updateScrollTabHover(KWin::effects->cursorPos());
        }
        // A maximize claim taken during the gesture was never paid: the batch
        // arms insert membership and then skip the compositor call while the
        // user is dragging, and nothing re-drives them — this lambda replays
        // geometry only, and its two GEOMETRY REPORTS are gated on wasResize,
        // so a MOVE end reports nothing at all. (Other calls in this lambda do
        // run unconditionally; the claim is about the geometry path.) The
        // engine emits on change, so a drag that leaves the strip alone
        // schedules no batch either. This is the
        // one point that always runs at the end of a gesture.
        m_tilingHandler->reconcileMaximizeAfterGesture(window);
    });
}

void PlasmaZonesEffect::armResizeAckWatch(KWin::EffectWindow* w)
{
    const QPointer<KWin::EffectWindow> safeW = w;
    disconnect(m_resizeHold.ackWatch);
    m_resizeHold.ackWatch =
        connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW](KWin::EffectWindow*) {
            if (!safeW || m_resizeHold.window != safeW) {
                return;
            }
            if (GestureEndDecisions::frameAnswersRequest(safeW->frameGeometry().size(), m_resizeHold.requestedSize)
                || m_daemonGate.commandStamps.current(safeW) != m_resizeHold.commandStampAtFinish) {
                drainResizeHold(safeW->frameGeometry().toRect());
            }
        });
    // A client that never answers (a suspended one) must not hold the window:
    // at the deadline the drain reads the size KWin asked for.
    QTimer::singleShot(GestureEndDecisions::kAckDeadlineMs, this, [this, safeW, gen = m_resizeHold.generation]() {
        if (!safeW || m_resizeHold.window != safeW || m_resizeHold.generation != gen) {
            return;
        }
        const KWin::Window* const kw = safeW->window();
        const QRectF requested = kw ? QRectF(kw->moveResizeGeometry()) : QRectF();
        drainResizeHold(requested.isValid() ? requested.toRect() : QRectF(safeW->frameGeometry()).toRect());
    });
}

void PlasmaZonesEffect::drainResizeHold(const QRect& frame)
{
    KWin::EffectWindow* const w = m_resizeHold.window.data();
    const QRect start = m_resizeHold.startGeometry;
    const quint8 heldAxes = m_resizeHold.heldAxes;
    disconnect(m_resizeHold.ackWatch);
    // Released FIRST: the bodies below must not see a hold.
    const quint64 generation = m_resizeHold.generation + 1;
    m_resizeHold = ResizeHold{};
    m_resizeHold.generation = generation;
    if (!w || w->isDeleted()) {
        return;
    }
    // Where the resize left the window, settled once (F486). At the deadline
    // the committed frame is still the old size, so the screen is read from
    // the frame the drain was handed.
    const QString windowId = getWindowId(w);
    QString after;
    if (frame == QRectF(w->frameGeometry()).toRect()) {
        after = pendingWindowScreenId(w);
    } else {
        const QString scrollTracked =
            m_tilingHandler->hasScrollingScreens() ? m_tilingHandler->scrollTrackedScreenFor(windowId) : QString();
        const QPoint centre = frame.center();
        const KWin::LogicalOutput* const out = KWin::effects ? KWin::effects->screenAt(centre) : nullptr;
        after = scrollTracked.isEmpty() ? resolveEffectiveScreenId(centre, out ? out : windowOutput(w)) : scrollTracked;
    }
    const QString before = m_trackedScreenPerWindow.value(w);
    const auto crossing = GestureEndDecisions::classify(before, after);
    if (crossing != GestureEndDecisions::Crossing::None) {
        m_trackedScreenPerWindow[w] = after;
        reportActiveWindowScreen(w, after);
        m_screenChangeHandler->applyGestureEndCrossing(w, before, after);
    }
    // The context edits KWin made during the resize (F665), after the
    // crossing, each pushing the context it left first.
    if (heldAxes & ResizeHold::DesktopAxis) {
        runContextEdit(w, ResizeHold::DesktopAxis);
    }
    if (heldAxes & ResizeHold::ActivityAxis) {
        runContextEdit(w, ResizeHold::ActivityAxis);
    }
    if (heldAxes != 0) {
        invalidateRuleCacheForStateChange(windowId);
    }
    if (!shouldHandleWindow(w)) {
        return;
    }
    // A floating window the user just RESIZED has a new free size. Persist it
    // immediately into the unified record's shared free geometry (overwrite=true)
    // so the float-back is durable right away — recordFreeGeometry marks the
    // placement store dirty, arming the debounced save. The save-time sweep only
    // folds the live frame shadow into the record on the next dirtying event /
    // shutdown, and a bare resize never marks anything dirty, so without this the
    // new size could be lost on an unclean exit. Resizes never snap, so this can
    // never race the drag→snap pipeline (which owns the move case); guarding on
    // isWindowFloating keeps it to genuinely floated windows.
    if (!windowId.isEmpty() && isWindowFloating(windowId)) {
        // toRect() (rounding) rather than truncation: fractional-scale
        // outputs leave sub-pixel residue in frameGeometry(), and the
        // other geometry-capture paths round too. Correct for
        // maximize/fullscreen (freeGeometryForCapture) so maximizing a
        // floating window does not clobber its free-float size with the
        // full-monitor rect (this store uses overwrite=true).
        const QRect geom = freeGeometryForCapture(w, QRectF(frame)).toRect();
        if (geom.width() > 0 && geom.height() > 0) {
            PhosphorProtocol::ClientHelpers::fireAndForget(
                this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
                {windowId, geom.x(), geom.y(), geom.width(), geom.height(), getWindowScreenId(w),
                 /*overwrite=*/true},
                QStringLiteral("storePreTileGeometry - float resize"));
        }
    }
    // Report the committed resize to the daemon so it can reflow tiled
    // neighbours (GitHub #652), unless it ended on another screen, whose tiles
    // it does not belong to. The daemon ignores floating / untracked windows,
    // so this is harmless for the float case handled just above, and it
    // re-validates membership before reflowing.
    if (GestureEndDecisions::reportsResize(crossing)) {
        notifyWindowResized(w, start, frame);
    }
    m_tilingHandler->noteFreeGeometryAfterGesture(w, /*resized=*/true);
}

} // namespace PlasmaZones
