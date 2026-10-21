// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "paint_internal.h"
#include "compositor/effectlogging.h"
#include "compositor/windowanimator.h"
#include "handlers/dragtracker.h"
#include "handlers/instantrestoredecisions.h"
#include "handlers/screenchangehandler.h"
#include "tilinghandler/tilinghandler.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/RetargetPolicy.h>

#include <effect/effecthandler.h>
#include <wayland/surface.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <utility>

namespace PlasmaZones {

namespace {
// A pending maximize morph whose size-landing commit arrives later than this
// is considered stale (state flipped but the commit never came, e.g. an
// occluded client under the lock screen) — the morph is skipped so a much
// later unrelated resize cannot fire a bogus maximize animation.
constexpr qint64 kPendingMaximizeMorphDeadlineMs = 1000;

// Maximize-morph landing discriminator: has the frame SIZE actually moved
// away from the departure rect? A maximize/restore always changes the frame
// size, while a position-only change can apply early server-side, so the
// size delta (with a 1px tolerance for fractional-scale residue) is what
// distinguishes "the jump landed" from "still waiting on the client's
// commit". Shared by the state-changed arming site and the deferred
// windowFrameGeometryChanged completion so the threshold has one home.
bool maximizeSizeLanded(const QRectF& frame, const QRectF& departureFrame)
{
    return qAbs(frame.width() - departureFrame.width()) > 1.0 || qAbs(frame.height() - departureFrame.height()) > 1.0;
}
} // namespace

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    // Idempotency guard. Most connects below take a lambda slot, and
    // Qt::UniqueConnection cannot de-duplicate a functor (it covers
    // member-function slots only), so a second call for the same window would
    // silently double each per-window handler — every geometry change handled
    // twice, every push marshalled twice. The two callers' window sets are
    // disjoint by construction (see the member's declaration), so this never
    // fires today; it is here so that stays true when a third caller appears.
    if (m_wiredWindows.contains(w)) {
        return;
    }
    m_wiredWindows.insert(w);

    // A maximize-on (either axis) or fullscreen-on edge: KWin's restore rect is
    // the frame the window left, a free spot the daemon keeps as its float-back
    // (F215). The daemon refuses it for a window in a zone or on a tile. Nothing
    // is sent when there is no restore rect (it equals the frame, or the window
    // is a windowed-fullscreen or parked strip tile, which freeGeometryForCapture
    // answers with an invalid rect). Kept for a window kept above too, like its
    // frame shadow (F923), and for a fullscreen one, whose edge this also is.
    // Never for a tile this handler tracks, or a strip tile its fullscreen holds
    // out of the strip: the restore rect is the tile or column rect. The hold's
    // float reaches the daemon first, so its tiled refusal no longer applies and
    // only an exact managed-frame match would keep the column rect out (F1114).
    const auto pushRestoreRect = [this](KWin::EffectWindow* window) {
        if (!shouldHandleWindow(window, nullptr, /*exemptFullscreen=*/true, /*exemptOwnKeepAbove=*/true)) {
            return;
        }
        const QString windowId = getWindowId(window);
        if (m_tilingHandler->holdsFullscreenFloat(windowId)
            || (m_tilingHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId))) {
            return;
        }
        const QRect geom = freeGeometryForCapture(window, QRectF(window->frameGeometry())).toRect();
        if (windowId.isEmpty() || !geom.isValid() || geom == window->frameGeometry().toRect()
            || !m_daemonGate.serviceRegistered) {
            return;
        }
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
            {windowId, geom.x(), geom.y(), geom.width(), geom.height(), getWindowScreenId(window),
             /*overwrite=*/true},
            QStringLiteral("storePreTileGeometry - maximize restore rect"));
    };

    // Recover the compositor's move state for a window already being dragged
    // when we wired it. The start signal we connect below has come and gone for
    // such a window (effect reload or compositor restart mid-gesture), so
    // without this the tracker would report no compositor move for the rest of
    // that drag.
    m_dragTracker->noteWiredWindowMoveState(w);

    // Desktop and activity set changes (the classified edit and the stamp it
    // is diffed against) live in window_desktop_connections.cpp.
    wireContextChangeHandlers(w);

    // Cross-output and virtual-screen moves with the flags-settle
    // re-evaluation (window_output_connections.cpp), the identity and metadata
    // pushes (window_metadata_connections.cpp) and the interactive
    // move/resize pair (window_moveresize_connections.cpp). They are wired
    // here, in this order, so a connection to a signal that is also
    // connected further down keeps its place in that signal's slot order.
    wireOutputChangeHandlers(w);
    wireMetadataHandlers(w);
    wireUserMoveResizeHandlers(w);

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowMaximizedStateChanged);

    // Departure-rect capture for the maximize morph wiring below. KWin
    // guarantees windowMaximizedStateAboutToChange fires before the
    // maximize/restore geometry change (effectwindow.h documents the
    // ordering, the stock maximize script relies on it the same way, and it
    // is verified in KWin 6.7.4: both X11Window::maximize and
    // XdgToplevelWindow::maximize emit maximizedAboutToChange BEFORE assigning
    // their mode member, X11 additionally under blockGeometryUpdates),
    // so frameGeometry() here is the rect the window is leaving — the only
    // point the old rect can be read. The state-changed edge below may fire
    // with the destination geometry already applied OR still pending the
    // client's commit (see PendingMaximizeMorph); either way this capture
    // is what anchors the morph's departure. Latest-wins per window: an
    // axis-only intermediate flip overwrites the entry, which is correct —
    // the morph should depart from wherever the window actually was just
    // before the edge we act on. Erased on windowDeleted alongside
    // m_lastFullyMaximized.
    connect(w, &KWin::EffectWindow::windowMaximizedStateAboutToChange, this,
            [this](KWin::EffectWindow* window, bool, bool) {
                // isDeleted() as well as null: a departure rect captured for a
                // corpse anchors nothing (no morph is owed for a window that is
                // going away) and the entry would just wait for the
                // windowDeleted sweep.
                if (window && !window->isDeleted()) {
                    m_shaderManager.m_preMaximizeFrame.insert(window, window->frameGeometry());
                }
            });

    // The maximize edge: the rule and metadata refresh, the scrolling
    // maximize interception and the maximize morph (beginMaximizeShaderMorph,
    // which rides WindowPlaceIn / WindowPlaceOut). The TilingHandler hookup
    // above only floats a monocle member the user unmaximized by hand.
    //
    // KWin emits windowMaximizedStateChanged on every maximize-mode change,
    // axis-only ones included (Maximize Vertically / Horizontally from the
    // decoration's middle or right click, KWin's unbound shortcuts, a window
    // rule, the client). Only a change into or out of full maximize is the
    // morph's edge, so track the last fully-maximized state per window and
    // fire on those transitions only; an axis flip between two modes that
    // are not full plays nothing.
    // Seed from the LIVE maximize mode: a window already fully maximized when
    // the effect (re)loads has no entry, so its first RESTORE compared
    // false==false, read as a no-edge, and played no morph.
    //
    // No equivalent seed for m_maximizedToEdgesWindows, and that asymmetry is
    // intended. This map is an EDGE FILTER whose whole job is answering
    // "did the state change", so a missing entry is a wrong answer with no
    // way back — nothing else writes a TRUE entry (noteMaximizeDemotedForSnap
    // stamps false, what a missing entry already answers). The claim ledger is an
    // OWNERSHIP record, and an unseeded one is self-healing: the daemon's
    // first tile batch carries the flag, and the Apply arm re-inserts
    // membership for any column the engine still says is maximized. Seeding
    // it from live compositor state would also be a guess, since KWin's
    // maximize bit does not distinguish a column maximize from a user's own.
    //
    // The cost of not seeding is bounded to one click: after an effect
    // reload, the first maximize on an already-column-maximized window
    // un-maximizes and re-maximizes before the batch re-establishes the
    // record. The seed lands before that lambda is connected, which is the
    // only ordering that matters.
    if (KWin::Window* kwSeed = w->window()) {
        m_shaderManager.m_lastFullyMaximized.insert(w, kwSeed->maximizeMode() == KWin::MaximizeFull);
    }

    // Shadow-margin cache for surfaceWindowRect(). Seed from the window's
    // current rects, then refresh on every windowExpandedGeometryChanged. The
    // effect-load sweep can wire a window mid-resize (the move state recovered
    // above), when the expanded rect lags the frame; the plausibility cap can
    // admit that lagged pair, and the next windowExpandedGeometryChanged
    // corrects it. The body — and the write-side invariants: never refresh
    // from a paint-time sample, refuse implausible margins — lives beside
    // surfaceWindowRect in surfacelayers.cpp.
    refreshSurfaceShadowMargins(w);
    connect(w, &KWin::EffectWindow::windowExpandedGeometryChanged, this,
            &PlasmaZonesEffect::refreshSurfaceShadowMargins);
    connect(
        w, &KWin::EffectWindow::windowMaximizedStateChanged, this,
        [this, pushRestoreRect](KWin::EffectWindow* window, bool horizontal, bool vertical) {
            // isDeleted() as well as null. Every body below is meaningless
            // for a corpse, and one is actively harmful: the rule-cache
            // invalidation calls getWindowId, which re-populates the id
            // caches slotWindowClosed has just scrubbed (window_lifecycle
            // spells that hazard out), leaving a stale mapping for the
            // windowDeleted backstop to clean up again.
            if (!window || window->isDeleted()) {
                return;
            }
            const bool fullyMaximized = horizontal && vertical;
            const bool wasFullyMaximized = m_shaderManager.m_lastFullyMaximized.value(window, false);
            if (fullyMaximized == wasFullyMaximized) {
                // Intermediate axis-only flip, so no shader — but on a
                // scroll-managed tile the bit still has to go back.
                //
                // An axis-only maximize (Maximize Vertically / Horizontally:
                // the decoration's middle or right click, KWin's unbound
                // shortcuts, a window rule, the client) never reaches the
                // interception below, and nothing else clears it: the batch
                // arm that would only runs when a batch arrives, and the
                // engine emits on change, so a flip that moves no column
                // schedules none. The window then sits maximized on one axis
                // against the strip's rects with no correction coming. A KWin
                // quick tile is not one of these: it un-maximizes and
                // moveResizes to the tile, so it lands as a frame change (on a
                // column-maximize member, as the full-to-restore edge the
                // interception claims), and its own requestedTileChanged
                // connection below takes the tile back off.
                //
                // CANCEL ONLY, never a dispatch. Routing this through
                // interceptMaximizeRequest would dispatch a toggle,
                // turning the user's axis maximize into a column maximize
                // (or, on a member, into an un-maximize).
                //
                // The daemon's capture reads the axis state too (F65): its
                // registry must not keep the pre-edge answer, and refusing the
                // axis-maximized frame leaves it the restore rect alone to keep
                // as the float-back. Read before the cancel restores it.
                pushWindowMetadata(window);
                if (horizontal || vertical) {
                    pushRestoreRect(window);
                }
                m_tilingHandler->cancelAxisOnlyMaximize(window);
                return;
            }
            m_shaderManager.m_lastFullyMaximized.insert(window, fullyMaximized);
            // IsMaximized is a matchable rule field with the same
            // cache-key staleness as IsMinimized (see the minimizedChanged
            // metadata lambda below) — invalidate on the genuine
            // full-maximize edge, after the tracking write and before the
            // interactive-gesture early return (the verdict must refresh
            // even when the shader is skipped).
            invalidateRuleCacheForStateChange(getWindowId(window));
            // The daemon's registry holds the maximize state its float-back
            // capture reads (fillsOutputState): without a push on the edge it
            // kept the open-time value and recorded an output-sized frame
            // over a good float-back (F33).
            pushWindowMetadata(window);
            if (fullyMaximized) {
                pushRestoreRect(window);
            }
            // MAXIMIZE INTERCEPTION. On a scroll-managed tile the request
            // belongs to the scrolling engine's maximize-to-edges verb, not
            // to KWin: the strip owns the column's width, so letting both
            // answer would give one window two maximize authorities.
            // Placed AFTER the edge filter and the tracking write so it
            // sees genuine full-maximize edges only (an axis-only flip
            // must never dispatch the toggle verb, see the edge filter),
            // and after the rule-cache invalidation, which must run for the
            // IsMaximized field whoever ends up owning the state.
            //
            // The suppression check keeps this off the handler's own
            // bracketed writes; interceptMaximizeRequest additionally
            // no-ops on the Wayland-lagged echo of the refusal handler's
            // write-back, which arrives with the counter back at 0.
            //
            // A claimed request skips the shader install HERE
            // deliberately, not the maximize animation: the window still
            // resizes when the column grows, and that geometry arrives
            // through the strip's own batch, which anchors its leg at the
            // rect this press left (the claimed press interceptMaximizeRequest
            // records) and rides placeOut on a restore. A maximize morph
            // from this handler on top would supersede that leg — the same
            // reasoning as the drag-restore guard below. One owner per leg,
            // and for an engine-authored maximize the owner is the batch.
            if (!m_tilingHandler->isSuppressingMaximizeChanged() && m_tilingHandler->interceptMaximizeRequest(window)) {
                m_shaderManager.m_pendingMaximizeMorph.remove(window);
                return;
            }
            // The handler's OWN bracketed writes take the same skip, and
            // must: on XWayland maximize() emits this signal synchronously
            // with the counter still held, so the conjunct above is false
            // and control used to fall through to the shader install
            // below — the engine-authored column maximize played a second
            // placement morph on X11 and not on Wayland, where the
            // committed echo arrives with the counter at 0 and the
            // interception claims it. Same deliberate skip, now on both
            // platforms. The edge tracking and the rule-cache
            // invalidation above have already run, so nothing else is
            // lost by returning here.
            //
            // The counter is raised by every bracketed maximize write this
            // handler makes, not only the column-maximize ones, so the
            // monocle apply and release skip here too on X11. That is the
            // same judgement applied consistently: motion this effect
            // authored is animated by the batch that authored it — a
            // placement leg anchored at the captured pre-maximize rect
            // (monocleBitWritten / monocleBitReleased in the tile batch) —
            // not by a second morph replayed over it from this handler.
            //
            // On Wayland the MONOCLE echo arrives with the counter back at 0
            // and the interception above declines it (a monocle screen is not
            // scrolling); the one-shot below absorbs it instead.
            if (m_tilingHandler->isSuppressingMaximizeChanged()) {
                m_shaderManager.m_pendingMaximizeMorph.remove(window);
                return;
            }
            // ...and the Wayland MONOCLE echo, which arrives with the counter
            // back at 0, is absorbed the same way when the batch armed it
            // (ShaderTransitionManager::m_monocleEchoOwed). Without this it fell
            // through to beginMaximizeShaderMorph, whose time-driven leg never
            // matches the batch's animator-driven one in timing mode, so the
            // same-effect short-circuit could not absorb it either: the echo
            // replayed a second placement morph over the batch's leg.
            if (const auto echoIt = m_shaderManager.m_monocleEchoOwed.find(window);
                echoIt != m_shaderManager.m_monocleEchoOwed.end()) {
                const bool matchingEcho = echoIt.value() == fullyMaximized;
                m_shaderManager.m_monocleEchoOwed.erase(echoIt);
                if (matchingEcho) {
                    qCDebug(lcEffect) << "Absorbed the monocle maximize echo for" << getWindowId(window);
                    m_shaderManager.m_pendingMaximizeMorph.remove(window);
                    return;
                }
            }
            // Drag-restore guard: KWin unmaximizes a window mid interactive
            // move when the user grabs the maximized title bar and pulls
            // ("restore on drag"). The drag already owns the visuals — the
            // gesture start ended any placement leg and, when a move pack is
            // assigned, installed it as a HELD transition — and installing the
            // placement morph here would supersede it: the move pack dies
            // mid-drag and a full-screen→cursor morph replays over the
            // pointer. The isUserResize branch is skipped for a different
            // reason: an interactive resize starts NO shader (the start
            // handler gates on !isUserResize), but it is still a held
            // gesture with continuous geometry feedback, so a discrete
            // maximize morph replaying under the pointer would be just as
            // wrong. Skip the shader; the edge tracking above still ran,
            // so the next non-interactive flip fires normally.
            if (window->isUserMove() || window->isUserResize()) {
                m_shaderManager.m_pendingMaximizeMorph.remove(window);
                return;
            }
            const QRectF newFrame = window->frameGeometry();
            QRectF preFrame = m_shaderManager.m_preMaximizeFrame.value(window);
            if (preFrame.isEmpty()) {
                // No capture (window managed after the about-to-change
                // fired, or a degenerate rect). Fall back to the live
                // frame: the size test below then defers to the geometry
                // change, which still carries the real jump.
                preFrame = newFrame;
            }
            // KWin does NOT guarantee the maximize/restore geometry has
            // been applied when this state signal fires — see the
            // PendingMaximizeMorph docstring for the observed decoupling.
            // Only install here when the size has actually changed
            // (maximizeSizeLanded above); otherwise arm the pending entry
            // and let the size-delivering windowFrameGeometryChanged below
            // complete the install at the visible jump.
            if (maximizeSizeLanded(newFrame, preFrame)) {
                m_shaderManager.m_pendingMaximizeMorph.remove(window);
                // A placement leg still running: the morph departs from the
                // rect it is drawing, and the leg ends (F674).
                if (const QRectF drawn = endRunningPlacementLeg(window, /*endMaximizeLeg=*/false); drawn.isValid()) {
                    preFrame = drawn;
                }
                beginMaximizeShaderMorph(window, preFrame);
            } else {
                m_shaderManager.m_pendingMaximizeMorph.insert(window, {preFrame, ShaderInternal::shaderClockNowMs()});
            }
        });

    // KWin's quick tile of a strip tile: the strip owns the tile's rect, so the
    // tile is taken back off and the column put back
    // (TilingHandler::cancelStripQuickTile). Queued, so KWin has finished the
    // request that emitted this before the cancel issues a request of its own.
    if (KWin::Window* kwTile = w->window()) {
        const QPointer<KWin::EffectWindow> safeW = w;
        connect(
            kwTile, &KWin::Window::requestedTileChanged, this,
            [this, safeW]() {
                if (safeW) {
                    m_tilingHandler->cancelStripQuickTile(safeW.data());
                }
            },
            Qt::QueuedConnection);
    }

    // TilingHandler::slotWindowFullScreenChanged: a windowed-fullscreen
    // member's enter and exit, and for any other tiled window the enter's
    // untrack (clearWindowTiledAllScreens; monocle membership survives) and
    // the exit's re-track, decoration refresh and strip re-emit.
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowFullScreenChanged);

    // Decorations.Performance.SuppressWhileFullscreen — the enter and exit edge
    // of the gate. A SEPARATE connection rather than a line inside the tiling
    // handler's slot above: that slot is scrolling-mode machinery with several
    // early returns and a windowed-fullscreen branch, and the gate is a
    // decoration concern that has to hold in every placement mode. Connected
    // after it so the tiling handler has already reconciled its own state (and
    // shed the fullscreen window's own decoration) by the time the sweep runs.
    // refreshFullscreenSuppression re-derives the covered outputs and only
    // sweeps when the answer actually moved.
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, this, [this]() {
        refreshFullscreenSuppression();
    });
    // IsFullscreen is a matchable rule field, and the daemon's float-back
    // capture reads it through fillsOutputState: the edge refreshes the
    // window's rule verdicts and pushes its metadata, as the full-maximize
    // edge does (F33, F73).
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, this, [this, pushRestoreRect](KWin::EffectWindow* window) {
        if (!window || window->isDeleted()) {
            return;
        }
        invalidateRuleCacheForStateChange(getWindowId(window));
        pushWindowMetadata(window);
        if (window->isFullScreen()) {
            pushRestoreRect(window);
        } else {
            // A tile KWin took for a strip tile while it was fullscreen: the
            // quick-tile corrector declined it then, and KWin's
            // requestedTileChanged will not repeat (F123). After the tiling
            // handler's slot above, which re-marks a strip tile returning
            // from its own fullscreen.
            m_tilingHandler->cancelStripQuickTile(window);
        }
    });

    // The same gate's OTHER edges. A fullscreen window keeps isFullScreen()
    // true while it is minimized, sent to another desktop or activity, or moved
    // to another output, so without these the covered set stays stale and a monitor with
    // nothing on it goes on being undecorated. Each is pre-gated on the window
    // actually being fullscreen: the refresh walks the entire stacking order,
    // and the overwhelming majority of windows firing these are not fullscreen
    // and cannot move the answer. The refresh's own set comparison then makes a
    // no-change call cost one compare.
    connect(w, &KWin::EffectWindow::minimizedChanged, this, [this, w]() {
        if (w->isFullScreen()) {
            refreshFullscreenSuppression();
        }
    });
    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this, w]() {
        if (w->isFullScreen()) {
            refreshFullscreenSuppression();
        }
    });
    // Moving to another monitor changes which output the gate covers, and the
    // set is derived positionally so nothing else re-derives it. A separate
    // connection rather than a line in the placement outputChanged lambda
    // (window_output_connections.cpp) for the same reason the fullscreen one
    // is separate: that lambda is placement machinery with several early
    // returns, including one for daemon-driven applies, and the gate has to
    // hold whichever of those it takes.
    if (KWin::Window* kw = w->window()) {
        // The EffectWindow is not the sender here, so it is held weakly.
        const QPointer<KWin::EffectWindow> safeW = w;
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (safeW && safeW->isFullScreen()) {
                refreshFullscreenSuppression();
            }
        });
        // An activity move: the walk skips a window off the current activity,
        // and the global currentActivityChanged edge covers only a switch.
        connect(kw, &KWin::Window::activitiesChanged, this, [this, safeW]() {
            if (safeW && safeW->isFullScreen()) {
                refreshFullscreenSuppression();
            }
        });
    }

    // The tiling handler's frame-commit slot: the strip counter-assert against
    // an external mover, the virtual-screen crossing detector for tracked
    // tiles, and the autotile centring of a Wayland window that committed a
    // smaller size than its tile. Connected ahead of the lambda below, so a
    // re-seat the counter-assert makes is in place before the offered-column
    // read runs.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowFrameGeometryChanged);

    // A geometry request for the window outside every effect apply: KWin's
    // own (its Expand and Shrink Window verbs, a script setting the frame, an
    // axis-only maximize, its keep-visible clamp) or the user's resize. KWin
    // emits this at the top of each request and never for a client's commit,
    // so the request is decided there and then from the size KWin has just
    // stored for it. A request for another size first voids the answer an
    // armed stale ack owes (Body -1.5 below): KWin's configure for it
    // supersedes the stale one, and re-issuing the old command over it would
    // undo a resize KWin made, or one the strip puts back below
    // (ScrollDecisions::requestVoidsStaleAck, CR2-22). Then a request that
    // resizes a strip window is put back to its column, or marks its frame as
    // no longer answering the column the strip offered
    // (noteForeignStripRequest). The user-move test first of all, because
    // KWin issues a request per pointer step of an interactive move; the
    // stale-ack probe and the offered-column probe are each one hash lookup,
    // which a session with no strip and no stale ack pays.
    connect(w, &KWin::EffectWindow::windowFrameGeometryAboutToChange, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                if (!safeW || safeW->isUserMove() || m_daemonGate.inGeometryApply || safeW->isDeleted()) {
                    return;
                }
                auto& staleAcks = m_daemonGate.commandStamps.staleAcks;
                if (const auto stale = staleAcks.find(safeW.data()); stale != staleAcks.end()) {
                    if (const KWin::Window* kwStale = safeW->window(); kwStale
                        && ScrollDecisions::requestVoidsStaleAck(QRectF(kwStale->moveResizeGeometry()).size(),
                                                                 stale->target.size())) {
                        qCDebug(lcEffect) << "Stale-ack answer for" << getWindowId(safeW.data())
                                          << "superseded by a request for" << QRectF(kwStale->moveResizeGeometry());
                        staleAcks.erase(stale);
                    }
                }
                if (m_scrollOfferedColumn.isEmpty()) {
                    return;
                }
                noteForeignStripRequest(safeW.data());
            });

    // One windowFrameGeometryChanged lambda for the effect-side
    // per-tick work, in the order the bodies run: the answer to a superseded
    // configure's late ack (Body -1.5), a strip-animation retarget onto the
    // rect the client actually committed, unless that is a park (Body -1), the
    // client's answer to its strip column, reported to the engine and centred
    // in the column (Body -0.5),
    // deferred maximize completion (Body 0), first-frame suppression release
    // (Body 1), and the throttled daemon push (Body 2). Keeping the last two
    // as separate connections (which they were originally) doubled the per-geometry-
    // tick lambda dispatch cost without functional benefit; the bodies
    // are independent so collapsing them just runs one capture+vtable
    // hop per tick instead of two. The autotile-handler connection
    // immediately above is kept separate because it dispatches to a slot
    // on a different receiver (`m_tilingHandler.get()`). The virtual-screen
    // crossing detector is its own connection (window_output_connections.cpp).
    //
    // Body 1 — first-frame open suppression release: a window withheld
    // from compositing on open (see RestoreSuppression) is released the
    // moment its reposition configure lands — detected as the live
    // geometry leaving the spawn point once applyWindowGeometry has
    // stamped the resolved target. Before the target is known a
    // geometry change is just the client's own initial size negotiation
    // and is ignored. A size-only commit releases only at the target
    // size: a client that commits a size of its own before the
    // reposition lands has not landed (F759).
    //
    // Body 2 — frame-geometry shadow: push the latest geometry to the
    // daemon so daemon-local shortcut handlers (float toggle, etc.) can
    // read fresh geometry without round-tripping. Throttled by the shared
    // single-shot m_frameGeometryFlushTimer: at most one setFrameGeometry
    // per window per 50 ms, so a long drag sends one every 50 ms.
    connect(
        w, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
            // isDeleted() alongside the null test, as the other lambdas
            // over this signal and its maximize siblings do. A window held
            // alive under WindowClosedGrabRole still emits this, and no body
            // below is owed anything for a corpse.
            //
            // It is a HAZARD guard, not merely an early-out, and the hazard
            // is the same one the maximize lambda's guard names: the id
            // lookup just below runs BEFORE the checks that would decline for
            // the bodies, and getWindowId re-populates the id caches on a
            // miss — the caches slotWindowClosed has just scrubbed for a
            // window not riding a close animation. The re-populated mapping
            // then waits for the windowDeleted backstop. The moves and the
            // daemon flush themselves do decline on their own
            // (scrollManagedOutputFor and flushPendingFrameGeometry both
            // refuse a deleted window); what they do not do is decline
            // before the lookup.
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // One lookup for every body. Body 2 needs it on every tick
            // anyway, so asking it first costs nothing extra.
            const QString windowId = getWindowId(safeW.data());
            // Body -1.5 — answer a superseded configure's late ack. An
            // apply that asked for the size the client already had, while a
            // different size was still in flight, was applied by KWin with
            // no configure, so that older configure stayed outstanding and
            // its ack just landed the stale size (WindowCommandStamps::
            // StaleAck). Re-issue the command once: its size now differs
            // from the client's, so KWin sends a real configure. Void once
            // a newer command has landed (a size-equal move carries the
            // answer forward instead) or the user has the frame.
            //
            // An answer whose superseded configure the client acked at the
            // target size stays armed for the rest of its command: KWin hands
            // an effect no configure serials, and an ack that moves nothing
            // emits no frame change to consume it on. The quick-tile corrector
            // arms one that way whenever KWin's tile configure was only
            // scheduled when the tile was dropped, since that configure then
            // goes out carrying the column (ScrollDecisions::
            // droppedTileAckOutstanding). A geometry request KWin makes for
            // another size voids it (the frameGeometryAboutToChange hook
            // above). What it can still do is re-issue its own command's rect,
            // once, against a later resize the client makes on its own while
            // that command is current.
            //
            // Nor while the outputs change or the strip's output has moved
            // since its batch (F196): the target is in the coordinates the
            // command was built in, which an output move invalidates without
            // superseding the command, so the re-issue would pull the window
            // back to where the output used to be. The same gate the
            // offered-column read and the counter-assert take (F815); the next
            // command arrives in the new coordinates.
            if (const auto staleIt = m_daemonGate.commandStamps.staleAcks.find(safeW.data());
                staleIt != m_daemonGate.commandStamps.staleAcks.end() && !m_daemonGate.inGeometryApply) {
                const WindowCommandStamps::StaleAck stale = *staleIt;
                const QSize committed = safeW->frameGeometry().toRect().size();
                const bool coordinatesStale = m_screenChangeHandler->isScreenChangeInProgress()
                    || (m_tilingHandler->hasScrollingScreens()
                        && m_tilingHandler->stripOutputMovedSinceBatch(
                            m_tilingHandler->scrollTrackedScreenFor(windowId)));
                if (!m_daemonGate.commandStamps.isCurrent(safeW.data(), stale.stamp) || safeW->isUserMove()
                    || safeW->isUserResize() || coordinatesStale) {
                    m_daemonGate.commandStamps.staleAcks.erase(staleIt);
                } else if (committed != stale.target.size()) {
                    m_daemonGate.commandStamps.staleAcks.erase(staleIt);
                    if (KWin::Window* kwStale = safeW->window()) {
                        qCInfo(lcEffect) << "Re-issuing" << stale.target << "over a superseded configure's ack"
                                         << committed << "for" << windowId;
                        const auto staleGuard = geometryApplyScope();
                        kwStale->moveResize(QRectF(stale.target));
                    }
                }
            }
            // Body -1 — retarget a strip animation onto the rect the
            // client actually committed.
            //
            // Only fires for a client that did not take the geometry it
            // was handed. A programmatic move lands at once, an X11 resize
            // with it, and a Wayland resize lands on the client's ack of the
            // configure; a client that takes its rect commits exactly the
            // animation's target, so the isAnimatingToTarget test
            // short-circuits for it whenever its commit arrives. A client with a
            // size constraint it enforces itself — an aspect ratio, a
            // minimum bigger than its column — negotiates asynchronously
            // and lands somewhere else, usually centred within the rect it
            // was offered. The leg then drove toward the COLUMN rect while
            // the commit sat centred inside it, so the window slid to its
            // column's edge and snapped back on every step scroll.
            //
            // Measured, not predicted: constrainTileGeometry can pre-empt
            // this for X11 (it applies the same constraint KWin will) but
            // is a pass-through for Wayland, where the negotiated size is
            // not knowable up front. Retargeting when the commit arrives
            // needs no prediction and covers both.
            //
            // DELIBERATELY UNGATED on inGeometryApply, unlike Body -0.5
            // below, and the difference is what each body does with the
            // commit rather than an oversight. This one retargets a
            // running leg ONTO the rect that was just committed, which is
            // the right destination whoever committed it — including the
            // effect itself, since a mid-animation apply from any of the
            // bracketed sites is exactly a new destination the leg should
            // adopt. Body -0.5 instead CENTRES the window on a
            // size mismatch, and during an effect apply that mismatch is
            // transient, so acting on it would fight the write in flight.
            //
            // NOT for applyWindowGeometry's own animated commit, which it
            // makes BEFORE retargeting the leg itself: the synchronous
            // re-entry would retarget first with PreservePosition and zero
            // the leg's velocity, so a spring curve lost its momentum on
            // every animated re-apply. The apply marks that one commit
            // (DaemonGateState::animatedApplyCommit); every other effect
            // commit is a new destination the leg should adopt.
            //
            // Scoped to strip members: this is the only path that
            // relocates a window away from its committed rect, so it is
            // the only one where a divergent commit desynchronises the
            // leg from where the window will actually be.
            //
            // NOT for a commit at a park. A leaving column's leg ends at the
            // screen edge while its commit is the park (visualTargetOverride
            // in applyWindowGeometry), so a park commit landing mid-leg is
            // the split working, not a client that refused its rect: a
            // size-changing park lands there on its ack, and on KWin 6.8 the
            // counter-assert re-seats a park the client's ack pulled back on
            // screen. Retargeting onto it swept the leaving leg down off the
            // screen instead of out past the edge. Asked last, because it
            // walks the outputs.
            if (m_windowAnimator->hasAnimation(safeW.data()) && scrollManagedOutputFor(safeW.data())
                && m_daemonGate.animatedApplyCommit != safeW.data()) {
                const QRectF committed = safeW->frameGeometry();
                if (!committed.isEmpty() && !m_windowAnimator->isAnimatingToTarget(safeW.data(), committed)
                    && !m_tilingHandler->rectAtScrollPark(committed.toRect())) {
                    // PreservePosition: the leg keeps the pixels it is
                    // already showing and bends toward the true rect.
                    // Velocity carried across would re-scale to a
                    // correction that is a few hundred pixels at most and
                    // overshoot it.
                    //
                    // A DegenerateReap needs no replacement: the retarget
                    // landed on the rect the window already occupies, the
                    // leg has converged, and the reap's completion handler
                    // ends it cleanly. An ACCEPTED retarget restarts the
                    // animator's progress from the pixels on screen, so a
                    // geometry-owning morph riding it is re-anchored there
                    // too, as applyWindowGeometry does after its own
                    // retarget. Left alone, the morph replayed the whole leg
                    // from its original departure rect (the #795 jump-back).
                    const QRectF visualPos = m_windowAnimator->currentValue(safeW.data(), committed);
                    const auto retarget = m_windowAnimator->retargetWithResult(
                        safeW.data(), committed, PhosphorAnimation::RetargetPolicy::PreservePosition);
                    auto* morph = m_shaderManager.findTransition(safeW.data());
                    if (retarget == PhosphorAnimation::RetargetResult::Accepted && morph && morph->cached
                        && morph->cached->iFromRectLoc >= 0 && morph->durationMs == 0) {
                        morph->fromGeometry = visualPos;
                        morph->toGeometry = committed;
                    }
                }
            }
            // Body -0.5 — read the frame as the client's answer to its strip
            // column: report it to the engine when it differs, then centre
            // it in the column (readColumnAnswer). The commit watch reads the
            // same answer when a settle changes no frame.
            //
            // Cheapest test first, and the ordering is load-bearing for
            // cost rather than correctness. scrollManagedOutputFor memoises
            // only WITHIN a paint pass, and this lambda runs off the paint
            // cycle, so once any screen is scrolling — its own first test
            // is hasScrollingScreens, which costs nothing when none is —
            // every call pays a tracked-screen resolve, a float probe and
            // an output lookup uncached. The empty-map test here and the
            // offered-column probe that opens readColumnAnswer answer the
            // common case, plain snap and autotile windows that miss the
            // map, before any of that. Body -1 above already orders it this
            // way.
            if (!m_daemonGate.inGeometryApply && !m_scrollOfferedColumn.isEmpty()) {
                readColumnAnswer(safeW.data(), windowId);
            }
            // Body 0 — deferred maximize-morph completion. The maximize
            // state edge above arms this entry when it fires before the
            // client has committed the new size (see PendingMaximizeMorph);
            // the geometry change that actually delivers the size lands
            // here and starts the morph at the visible jump. A
            // position-only step keeps waiting. The deadline discards a
            // stale entry (state flipped but the commit never came — e.g.
            // an occluded client under the lock screen) so a much later
            // unrelated resize cannot fire a bogus maximize animation.
            if (const auto pendingIt = m_shaderManager.m_pendingMaximizeMorph.constFind(safeW.data());
                pendingIt != m_shaderManager.m_pendingMaximizeMorph.constEnd()) {
                const auto pending = pendingIt.value();
                if (maximizeSizeLanded(safeW->frameGeometry(), pending.departureFrame)) {
                    m_shaderManager.m_pendingMaximizeMorph.remove(safeW.data());
                    // The deadline SKIPS the morph for a stale entry; the
                    // entry itself is consumed either way by the remove
                    // above (only a size-landing geometry change reaches
                    // this branch). A never-landing entry is otherwise
                    // dropped by the next genuine full-maximize edge (the
                    // state lambda removes or replaces it on every path),
                    // by noteMaximizeDemotedForSnap, or by the windowDeleted
                    // cleanup: bounded, and cheaper than a timer per entry.
                    const bool stale =
                        ShaderInternal::shaderClockNowMs() - pending.armedAtMs > kPendingMaximizeMorphDeadlineMs;
                    // Same interactive guard as the arming site: a drag
                    // that started while the entry was pending owns the
                    // visuals (its move pack, when one is assigned). A
                    // placement leg that ran through the wait ends here,
                    // and the morph departs from where it drew (F674).
                    if (!stale && !safeW->isUserMove() && !safeW->isUserResize()) {
                        QRectF departure = pending.departureFrame;
                        if (const QRectF drawn = endRunningPlacementLeg(safeW.data(), /*endMaximizeLeg=*/false);
                            drawn.isValid()) {
                            departure = drawn;
                        }
                        beginMaximizeShaderMorph(safeW.data(), departure);
                    }
                }
            }
            // Body 1 — suppression release. Integer-aligned rects:
            // fractional-scale outputs leave sub-pixel residue in
            // frameGeometry(), and a bit-exact compare released the
            // suppression on jitter that moved nothing.
            if (auto it = m_restoreSuppress.find(safeW.data()); it != m_restoreSuppress.end()
                && it->targetGeometry.isValid()
                && InstantRestoreDecisions::suppressionSettled(it->spawnGeometry.toRect(), it->targetGeometry.toRect(),
                                                               safeW->frameGeometry().toRect())) {
                endRestoreSuppression(safeW.data());
            }
            // Body 2 — throttled daemon shadow. Per tick this stashes the
            // latest geometry and runs ONLY the cheap decoration resync.
            // The shouldHandleWindow gate decides only whether the push
            // goes out, so it runs where the push does, in
            // flushPendingFrameGeometry once per window per flush, after
            // the flush's geometry-scoped verdict eviction (the one place
            // its cached exclusion verdict routinely misses). Per tick it
            // ran on every frame of an interactive move or resize
            // (discussion #816).
            if (windowId.isEmpty()) {
                return;
            }
            // Self-heal a noBorder reset KWin issues asynchronously after
            // a cross-OUTPUT move. For a rule-owned (title-bar-hidden)
            // window the manager already believes it hidden, so the
            // synchronous resync in updateAllDecorations bails ("still
            // suppressed") when it runs before KWin re-evaluates the
            // decoration. On X11 KWin keeps the client size when it
            // re-decorates (X11Window::updateDecoration), so the frame
            // grows by the title-bar height, firing this very signal. A
            // Wayland client is instead configured to keep its frame size
            // (XdgToplevelWindow::sendRoleConfigure takes the new borders
            // off the client size), so its frame grows, and this fires,
            // only when the client keeps its client size anyway. resyncWindow
            // re-hides exactly the windows the manager owns and believes
            // hidden whose decoration drifted back, and is a self-guarding
            // no-op otherwise. Kept PER TICK, not behind the flush: it is
            // a hash lookup plus two flag checks for the untracked common
            // case, and deferring it to the flush let the re-decorated
            // title bar flash for up to the 50ms throttle window. No
            // shouldHandleWindow gate: the manager owns whatever the title-bar
            // reconcile hides (reconcileRuleHiddenTitleBar shields structural and
            // own surfaces only, not the dialogs and transients shouldHandleWindow
            // rejects), so a gate would drop this self-heal for exactly those.
            m_decorationManager->resyncWindow(windowId);
            const QRect geo = safeW->frameGeometry().toRect();
            if (geo.width() <= 0 || geo.height() <= 0) {
                return;
            }
            m_pendingFrameGeometry[windowId] = {geo, safeW};
            if (!m_frameGeometryFlushTimer->isActive()) {
                m_frameGeometryFlushTimer->start();
            }
        });

    // Refresh the daemon's registry metadata on every minimize edge, connected
    // BEFORE the handler connections below so the daemon's suspension
    // classification reads fresh minimize state. Both handlers debounce their
    // ordinary float (kSpuriousMinimizePairMs), so that commit lands after the
    // push anyway; their same-edge sends depend on this connection order: both
    // re-minimize countermands send setWindowFloatingForScreen on the edge, and
    // so does the snap handler's unfloat of an adopted autotile minimize-float
    // (commitUnminimizeUnfloat). The daemon's mode-swap seed/restore decisions
    // consult WindowMetadata::isMinimized, which would otherwise remain at its
    // previous snapshot until an unrelated refresh — a stale value lets a
    // mode-swap seed tile a window that is minimized right now (nothing else
    // catches it: the seed filter keeps every non-minimized entry, and its one
    // float test, the target engine's slot, runs only once this read says
    // minimized).
    // Liveness-guarded but deliberately NOT gated on shouldHandleWindow /
    // isTileableWindow: the open-time push in slotWindowAdded registers EVERY
    // window, and the daemon's rule predicates (IsMinimized) evaluate against
    // that registry metadata for every window too — a tileable-only gate here
    // would leave non-tileable windows' minimize state permanently stale.
    // Spurious minimize pairs cost only the marshal: the registry upsert
    // de-dupes content-identical pushes.
    connect(w, &KWin::EffectWindow::minimizedChanged, this, [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
        // The minimize edge can race close teardown (the EffectWindow
        // outlives the client as a Deleted shell); pushing metadata for it
        // would resurrect a registry record the close path just removed.
        if (!safeW || safeW->isDeleted()) {
            return;
        }
        pushWindowMetadata(safeW.data());
        // IsMinimized is a matchable rule field stamped live into the
        // per-window query, but the verdict caches key on (windowId, ruleSet
        // revision) — neither moves on a minimize edge, so an
        // `IsMinimized`-scoped exclusion or appearance verdict would pin
        // stale (buildWindowMap consults the placement gate for minimized
        // windows, so the wrong verdict IS produced and cached). The managed
        // paths' float-flip invalidation only covers engine-managed windows
        // and only when the float bit actually flips; this covers every
        // window on every edge, coalesced by the flush.
        invalidateRuleCacheForStateChange(getWindowId(safeW.data()));
    });

    // Tiling screens: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowMinimizedChanged);

    // The window.minimize shader leg for every window the animation filter
    // admits, then the snap handler's minimize float / unminimize unfloat for
    // a snapped one.
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);

    // Refresh the registry on every urgency edge, for the same reason as the
    // minimize edge above: WindowMetadata::isDemandingAttention would
    // otherwise sit at whatever the last unrelated push snapshotted, and a
    // stale urgency is worse than none: the daemon mirrors it to the Phosphor
    // shell (windowUrgencyChanged, getUrgentWindows), whose placement map would
    // keep a window lit long after it stopped asking for attention, or never
    // light it. The effect's own tab pill reads KWin's live bit at model
    // rebuild. The signal lives on KWin::Window, not EffectWindow, so this
    // connection needs the underlying window; a window without one (no
    // KWin::Window backing) simply never reports urgency, which the daemon
    // reads as "not urgent". The EffectWindow is captured weakly; the
    // KWin::Window is only the signal SENDER and is not captured at all, and
    // passing `this` as the context object means Qt drops the connection when
    // either the sender or the effect is destroyed.
    if (KWin::Window* underlying = w->window()) {
        connect(underlying, &KWin::Window::demandsAttentionChanged, this,
                [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                    if (!safeW || safeW->isDeleted()) {
                        return;
                    }
                    pushWindowMetadata(safeW.data());
                    // Urgency lights a compositor-drawn tab pill in both bar
                    // styles, so this rebuilds every strip naming the window
                    // (noteScrollTabWindowChanged), unlike the caption hook in
                    // window_metadata_connections.cpp, which skips segment-bar
                    // screens.
                    m_tilingHandler->noteScrollTabWindowChanged(getWindowId(safeW.data()));
                });
    }
}

void PlasmaZonesEffect::beginMaximizeShaderMorph(KWin::EffectWindow* window, const QRectF& departureFrame)
{
    if (!window) {
        return;
    }
    // ALWAYS a forward leg. Geometry packs encode direction in the rects,
    // not the timeline: the zone-snap path (window_geometry_apply.cpp) never reverses
    // either — a shrink into a small zone is a forward morph with a small
    // iToRect. Reversing here split the two geometry-shader families:
    // fragment morphs read raw iTime (flipped → played maximized→restored),
    // but the vertex-grid packs (fold / stretch / flow / ripple-snap) run
    // their motion through legProgress(), which un-flips iTime back to a
    // forward 0→1 — with swapped rects they animated restored→MAXIMIZED
    // while the real window sat restored, then popped at teardown ("gets
    // sized down, then plays an animation"). Forward + natural rects
    // satisfies both families with the same values, and keeps the grid
    // anchoring contract intact (apply() builds the deform grid on
    // iToRect == the live frame).
    //
    // ON THE PLACEMENT NODES, not a maximize node of its own. Growing to the
    // maximize area is a window arriving somewhere and rides placeIn; restoring
    // is a window let go and rides placeOut — the same two legs every engine
    // placement and release ride, so a user's "when windows change size" pack
    // plays here too. The committed mode is the direction: this runs from the
    // maximizedChanged handler (and its deferred completion), which KWin emits
    // only after updating maximizeMode() — verified against 6.7.4, both
    // X11Window::maximize and XdgToplevelWindow::updateMaximizeMode assign the
    // member before emitting, and EffectWindow's forwarder reads it inside the
    // handler.
    const KWin::Window* kw = window->window();
    const bool toMaximized = kw && kw->maximizeMode() == KWin::MaximizeFull;
    // A second maximize edge while this handler's own leg is still live (a
    // rapid toggle) SUPERSEDES that leg, departing from the rect it is drawing
    // now (see ShaderTransition::maximizeLeg). The drawn rect mirrors the paint
    // predictor's split: position on the raw progress, size on the clamped one.
    QRectF drawnDeparture;
    if (ShaderTransition* live = m_shaderManager.findTransition(window); live && live->maximizeLeg) {
        drawnDeparture = predictedMorphRect(*live, ShaderInternal::shaderClockNowMs());
        endShaderTransition(window);
    }
    const ShaderTransition* liveBefore = m_shaderManager.findTransition(window);
    const quint64 generationBefore = liveBefore ? liveBefore->generation : 0;
    bool ownsMaximizeLeg = false;
    tryBeginShaderForEvent(window,
                           toMaximized ? PhosphorAnimation::ProfilePaths::WindowPlaceIn
                                       : PhosphorAnimation::ProfilePaths::WindowPlaceOut,
                           animationDurationMs(),
                           /*reverse=*/false, /*holdCloseGrab=*/false, /*holdAddedGrab=*/false,
                           /*animateMinimized=*/false, &ownsMaximizeLeg);
    // Geometry-morph endpoints — sibling of the snap-placement wiring in
    // window_geometry_apply.cpp. The placement legs are geometry-contract events, so every
    // assignable pack derives its drawn rect from iFromRect/iToRect; leaving
    // them default-invalid pushes zero vec4s and a morph pack masks every
    // fragment outside a 0×0 rect at the origin — the window paints fully
    // transparent for the whole leg and pops in on teardown.
    //
    // Gate on the IDENTITY verdict, not on findTransition liveness: with no
    // placement pack assigned, tryBeginShaderForEvent installs nothing
    // and findTransition hands back whatever unrelated leg is in flight
    // (window.open on a self-maximizing app is the reachable case). Writing
    // morph endpoints onto that leg re-anchors its drawn rect mid-flight and
    // — for a non-morph leg — switches it into morph mode. Same rule as the
    // heldMove stamp on the drag path.
    if (!ownsMaximizeLeg) {
        return;
    }
    auto* st = m_shaderManager.findTransition(window);
    if (!st || !st->cached || st->cached->iFromRectLoc < 0) {
        return;
    }
    const QRectF newFrame = window->frameGeometry();
    QRectF preFrame = drawnDeparture.isValid() ? drawnDeparture : departureFrame;
    if (preFrame.isEmpty()) {
        // Degenerate departure rect: degrade to a static morph at the live
        // frame — visible, just motionless — rather than the transparent
        // zero-rect.
        preFrame = newFrame;
    }
    // Always retarget the destination; anchor the departure + snapshot only
    // on a FRESH install. A leg the same-effect short-circuit KEPT (another
    // placement leg of the same pack, already running) has its own departure
    // and snapshot, and re-anchoring it mid-flight would jump the drawn rect
    // and collapse the cross-fade. Fresh is decided by the generation, not by
    // the snapshot: the default window-morph is vertex-only and never takes
    // one, so a missing snapshot said nothing about whether the leg was new.
    st->toGeometry = newFrame;
    if (!liveBefore || st->generation != generationBefore) {
        st->maximizeLeg = true;
        st->fromGeometry = preFrame;
        // preFrame is a REAL rect the window occupied, so any synthetic-origin
        // marker a kept scroll leg carried no longer describes fromGeometry.
        // Clear it, or the pending capture wrongly takes the raw path and the
        // maximize morph's old side loses its decorated composite seed. The
        // invariant: fromIsSynthetic tracks the provenance of the CURRENT
        // fromGeometry, maintained at every writer (see window_geometry_apply.cpp's
        // sticky retarget arm for the synthetic-path counterpart).
        st->fromIsSynthetic = false;
        // Old-content cross-fade: same guard as the move-start hookup. The
        // raw capture happens on the first paint (post-jump, so it degrades
        // to the live content for undecorated windows), but decorated
        // windows seed from the frozen pre-jump composite — see
        // captureOldWindowSnapshot.
        if (st->cached->iOldWindowLoc >= 0) {
            st->needsSnapshot = true;
        }
    }
}

QRectF PlasmaZonesEffect::endRunningPlacementLeg(KWin::EffectWindow* w, bool endMaximizeLeg)
{
    // The rect the leg is drawing: the animator's current value, or a
    // maximize morph's predicted one (invalid for a morph that draws no rect
    // of its own, which is then left running). Time-driven legs that are not
    // maximize legs, the held move and the open among them, belong to the
    // gesture or the open and are never ended here.
    const QRectF frame = w->frameGeometry();
    QRectF drawn;
    bool endShader = false;
    if (ShaderTransition* live = m_shaderManager.findTransition(w); endMaximizeLeg && live && live->maximizeLeg) {
        drawn = predictedMorphRect(*live, ShaderInternal::shaderClockNowMs());
        endShader = drawn.isValid();
    }
    if (m_windowAnimator->hasAnimation(w)) {
        drawn = m_windowAnimator->currentValue(w, frame);
        m_windowAnimator->removeAnimation(w);
        // removeAnimation runs no completion, and an animator-driven shader
        // leg (durationMs 0) has no other progress source or teardown: end it
        // as the completion callback would (F335).
        if (const ShaderTransition* orphan = m_shaderManager.findTransition(w); orphan && orphan->durationMs == 0) {
            endShader = true;
        }
    }
    if (endShader) {
        endShaderTransition(w);
    }
    // removeAnimation schedules no damage: the drawn rect and the frame.
    if (drawn.isValid()) {
        repaintSnapRegions(w, drawn, frame.toRect());
    }
    return drawn;
}

void PlasmaZonesEffect::recordStripOffer(KWin::EffectWindow* w, const QString& windowId, const QRect& column,
                                         bool sendsSize)
{
    using ScrollDecisions::ColumnAnswer;
    if (!w) {
        return;
    }
    ScrollOfferedColumn& entry = m_scrollOfferedColumn[windowId];
    entry.rect = column;
    // A fresh delivery re-arms the put-back budget (noteForeignStripRequest),
    // as a fresh batch re-arms the counter-assert's.
    entry.putBackBurstStartMs = 0;
    entry.putBackBurstCount = 0;
    KWin::Window* const kw = w->window();
    if (sendsSize) {
        // A new offer: whatever the client commits next answers THIS column,
        // so the answer is owed again, whatever became of the last one.
        entry.answer = ColumnAnswer::Owed;
        armColumnAnswerWatch(w, windowId, entry);
        return;
    }
    // No size goes out (a pure move, or the window already holds the size):
    // the client has nothing new to answer.
    if (entry.answer == ColumnAnswer::Owed) {
        // An answer still owed from an earlier offer stays owed. One that has
        // already settled was refused by a gate this batch has just cleared
        // (the strip's output moved, an output change), and no commit is
        // coming to read it again, so the catch-up walk reads it.
        if (kw && QRectF(kw->moveResizeGeometry()).toRect() == w->frameGeometry().toRect()) {
            scheduleColumnAnswerCatchUp();
        }
        return;
    }
    // Read or Foreign, or a fresh entry: the batch placed the window at the
    // size it holds, which is the answer standing from here on, so a later
    // frame change reports only a size the client moves to by itself.
    entry.answer = ColumnAnswer::Read;
    entry.answeredSize = kw ? QRectF(kw->moveResizeGeometry()).toRect().size() : w->frameGeometry().toRect().size();
}

void PlasmaZonesEffect::armColumnAnswerWatch(KWin::EffectWindow* w, const QString& windowId, ScrollOfferedColumn& entry)
{
    // A watch already serving this entry serves the new offer too: it reads
    // whatever the entry says when the next commit lands.
    if (entry.answerWatch != 0) {
        return;
    }
    KWin::Window* const kw = w ? w->window() : nullptr;
    if (!kw || !kw->surface()) {
        // No surface to watch: the frame hook alone reads the answer.
        return;
    }
    const quint64 watchId = ++m_columnAnswerWatchSeq;
    entry.answerWatch = watchId;
    // A client whose settle leaves its frame unchanged emits no frame change:
    // one answering with the frame it already had, or one whose answering
    // commit read unsettled because a state-only configure (activation, a
    // suspend, a scale change) followed the resize and settles later with
    // nothing left to move. Its commits are read instead, for as long as the
    // answer is owed. KWin connected its own commit handler when it made the
    // window, so it has run, and moveResizeGeometry() is current, when this one
    // does (the resize hold's ack watch relies on the same order).
    //
    // The watch retires itself on the first commit after the answer stops
    // being owed: the entry was read, removed, or replaced by one serving
    // another watch. That keeps it out of every remover of the map, at the
    // cost of one more call after the answer is read.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection =
        connect(kw->surface(), &KWin::SurfaceInterface::committed, this,
                [this, safeW = QPointer<KWin::EffectWindow>(w), windowId, watchId, connection]() {
                    const auto it = m_scrollOfferedColumn.find(windowId);
                    const bool serving = it != m_scrollOfferedColumn.end() && it->answerWatch == watchId;
                    if (!safeW || safeW->isDeleted() || !serving || it->answer != ScrollDecisions::ColumnAnswer::Owed) {
                        if (serving) {
                            it->answerWatch = 0;
                        }
                        disconnect(*connection);
                        return;
                    }
                    // Declined inside an effect apply, as the frame hook declines.
                    if (!m_daemonGate.inGeometryApply) {
                        readColumnAnswer(safeW.data(), windowId);
                    }
                });
}

void PlasmaZonesEffect::readColumnAnswer(KWin::EffectWindow* w, const QString& windowId)
{
    using ScrollDecisions::ColumnAnswer;
    // Copied out, not held as an iterator across the gates below.
    // scrollManagedOutputFor does not touch this map today, so the iterator
    // would survive, but it resolves a screen, a float verdict and an output,
    // and a future reader has no reason to expect an unrelated call to be
    // iterator-critical.
    const auto found = m_scrollOfferedColumn.constFind(windowId);
    if (found == m_scrollOfferedColumn.constEnd() || !w || w->isDeleted()) {
        return;
    }
    const ScrollOfferedColumn offer = *found;
    KWin::Window* const kw = w->window();
    if (!kw) {
        return;
    }
    const QRect live = w->frameGeometry().toRect();
    // The gates the answer is read through (ScrollDecisions::
    // readsAsColumnAnswer), the cheap ones first: this runs on every frame
    // change of a strip window and on every commit while an answer is owed,
    // and the scroll-managed resolve is the costly one.
    //
    // Settled compares the whole rect, exactly, unlike the autotile centring
    // pass's one-pixel tolerance. KWin syncs its move-resize geometry to the
    // very rect it commits as the frame, skipping the sync only while a
    // configure that places the window is unacknowledged or about to be sent,
    // so at rest the two are the same value and round alike. A tolerance would
    // admit a one-pixel resize still in flight, and the position half catches
    // a placing configure still queued.
    //
    // KWin's own maximize, fullscreen and quick tile commit the new frame
    // before the state change that explains it, so their REQUESTED state says
    // which this is; a frame committed for one is not the client's answer to
    // its column (F123). The resize hold waits for the client's answer to the
    // user's resize, which is the user's frame (F35).
    ScrollDecisions::ColumnAnswerGates gates;
    gates.settled = QRectF(kw->moveResizeGeometry()).toRect() == live;
    gates.kwinStateAtRest = kw->requestedMaximizeMode() == KWin::MaximizeRestore && !kw->isRequestedFullScreen()
        && !kw->isFullScreen() && !kw->requestedTile();
    gates.resizeHeld = m_resizeHold.window == w;
    // One tracked-screen resolve for both its readers below (F156).
    QString screenId;
    if (gates.settled && gates.kwinStateAtRest && !gates.resizeHeld && offer.answer != ColumnAnswer::Foreign
        && !live.isEmpty() && live.intersects(offer.rect)) {
        gates.managed = scrollManagedOutputFor(w) != nullptr;
        if (gates.managed) {
            screenId = m_tilingHandler->scrollTrackedScreenFor(windowId);
            // Not while the outputs change or the strip's output has moved
            // since its batch: the offer is in the old coordinates (F815).
            gates.coordinatesHold = !m_screenChangeHandler->isScreenChangeInProgress()
                && !m_tilingHandler->stripOutputMovedSinceBatch(screenId);
        }
    }
    if (!ScrollDecisions::readsAsColumnAnswer(gates, offer.answer, offer.rect, live)) {
        if (offer.answer == ColumnAnswer::Owed && !gates.settled) {
            qCDebug(lcEffect) << "Column answer for" << windowId << "not settled: commanded"
                              << QRectF(kw->moveResizeGeometry()) << "frame" << live;
        }
        return;
    }
    // The report: once per answer (ScrollDecisions::isNewColumnAnswer). An
    // answer the report could not take yet (no daemon, no axis map) stays
    // owed for the catch-up walk; one it took, or found nothing in, is read.
    if (ScrollDecisions::isNewColumnAnswer(offer.answer, offer.answeredSize, live.size())
        && m_tilingHandler->reportStripCommit(windowId, screenId, offer.rect, live)
            != TilingHandler::StripCommitReport::Deferred) {
        if (const auto entry = m_scrollOfferedColumn.find(windowId); entry != m_scrollOfferedColumn.end()) {
            entry->answer = ColumnAnswer::Read;
            entry->answeredSize = live.size();
        }
    }
    // The centring. The strip offers the full column rect the first time it
    // sees a column SIZE, because until the client has answered there is
    // nothing else to offer. A client that will not take that size commits
    // its own at the column's top-left, so a freshly inserted window — or one
    // that just went full width — hugs the top (or the left) until the next
    // batch, which is the first placement able to offer the settled size
    // centred.
    //
    // Corrected here instead of waiting: the answer is in, so the centred
    // position is known now. Measured rather than predicted, like every other
    // placement decision on this path — the alternative would be modelling the
    // client's own size rule, which is not knowable up front.
    //
    // Only on a settled frame (F49, F154). A move retargets the configures
    // still queued to the new position, so centring a frame that a queued
    // resize is about to replace landed that resize displaced, and undid the
    // stale-ack re-issue (Body -1.5) positionally on the very tick it was
    // sent.
    if (live.size() == offer.rect.size()) {
        return;
    }
    // Not a column the strip placed outside its output (a park, or a straddler
    // cropped at the edge): its seat is the counter-assert's, which re-seats
    // it against KWin 6.8's keep-visible clamp under a budget that stops the
    // configure, ack and clamp loop a handover to another output starts. A
    // move here would make the very re-seat the budget had just refused, with
    // nothing charged for it (F172).
    if (const auto commanded = m_scrollCommandedRects.constFind(windowId);
        commanded != m_scrollCommandedRects.constEnd() && commanded->outOfView) {
        return;
    }
    // Same seat as the counter-assert and the paint resolver
    // (ScrollDecisions::reseatOrigin), so the three agree: the same toRect()
    // rounding, and the same clamp at zero, so a frame whose minimum exceeds
    // its column stays anchored at the column's origin rather than shifting
    // past its edge (F173).
    const QPoint centred = ScrollDecisions::reseatOrigin(offer.rect, live.size());
    if (live.topLeft() == centred) {
        return;
    }
    // A pure move(), so it cannot renegotiate the size the client settled on.
    // Bracketed like the effect's other commits (applies, centring). The move
    // emits a synchronous frameGeometryChanged, which re-enters that signal's
    // whole connection list from the top: the tiling handler's slot (the
    // counter-assert, the virtual-screen crossing detector and the autotile
    // centring pass) and the frame hook itself. Under the bracket the slot
    // treats the move as the effect's own rather than an external or
    // user-driven one, and the frame hook's read declines at its
    // inGeometryApply gate, so the re-entry neither reports nor moves again.
    const auto restoreGate = geometryApplyScope();
    kw->move(QPointF(centred));
}

void PlasmaZonesEffect::noteForeignStripRequest(KWin::EffectWindow* w)
{
    using ScrollDecisions::ColumnAnswer;
    const QString windowId = getWindowId(w);
    const auto it = m_scrollOfferedColumn.find(windowId);
    if (it == m_scrollOfferedColumn.end() || it->answer == ColumnAnswer::Foreign) {
        return;
    }
    // The user's resize: the frame is the user's from its first step, and the
    // resize drain reports it to the engine as a resize (notifyWindowResized).
    // Read as an answer, its late ack planted a floor at the user's width and
    // centred the window in the column it was dragged out of (F35).
    if (w->isUserResize()) {
        it->answer = ColumnAnswer::Foreign;
        return;
    }
    KWin::Window* const kw = w->window();
    if (!kw) {
        return;
    }
    // KWin's own request, readable here because Window::moveResize, move and
    // resize store it before moveResizeInternal emits this signal. One that
    // resizes on KWin's account would otherwise read as the client's answer
    // once the client acks it, and become a floor no width verb or drag could
    // clear, or float the window outright when it passes the work area
    // (ScrollDecisions::resizesOnKWinsAccount, F141).
    const QRectF requested(kw->moveResizeGeometry());
    const QRect frame = w->frameGeometry().toRect();
    if (!ScrollDecisions::resizesOnKWinsAccount(requested, QRectF(w->frameGeometry()), it->rect.size())) {
        return;
    }
    // The strip owns the column's rect, so the resize is put back
    // (ScrollDecisions::putsBackKWinResize), as an X11 window's is by the
    // counter-assert. Here, from inside the request, before KWin acts on it:
    // XdgSurfaceWindow::moveResizeInternal emits this signal first, and for a
    // size other than the client's it only schedules a configure, which
    // XdgToplevelWindow::sendRoleConfigure builds from the move-resize
    // geometry KWin holds when it sends it. Put back now, KWin's size never
    // reaches the client: the configure carries the rect put back, the frame
    // KWin's request has not touched stays where it is, and the client's
    // answer to its column stands as it was. Put back later, the configure
    // could already have gone out. A raw moveResize rather than an apply,
    // like the stale-ack re-issue: the window keeps its command stamp, a
    // strip leg still running keeps running, and no stale ack is armed for a
    // configure KWin never sent.
    //
    // Copied out, not held as an iterator across the gates below, as
    // readColumnAnswer does, and found again for the writes.
    const ColumnAnswer answer = it->answer;
    const QRect offered = it->rect;
    ScrollDecisions::KWinResizePutBackGates gates;
    gates.kwinStateAtRest = kw->requestedMaximizeMode() == KWin::MaximizeRestore && !kw->isRequestedFullScreen()
        && !kw->isFullScreen() && !kw->requestedTile();
    gates.userGesture =
        m_resizeHold.window == w || (m_dragTracker->isDragging() && windowId == m_dragTracker->draggedWindowId());
    if (gates.kwinStateAtRest && !gates.userGesture) {
        gates.managed = scrollManagedOutputFor(w) != nullptr;
        if (gates.managed) {
            gates.coordinatesHold = !m_screenChangeHandler->isScreenChangeInProgress()
                && !m_tilingHandler->stripOutputMovedSinceBatch(m_tilingHandler->scrollTrackedScreenFor(windowId));
        }
    }
    // The commanded rect only while the strip apply that recorded it is still
    // the window's latest command, as the counter-assert reads it.
    QRect commanded;
    if (const auto cit = m_scrollCommandedRects.constFind(windowId);
        cit != m_scrollCommandedRects.constEnd() && m_daemonGate.commandStamps.isCurrent(w, cit->commandStamp)) {
        commanded = cit->rect;
    }
    const QRect putBack = ScrollDecisions::kwinResizePutBackRect(commanded, answer, offered, frame);
    const auto entry = m_scrollOfferedColumn.find(windowId);
    if (entry == m_scrollOfferedColumn.end()) {
        return;
    }
    // The budget last, so a request the gates refuse spends none of it. A
    // script that keeps writing the frame is put back three times per rolling
    // second at most (the counter-assert's budget, re-armed by every delivery
    // of a column, recordStripOffer); past that its size stands, Foreign,
    // until the next delivery.
    if (ScrollDecisions::putsBackKWinResize(gates) && !putBack.isEmpty()
        && ScrollDecisions::shouldCounterAssert(entry->putBackBurstStartMs, entry->putBackBurstCount,
                                                ShaderInternal::shaderClockNowMs(), /*frameDiffers=*/true)) {
        qCInfo(lcEffect) << "Putting strip window" << windowId << "back at" << putBack
                         << "over a resize KWin requested:" << frame.size() << "->" << requested.size();
        const auto putBackGuard = geometryApplyScope();
        kw->moveResize(QRectF(putBack));
        return;
    }
    qCDebug(lcEffect) << "Column answer for" << windowId
                      << "superseded by a resize KWin requested:" << w->frameGeometry().size() << "->"
                      << requested.size();
    entry->answer = ColumnAnswer::Foreign;
}

void PlasmaZonesEffect::scheduleColumnAnswerCatchUp()
{
    if (m_columnAnswerCatchUpQueued) {
        return;
    }
    m_columnAnswerCatchUpQueued = true;
    // Queued, so the caller's own state is written by the time it runs (the
    // axis map behind the behaviour seed, the batch's applies) and every
    // trigger in one turn costs one walk.
    QTimer::singleShot(0, this, [this]() {
        m_columnAnswerCatchUpQueued = false;
        // Collected first: reading an answer can move the window, and that
        // move's frame change re-enters the frame hook, which reads the map.
        QStringList owed;
        for (auto it = m_scrollOfferedColumn.constBegin(); it != m_scrollOfferedColumn.constEnd(); ++it) {
            if (it->answer == ScrollDecisions::ColumnAnswer::Owed) {
                owed.append(it.key());
            }
        }
        for (const QString& windowId : std::as_const(owed)) {
            if (KWin::EffectWindow* const w = findWindowByIdExact(windowId)) {
                readColumnAnswer(w, windowId);
            }
        }
    });
}

} // namespace PlasmaZones
