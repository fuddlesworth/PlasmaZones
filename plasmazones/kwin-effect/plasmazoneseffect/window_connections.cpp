// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "paint_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/RetargetPolicy.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

#include "tilinghandler/tilinghandler.h"
#include "compositor/windowanimator.h"
#include "handlers/dragtracker.h"

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

    // Idempotency guard. Every connect below uses a lambda slot, which rules out
    // Qt::UniqueConnection, so a second call for the same window would silently
    // double each per-window handler — every geometry change handled twice, every
    // push marshalled twice. The two callers' window sets are disjoint by
    // construction (see the member's declaration), so this never fires today; it
    // is here so that stays true when a third caller appears.
    if (m_wiredWindows.contains(w)) {
        return;
    }
    m_wiredWindows.insert(w);

    // A maximize-on or fullscreen-on edge: KWin's restore rect is the frame the
    // window left, a free spot the daemon keeps as its float-back (F215). The
    // daemon refuses it for a window in a zone or on a tile. Nothing is sent
    // when there is no restore rect (it equals the frame, or the window is a
    // windowed-fullscreen or parked strip tile, which freeGeometryForCapture
    // answers with an invalid rect).
    const auto pushRestoreRect = [this](KWin::EffectWindow* window) {
        if (!shouldHandleWindow(window)) {
            return;
        }
        const QString windowId = getWindowId(window);
        const QRect geom = freeGeometryForCapture(window, QRectF(window->frameGeometry())).toRect();
        if (windowId.isEmpty() || !geom.isValid() || geom == window->frameGeometry().toRect()) {
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
    if (m_dragTracker) {
        m_dragTracker->noteWiredWindowMoveState(w);
    }

    // Desktop-set changes (the classified edit and the stamp it is diffed
    // against) live in window_desktop_connections.cpp.
    wireContextChangeHandlers(w);

    // Cross-output and virtual-screen moves with the flags-settle eviction
    // backstop (window_output_connections.cpp), the identity and metadata
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

    // window.maximize / window.unmaximize shader transition. Sibling lambda
    // to the TilingHandler hookup above (autotile drives the snap-back
    // logic; we drive the shader leg).
    //
    // KWin emits windowMaximizedStateChanged once per axis flip — a
    // user-driven left-half-snap → fully-maximize sequence fires twice
    // (vertical-only first, then fully-maximized). Without an edge filter
    // we'd start the placement morph for the intermediate state, then
    // immediately install it again on the next emission, with
    // the timer-driven teardown of the first racing the install of the
    // second. Track the last fully-maximized state per window and only
    // fire on actual edge transitions.
    // Seed from the LIVE maximize mode: a window already fully maximized when
    // the effect (re)loads has no entry, so its first RESTORE compared
    // false==false, read as a no-edge, and played no morph.
    //
    // No equivalent seed for m_maximizedToEdgesWindows, and that asymmetry is
    // intended. This map is an EDGE FILTER whose whole job is answering
    // "did the state change", so a missing entry is a wrong answer with no
    // way back — nothing else ever writes it. The claim ledger is an
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
    // current rects (nothing is resizing at connect time, so the pair agrees),
    // then refresh on every windowExpandedGeometryChanged. The body — and the
    // write-side invariants: never refresh from a paint-time sample, refuse
    // implausible margins — lives beside surfaceWindowRect in surfacelayers.cpp.
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
                // A quick tile (Meta+Left and friends) sets ONE axis, which
                // never reaches the interception below, and nothing else
                // clears it: the batch arm that would only runs when a
                // batch arrives, and the engine emits on change, so a quick
                // tile that moves no column schedules none. The window then
                // sits half-maximized against the strip's rects with no
                // correction coming.
                //
                // CANCEL ONLY, never a dispatch. Routing this through
                // interceptMaximizeRequest would dispatch a toggle,
                // turning the user's quick tile into a column maximize (or,
                // on a member, into an un-maximize).
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
            // sees genuine full-maximize edges only (KWin emits once per
            // axis, and a half-snapped window going to full fires twice —
            // a toggle verb driven off both would cancel itself), and
            // after the rule-cache invalidation, which must run for the
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
            // through the strip's own batch, which installs its own
            // placement leg (slotWindowsTileRequested) anchored at the
            // pre-maximize rect. Installing a maximize morph from this
            // handler on top would supersede that one — the same reasoning
            // as the drag-restore guard below. One owner per leg, and for an
            // engine-authored maximize the owner is the batch.
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
            // windowStartUserMovedResized hookup above installed the
            // window.move shader as a HELD transition — and installing the
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
                beginMaximizeShaderMorph(window, preFrame);
            } else {
                m_shaderManager.m_pendingMaximizeMorph.insert(window, {preFrame, ShaderInternal::shaderClockNowMs()});
            }
        });

    // Track when a monocle-maximized window goes fullscreen
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
        }
    });

    // The same gate's OTHER edges. A fullscreen window keeps isFullScreen()
    // true while it is minimized, sent to another desktop, or moved to another
    // output, so without these the covered set stays stale and a monitor with
    // nothing on it goes on being undecorated. Each is pre-gated on the window
    // actually being fullscreen: the refresh walks the entire stacking order,
    // and the overwhelming majority of windows firing these are not fullscreen
    // and cannot move the answer. The refresh's own set comparison then makes a
    // no-change call cost one compare.
    connect(w, &KWin::EffectWindow::minimizedChanged, this, [this, w]() {
        if (w && w->isFullScreen()) {
            refreshFullscreenSuppression();
        }
    });
    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this, w]() {
        if (w && w->isFullScreen()) {
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
        connect(kw, &KWin::Window::outputChanged, this, [this, w]() {
            if (w && w->isFullScreen()) {
                refreshFullscreenSuppression();
            }
        });
    }

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowFrameGeometryChanged);

    // Single windowFrameGeometryChanged lambda combining the effect-side
    // per-tick work, in the order the bodies run: the answer to a superseded
    // configure's late ack (Body -1.5), a strip-animation retarget
    // onto the rect the client actually committed (Body -1), the offered-column
    // centring for a client that would not take its column (Body -0.5),
    // deferred maximize completion (Body 0), first-frame suppression release
    // (Body 1), and the debounced daemon push (Body 2). Keeping the last two
    // as separate connections (which they were originally) doubled the per-geometry-
    // tick lambda dispatch cost without functional benefit; the bodies
    // are independent so collapsing them just runs one capture+vtable
    // hop per tick instead of two. The autotile-handler connection
    // immediately above is kept separate because it dispatches to a slot
    // on a different receiver (`m_tilingHandler.get()`).
    //
    // Body 1 — first-frame open suppression release: a window withheld
    // from compositing on open (see RestoreSuppression) is released the
    // moment its reposition configure lands — detected as the live
    // geometry leaving the spawn point once applyWindowGeometry has
    // stamped the resolved target. Before the target is known a
    // geometry change is just the client's own initial size negotiation
    // and is ignored. Full-rect compare (not just topLeft) catches
    // size-only configures whose origin coincidentally matches the
    // spawn point.
    //
    // Body 2 — frame-geometry shadow: push the latest geometry to the
    // daemon so daemon-local shortcut handlers (float toggle, etc.) can
    // read fresh geometry without round-tripping. Debounced at ~50 ms
    // per window via m_frameGeometryFlushTimer so rapid move/resize
    // sequences collapse into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                // isDeleted() alongside the null test, as the other lambdas
                // over this signal and its maximize siblings do. A window held
                // alive under WindowClosedGrabRole still emits this, and no body
                // below is owed anything for a corpse.
                //
                // It is a HAZARD guard, not merely an early-out, and the hazard
                // is the same one the maximize lambda's guard names: Body -0.5
                // and Body 2 both call getWindowId BEFORE the checks that would
                // decline for them, and getWindowId re-populates the id caches
                // on a miss — the caches slotWindowClosed has just scrubbed for
                // a window not riding a close animation. The re-populated
                // mapping then waits for the windowDeleted backstop. The moves
                // and the daemon flush themselves do decline on their own
                // (scrollManagedOutputFor and flushPendingFrameGeometry both
                // refuse a deleted window); what they do not do is decline
                // before the lookup.
                if (!safeW || safeW->isDeleted()) {
                    return;
                }
                // Body -1.5 — answer a superseded configure's late ack. An
                // apply that asked for the size the client already had, while a
                // different size was still in flight, was applied by KWin with
                // no configure, so that older configure stayed outstanding and
                // its ack just landed the stale size (WindowCommandStamps::
                // StaleAck). Re-issue the command once: its size now differs
                // from the client's, so KWin sends a real configure. Void once
                // a newer command has landed or the user has the frame.
                if (const auto staleIt = m_daemonGate.commandStamps.staleAcks.find(safeW.data());
                    staleIt != m_daemonGate.commandStamps.staleAcks.end() && !m_daemonGate.inGeometryApply) {
                    const WindowCommandStamps::StaleAck stale = *staleIt;
                    const QSize committed = safeW->frameGeometry().toRect().size();
                    if (!m_daemonGate.commandStamps.isCurrent(safeW.data(), stale.stamp) || safeW->isUserMove()
                        || safeW->isUserResize()) {
                        m_daemonGate.commandStamps.staleAcks.erase(staleIt);
                    } else if (committed != stale.target.size()) {
                        m_daemonGate.commandStamps.staleAcks.erase(staleIt);
                        if (KWin::Window* kwStale = safeW->window()) {
                            qCInfo(lcEffect) << "Re-issuing" << stale.target << "over a superseded configure's ack"
                                             << committed << "for" << getWindowId(safeW.data());
                            const bool prevInApply = m_daemonGate.inGeometryApply;
                            m_daemonGate.inGeometryApply = true;
                            const auto staleGuard = qScopeGuard([this, prevInApply] {
                                m_daemonGate.inGeometryApply = prevInApply;
                            });
                            kwStale->moveResize(QRectF(stale.target));
                        }
                    }
                }
                // Body -1 — retarget a strip animation onto the rect the
                // client actually committed.
                //
                // Only fires for a client that did not take the geometry it
                // was handed. A programmatic moveResize commits synchronously,
                // so for an ordinary window frameGeometry() already equals the
                // animation's target by the time any leg is running and the
                // isAnimatingToTarget test short-circuits. A client with a
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
                if (m_windowAnimator->hasAnimation(safeW.data()) && scrollManagedOutputFor(safeW.data())
                    && m_daemonGate.animatedApplyCommit != safeW.data()) {
                    const QRectF committed = safeW->frameGeometry();
                    if (!committed.isEmpty() && !m_windowAnimator->isAnimatingToTarget(safeW.data(), committed)) {
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
                // Body -0.5 — centre a client that answered its column with
                // a different size.
                //
                // The strip offers the full column rect the first time it
                // sees a column SIZE, because until the client has answered
                // there is nothing else to offer. A client that will not take
                // that size commits its own at the column's top-left, so a
                // freshly inserted window — or one that just went full width —
                // hugs the top (or the left) until the next batch, which is
                // the first placement able to offer the settled size centred.
                //
                // Correct it here instead of waiting: the commit that just
                // arrived IS the answer, so the centred position is known now.
                // Measured rather than predicted, like every other placement
                // decision on this path — the alternative would be modelling
                // the client's own size rule, which is not knowable up front.
                //
                // A pure move(), so it cannot renegotiate the size it just
                // settled on and cannot be re-anchored by a queued configure.
                // Converges in one step: the guard compares against the
                // position it is about to install, so the synchronous
                // frameGeometryChanged this emits re-enters and does nothing.
                //
                // Cheapest test first, and the ordering is load-bearing for
                // cost rather than correctness. scrollManagedOutputFor memoises
                // only WITHIN a paint pass, and this lambda runs off the paint
                // cycle, so once any screen is scrolling — its own first test
                // is hasScrollingScreens, which costs nothing when none is —
                // every call pays a tracked-screen resolve, a float probe and
                // an output lookup uncached. Asking it before the
                // offered-column probe made every window's every geometry tick
                // pay that, including plain snap and autotile windows that miss
                // the map. The hash probe answers the common case for one
                // lookup. Body -1 above already orders it this way.
                if (!m_daemonGate.inGeometryApply && !m_scrollOfferedColumn.isEmpty()) {
                    const QString scrollId = getWindowId(safeW.data());
                    // Copied out, not held as an iterator across the predicate
                    // below. scrollManagedOutputFor does not touch this map
                    // today, so the iterator would survive — but it resolves a
                    // screen, a float verdict and an output, and a future
                    // reader has no reason to expect an unrelated call to be
                    // iterator-critical. The value is a QRect. The hit/miss
                    // answer is kept as its own bool rather than inferred from
                    // the copied rect, so the test stays exactly the one the
                    // iterator comparison made.
                    const auto colIt = m_scrollOfferedColumn.constFind(scrollId);
                    const bool haveOffer = colIt != m_scrollOfferedColumn.constEnd();
                    const QRect offered = haveOffer ? *colIt : QRect();
                    if (haveOffer && scrollManagedOutputFor(safeW.data())) {
                        const QRect live = safeW->frameGeometry().toRect();
                        if (live.size() != offered.size() && !live.size().isEmpty()) {
                            // Same centring as the strip apply and the paint
                            // resolver: the same toRect() rounding, and the
                            // same clamp at zero, so a frame whose minimum
                            // exceeds its column stays anchored at the column's
                            // origin rather than shifting past its edge.
                            //
                            // isEmpty rather than isValid: QSize::isValid()
                            // admits 0x0, which would centre a degenerate
                            // mid-unmap commit by the whole column.
                            const QPoint centred(offered.x() + qMax(0, offered.width() - live.width()) / 2,
                                                 offered.y() + qMax(0, offered.height() - live.height()) / 2);
                            if (live.topLeft() != centred && safeW->window()) {
                                // Bracketed like the effect's other commits
                                // (applies, centring). The move emits a synchronous
                                // frameGeometryChanged, which re-enters this
                                // signal's whole connection list from the top
                                // — including the virtual-screen crossing
                                // detector and the autotile reactive centring
                                // pass, both connected ahead of this lambda.
                                // Without the gate they treat a move the effect
                                // itself made as a user-driven one.
                                // Save/restore, not set/clear (nesting-safe).
                                const bool prevInApply = m_daemonGate.inGeometryApply;
                                m_daemonGate.inGeometryApply = true;
                                const auto restoreGate = qScopeGuard([this, prevInApply] {
                                    m_daemonGate.inGeometryApply = prevInApply;
                                });
                                safeW->window()->move(QPointF(centred));
                            }
                        }
                    }
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
                        // this branch, so a never-landing entry lives until
                        // the windowDeleted cleanup — bounded, and cheaper
                        // than a timer per entry).
                        const bool stale =
                            ShaderInternal::shaderClockNowMs() - pending.armedAtMs > kPendingMaximizeMorphDeadlineMs;
                        // Same interactive guard as the arming site: a drag
                        // that started while the entry was pending owns the
                        // visuals through the window.move shader.
                        if (!stale && !safeW->isUserMove() && !safeW->isUserResize()) {
                            beginMaximizeShaderMorph(safeW.data(), pending.departureFrame);
                        }
                    }
                }
                // Body 1 — suppression release. Integer-aligned compare:
                // fractional-scale outputs leave sub-pixel residue in
                // frameGeometry(), and a bit-exact inequality released the
                // suppression on jitter that moved nothing.
                if (auto it = m_restoreSuppress.find(safeW.data()); it != m_restoreSuppress.end()
                    && it->targetGeometry.isValid() && safeW->frameGeometry().toRect() != it->spawnGeometry.toRect()) {
                    endRestoreSuppression(safeW.data());
                }
                // Body 2 — debounced daemon shadow. Per tick this stashes the
                // latest geometry and runs ONLY the cheap decoration resync:
                // the shouldHandleWindow exclusion gate (an uncached rule
                // resolve over a freshly built ruleQuery) moved into
                // flushPendingFrameGeometry, so it runs once per 50ms flush
                // per window instead of on every geometry tick — animated
                // geometry (retiles, morphs, interactive resize) fired it
                // hundreds of times per second (discussion #816).
                const QString windowId = getWindowId(safeW);
                if (windowId.isEmpty()) {
                    return;
                }
                // Self-heal a noBorder reset KWin issues asynchronously after
                // a cross-OUTPUT move. For a rule-owned (title-bar-hidden)
                // window the manager already believes it hidden, so the
                // synchronous resync in updateAllDecorations bails ("still
                // suppressed") when it runs before KWin re-evaluates the
                // decoration. KWin grows the frame by the title-bar height
                // when it re-decorates, firing this very signal: resyncWindow
                // re-hides exactly the windows the manager owns and believes
                // hidden whose decoration drifted back, and is a self-guarding
                // no-op otherwise. Kept PER TICK, not behind the flush: it is
                // a hash lookup plus two flag checks for the untracked common
                // case, and deferring it to the flush let the re-decorated
                // title bar flash for up to the 50ms throttle window. No
                // shouldHandleWindow gate needed — the manager only ever owns
                // windows that passed it.
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
    // BEFORE the handler connections below. For SNAP the ordering matters on
    // the bus: the handler's float commit rides the same edge, and the push
    // must land first so the daemon's suspension classification reads fresh
    // minimize state. The AUTOTILE handler's float commit is debounced
    // (kMinimizeFloatDebounceMs), so for it the ordering guarantee comes from
    // that delay, not from connection order. The daemon's mode-swap
    // seed/restore decisions consult WindowMetadata::isMinimized, which would
    // otherwise remain at its previous snapshot until an unrelated refresh —
    // a stale value lets a mode-swap seed tile a window that is minimized
    // right now (the per-slot floating check cannot cover this: it resolves
    // via the screen's CURRENT mode, which flips mid-toggle).
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

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);

    // Refresh the registry on every urgency edge, for the same reason as the
    // minimize edge above: WindowMetadata::isDemandingAttention would
    // otherwise sit at whatever the last unrelated push snapshotted, and a
    // stale urgency is worse than none — the tab indicator would keep a tab
    // lit long after the window stopped asking for attention, or never light
    // it at all. The signal lives on KWin::Window, not EffectWindow, so this
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
                    // Urgency lights a compositor-drawn tab pill; same rebuild
                    // as the caption hook above.
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

} // namespace PlasmaZones
