// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DragTracker wiring, split out of lifecycle_wiring.cpp along its own
// comment seam (the connectDragTracker block is a self-contained connect
// set): the drag lifecycle fan-out — dragStarted / dragMoved / dragStopped —
// plus the drag-policy activation handshake. Called from the ctor shell in
// the same position connectDragTracker always held.

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>

#include <effect/effecthandler.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QPointer>

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/snaphandler.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY.

void PlasmaZonesEffect::connectDragTracker()
{
    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);

                // Capture the floating state at drag start, before any float
                // transition (the autotile-bypass fast path below floats tiled
                // windows). The drag-stop ApplyFloat path uses this to decide
                // whether to restore the pre-autotile size: a window that was
                // already floating is just being moved and must keep its current
                // user-chosen size, not snap back to the stale pre-autotile rect.
                m_dragActivation.startedFloating = isWindowFloating(windowId);
                m_dragActivation.tileSuspended = false;

                // Note: `cursor.drag` is intentionally NOT wired here. The
                // OffscreenEffect pipeline operates on window content; firing
                // a shader at drag start through it is indistinguishable from
                // `window.move`, and synchronously colliding with the
                // `windowStartUserMovedResized` lambda's `window.move` install
                // means whichever fires second wins (it would be `window.move`
                // here). The `cursor` class (`ProfilePaths::Cursor`, with its
                // `CursorHover` / `CursorClick` leaves) is reserved for a future
                // cursor-decoration / drag-shadow surface and carries no drag leaf.

                // Fire beginDrag async to get a daemon-authoritative policy.
                // While the reply is pending, we
                // default m_currentDragPolicy to a conservative snap-path so
                // the worst case (stale effect cache would have said autotile
                // but daemon knows better, or vice-versa) is a brief overlay
                // flash rather than a dead drag. The reply handler flips the
                // bypass flag retroactively a few ms later if the daemon says
                // this is an autotile drag.
                //
                // This replaces the previous stale-cache read of
                // m_tilingHandler->isManagedScreen() as the single source
                // of truth for drag-start routing — root cause of the
                // post-settings-reload dead-drag window found in #310 log
                // forensics.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_currentDragPolicy.streamDragMoved = true;
                m_currentDragPolicy.showOverlay = true;
                m_currentDragPolicy.grabKeyboard = true;
                m_currentDragPolicy.captureGeometry = true;

                // Bump the per-drag generation and capture the value so the
                // async reply below can detect a stale reply (drag ended
                // before reply arrived, or a new drag started in the gap).
                ++m_dragActivation.generation;
                const quint64 capturedDragGeneration = m_dragActivation.generation;
                const QString startScreenId = getWindowScreenId(w);
                // With PlasmaZones not running there is nothing for Escape to
                // cancel and no answer coming to release a grab, so neither the
                // call nor either grab below is made.
                const bool daemonUp = m_daemonGate.serviceRegistered;
                const QRect frame = geometry.toRect();
                if (daemonUp) {
                    auto* beginWatcher = new QDBusPendingCallWatcher(
                        PhosphorProtocol::ClientHelpers::asyncCall(
                            PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("beginDrag"),
                            {windowId, frame.x(), frame.y(), frame.width(), frame.height(), startScreenId,
                             static_cast<int>(m_currentMouseButtons)}),
                        this);
                    QPointer<KWin::EffectWindow> safeW = w;
                    const QString capturedWindowId = windowId;
                    connect(beginWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, safeW, capturedWindowId, capturedDragGeneration](QDBusPendingCallWatcher* bw) {
                                bw->deleteLater();
                                QDBusPendingReply<PhosphorProtocol::DragPolicy> reply = *bw;
                                if (!reply.isValid()) {
                                    qCWarning(lcEffect) << "beginDrag reply invalid:" << reply.error().message();
                                    // An unanswered drag releases the optimistic grab: no
                                    // policy will ever confirm it.
                                    if (m_dragActivation.generation == capturedDragGeneration && m_keyboardGrabbed
                                        && KWin::effects) {
                                        KWin::effects->ungrabKeyboard();
                                        m_keyboardGrabbed = false;
                                    }
                                    return;
                                }
                                const PhosphorProtocol::DragPolicy policy = reply.value();
                                if (const QString err = policy.validationError(); !err.isEmpty()) {
                                    qCWarning(lcEffect)
                                        << "beginDrag reply rejected:" << err
                                        << "— keeping conservative snap-path policy for" << capturedWindowId;
                                    return;
                                }
                                // Discard stale replies: the drag this call dispatched
                                // for has already ended (or a new drag started in the
                                // interim) — writing the captured policy now would
                                // bleed it into the active drag's state.
                                if (m_dragActivation.generation != capturedDragGeneration) {
                                    qCInfo(lcEffect)
                                        << "beginDrag reply discarded: drag generation" << capturedDragGeneration
                                        << "is stale (current=" << m_dragActivation.generation << ") for"
                                        << capturedWindowId;
                                    return;
                                }
                                const PhosphorProtocol::DragBypassReason oldReason = m_currentDragPolicy.bypassReason;
                                m_currentDragPolicy = policy;
                                qCInfo(lcEffect) << "beginDrag reply:" << capturedWindowId
                                                 << "bypass=" << m_currentDragPolicy.bypassReason
                                                 << "stream=" << m_currentDragPolicy.streamDragMoved
                                                 << "immediateFloat=" << m_currentDragPolicy.immediateFloatOnStart
                                                 << "grab=" << m_currentDragPolicy.grabKeyboard;
                                // The correction layer for the fast path below, both ways:
                                // a start it missed enters the bypass (and floats a tile the
                                // policy floats on drag), one it latched from a stale cache
                                // leaves it, and the keyboard grab follows the answer.
                                applyDragPolicyTransition(safeW, capturedWindowId, oldReason,
                                                          DragPolicyTransition::Source::BeginDragReply);
                            });
                }

                // Symmetric with the dragStopped re-drive: the pill hover guard
                // goes inert for the whole drag, and without this a hover (or
                // its interception) held at drag START stays latched until the
                // first motion event opens the guard — one event of lag for a
                // pointer drag, the whole drag for a keyboard-initiated move
                // whose pointer never moves. BEFORE the managed-screen fast
                // path below: scrolling screens are a subset of managed screens,
                // so a tail call after its early return could never run for any
                // screen that actually has pills.
                if (KWin::effects) {
                    m_tilingHandler->updateScrollTabHover(KWin::effects->cursorPos());
                }

                // Fast path: the effect-side autotile cache is USUALLY correct.
                // We still consult it synchronously so the common case runs at
                // zero latency. The async beginDrag reply above runs as a
                // correction layer for the cases where the cache is stale
                // (post-settings-reload — the #310 scenario).
                if (m_tilingHandler->isManagedScreen(startScreenId)) {
                    m_dragBypassedForEngine = true;
                    m_dragBypassScreenId = startScreenId;
                    // Reorder mode: the daemon owns drag-insert preview for tile
                    // swapping. Skip the synchronous float transition — we want
                    // the tile to stay visually in place while the daemon runs
                    // moveToTiledPosition on each cursor tick. The effect still
                    // flips into bypass state so snap-path logic is suppressed.
                    //
                    // Scrolling screens are excluded: the setting is the AUTOTILE
                    // drag behaviour, and there is no drag-insert preview for the
                    // strip — the daemon's scroll branch unconditionally answers
                    // immediateFloatOnStart for a tracked window. Letting a global
                    // Reorder suppress the synchronous float on a scrolling screen
                    // only deferred it to the async beginDrag reply, so the user
                    // dragged a borderless strip-sized tile for the round trip,
                    // which is the exact deferred-visual defect this fast path
                    // exists to prevent.
                    const bool reorderMode = !m_tilingHandler->isScrollingScreen(startScreenId)
                        && m_cachedAutotileDragBehavior == EffectAutotileDragBehavior::Reorder;
                    // If the window is currently autotile-tiled, restore its
                    // title bar and pre-autotile size NOW (synchronously, during
                    // the interactive move). This mirrors snap mode, where
                    // dragging a snapped window out of its zone visibly restores
                    // the free-floating size before release — without this, the
                    // user drags a borderless tile-sized window and only sees it
                    // become a floating window after they drop.
                    //
                    // Guarded on isTrackedWindow so we don't touch windows that
                    // are already floating (not in the autotile tree).
                    if (!reorderMode && m_tilingHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId)) {
                        m_tilingHandler->handleDragToFloat(w, windowId, /*immediate=*/true);
                        // Mark as drag-floated so the daemon's pre-tile geometry
                        // restore (applyGeometryForFloat, triggered by the
                        // setWindowFloatingForScreen call at drop) is skipped in
                        // slotApplyGeometryRequested — the window should stay
                        // where the user drops it, not snap back to a stored rect.
                        m_dragActivation.floatedWindowIds.insert(windowId);
                    }
                    // Grab OPTIMISTICALLY before leaving. This early return used to
                    // skip the grab unconditionally, which was right when an engine
                    // drag had no overlay and nothing Escape could cancel — the
                    // comment that introduced it said exactly that ("the drag
                    // proceeds freely", Feb 2026). Drag-insert previews and the drop
                    // indicator gave it both, so under always-on re-insert Escape
                    // has to reach the daemon from the drag's very first tick, and
                    // the daemon's answer is still in flight here.
                    //
                    // No policy test: the field is still the conservative default
                    // written above (grabKeyboard = true). The reply's transition
                    // reconciles the grab with the daemon's answer, and an error
                    // reply releases it, so an engine drag outside always-on
                    // re-insert holds it only for the round trip.
                    if (daemonUp && !m_keyboardGrabbed && KWin::effects) {
                        // See the reply lambda: grabKeyboard returns false when
                        // another effect owns the grab, and claiming it anyway
                        // makes drag-end release someone else's.
                        m_keyboardGrabbed = KWin::effects->grabKeyboard(this);
                        if (!m_keyboardGrabbed) {
                            qCWarning(lcEffect) << "dragStarted: keyboard grab refused (another effect holds it) for"
                                                << windowId << "- Escape will reach KWin's move filter";
                        }
                    }
                    return;
                }
                m_dragBypassedForEngine = false;
                m_dragActivation.detected = false;

                // beginDrag already initialized daemon-side snap-drag state
                // (called internally from the adaptor). Called for its LATCH, not
                // its answer: it sets m_dragActivation.detected so the per-tick
                // gates downstream keep forwarding after a mid-drag release. The
                // grab below is unconditional and is this path's own, which is
                // why the predicate no longer takes one.
                shouldForwardDragTicks();
                // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
                // Without this, Escape cancels the interactive move AND the overlay.
                // With the grab, Escape only dismisses the overlay while the drag continues.
                if (daemonUp && !m_keyboardGrabbed && KWin::effects) {
                    // Same answer-respecting capture as the two sites above.
                    m_keyboardGrabbed = KWin::effects->grabKeyboard(this);
                    if (!m_keyboardGrabbed) {
                        qCWarning(lcEffect) << "dragStarted: keyboard grab refused (another effect holds it) for"
                                            << windowId << "- Escape will cancel the interactive move";
                    }
                }
            });
    connect(m_dragTracker.get(), &DragTracker::dragMoved, this,
            [this](const QString& windowId, const QPointF& cursorPos) {
                // Cross-VS flip detection is daemon-owned. The
                // daemon's updateDragCursor handler computes policy at the
                // cursor position and emits dragPolicyChanged when it flips.
                // The effect reacts via slotDragPolicyChanged (see below).
                //
                // Here we only forward the cursor to the daemon as a
                // fire-and-forget call. The daemon-side dispatch handles
                // both the snap-path overlay updates and the cross-VS
                // detection in a single round trip.

                if (!m_daemonGate.serviceRegistered) {
                    return;
                }
                // In autotile bypass — skip snap zone processing locally;
                // the daemon's updateDragCursor still watches for a flip
                // BACK to snap mode.
                const bool bypassed =
                    m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen
                    || m_dragBypassedForEngine;
                if (!bypassed) {
                    // Gate D-Bus calls on activation trigger state so a drag
                    // without any intent to use zones doesn't flood the bus
                    // at 30Hz. This is a local input-event optimization; it
                    // isn't policy and doesn't come from the daemon.
                    if (!shouldForwardDragTicks() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                        return;
                    }
                }

                // Forward the cursor to the daemon. For snap drags, this
                // drives overlay/zone detection. For bypass drags, the
                // daemon watches the cursor for a cross-VS flip and emits
                // dragPolicyChanged when the policy changes.
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                    {windowId, qRound(cursorPos.x()), qRound(cursorPos.y()), static_cast<int>(m_currentModifiers),
                     static_cast<int>(m_currentMouseButtons)},
                    QStringLiteral("updateDragCursor"));
            });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end. Effects
                // guard mirrors the dragStarted lambda: DragTracker signals
                // are less teardown-exposed than D-Bus replies, but the two
                // lambdas should not drift.
                if (m_keyboardGrabbed && KWin::effects) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }

                // Clear the drag-floated marker on every drag end. Historically
                // this marker was used to suppress a post-drag pre-tile geometry
                // restore (applyGeometryForFloat), but the current daemon-side
                // drag-end path goes through AutotileEngine::setWindowFloat →
                // windowFloatingStateSynced → syncAutotileFloatStatePassive,
                // which never emits applyGeometryForFloat. Leaving the marker
                // set after a drag leaks it into subsequent Meta+F toggles:
                // the next user float is silently skipped, the window's visual
                // position diverges from the daemon's shadow, and then a
                // float→tile toggle overwrites the stored pre-tile rect with
                // the stale tile zone — permanently corrupting the restore
                // target (#bug: zed/firefox/plasmazones-settings resize issues).
                //
                // Capture the fact BEFORE the erase: the endDrag reply arrives
                // asynchronously, and the arms that apply no outcome (NoOp,
                // CancelSnap, error, timeout) must know whether the effect
                // floated this window at drag start so they can revoke that
                // optimistic write instead of leaving the float cache latched
                // on a window the daemon kept tiled.
                const bool effectFloatedThisDrag = m_dragActivation.floatedWindowIds.contains(windowId);
                m_dragActivation.floatedWindowIds.remove(windowId);

                // Single entry point for drag-end dispatch. The
                // daemon owns the decision; callEndDrag sends endDrag and
                // the reply handler applies whatever PhosphorProtocol::DragOutcome comes back
                // (ApplySnap / ApplyFloat / RestoreSize / NoOp / etc.).
                //
                // A crossing mid-drag only re-latched the bypass (and, back on
                // the snap path, suspended the tile); the drop-time action is
                // the PhosphorProtocol::DragOutcome, and callEndDrag settles a
                // suspended tile once it has the answer.
                callEndDrag(w, windowId, cancelled, effectFloatedThisDrag);

                // Bump the per-drag generation so any in-flight beginDrag
                // reply for the drag we just ended is discarded by the
                // reply lambda's generation check. Without this bump, the
                // mismatch check only fires when a NEW drag starts before
                // the reply arrives — a drag that ends WITHOUT a successor
                // would leave the captured generation equal to the current
                // value, the reply would pass the guard, and write its
                // policy + retroactive autotile float into stale state.
                ++m_dragActivation.generation;

                // Clear drag state for the next session.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_dragBypassedForEngine = false;
                m_dragBypassScreenId.clear();
                m_dragActivation.detected = false;

                // The pill hover guard held the tab indicators inert for the
                // whole drag (updateScrollTabHover's isDragging branch), and
                // hover is otherwise motion-driven: a drop that lands the
                // pointer over a pill with no further motion would leave it
                // unlit and uninterceptable until the next twitch. DragTracker
                // clears its drag state before emitting, so isDragging() is
                // already false here. That is not the whole guard: on a drop
                // with another button still held, KWin's move is still live and
                // updateScrollTabHover declines on compositorMoveResizeActive()
                // instead, deliberately. The re-drive for that case runs from
                // the finish signal (window_connections.cpp), once the
                // compositor's move genuinely ends — so this call doing nothing
                // on a multi-button drop is the design, not a dead call.
                if (KWin::effects) {
                    m_tilingHandler->updateScrollTabHover(KWin::effects->cursorPos());
                }
            });
}

} // namespace PlasmaZones
