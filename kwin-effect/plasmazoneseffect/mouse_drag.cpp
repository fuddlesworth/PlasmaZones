// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <PhosphorAnimation/StaggerTimer.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <core/output.h>

#include <QLoggingCategory>

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/snaphandler.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    // modifiersChanged selects the BRANCH below (a real keyboard transition
    // forwards a modifier-change D-Bus call; pointer noise must not), and
    // the event's own before/after pair is the only honest signal for that.
    // KWin (verified against 6.7.3's three mouseChanged emit sites) passes
    // its live modifier cache for BOTH slots on pointer/button events and
    // the real (new, old) pair only on keyboardModifiersChanged — it never
    // reports a spurious 0 while a key is held.
    const bool modifiersChanged = (modifiers != oldmodifiers);
    const bool buttonsChanged = (oldbuttons != buttons);

    // Wake any hover-reactive decoration. Its repaint driver stands down once the folded
    // cursor matches the live one (otherwise it re-folded the whole chain at vsync forever
    // with the pointer parked), and the cursor cache it compares against is only refreshed
    // inside prePaintScreen — so without a kick from HERE, a pointer move over a
    // hardware-cursor plane schedules no frame, nothing notices the cursor moved, and the
    // highlight freezes until something unrelated damages the screen. This is the only
    // cursor-position signal the effect gets.
    if (pos != oldpos) {
        repaintHoverDecorations(pos);
    }

    if (buttonsChanged && m_dragTracker->isDragging()) {
        qCDebug(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->"
                          << static_cast<int>(buttons);
    }

    // The caches are assigned UNCONDITIONALLY, like the buttons always were:
    // `modifiers` is KWin's authoritative live state on all three emit
    // paths, so every event is a free resync. Gating the write on
    // modifiersChanged made the cache write-only-on-transition, and with
    // key-repeat suppressed compositor-side a modifier held at effect load
    // (or across a compositor restart) had NO second event to correct the
    // NoModifier initializer — and KWin exposes no modifier accessor to
    // seed from.
    m_currentModifiers = modifiers;
    m_currentMouseButtons = buttons;
    if (modifiersChanged) {
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);
    }

    if (m_dragTracker->isDragging()) {
        if ((oldbuttons & Qt::LeftButton) && !(buttons & Qt::LeftButton)) {
            // Primary button released = drag is over. Force-end regardless of whether
            // other buttons (e.g. right-click for zone activation) are still held.
            //
            // KWin keeps isUserMove() true while any button is held, so
            // windowFinishUserMovedResized wouldn't fire until ALL buttons are
            // released. forceEnd() gives immediate snap response on LMB release.
            //
            // After forceEnd, applyWindowGeometry will defer (retry every 100 ms)
            // until isUserMove() clears when the remaining buttons are released.
            m_dragTracker->forceEnd(pos);
        } else if (modifiersChanged || buttonsChanged) {
            // Push modifier/button changes to daemon during drag immediately.
            // This includes activation button press/release — the daemon's
            // lazy snap-drag activation uses these modifiers to decide when
            // to promote a pending drag to active (first tick with trigger
            // held) and when to hide the overlay (trigger released).
            //
            // For bypass (autotile) drags, modifier changes must also flow
            // so the daemon's autotile drag-insert rising-edge detection
            // (hold and toggle modes) can fire without requiring cursor
            // motion. Without this, tapping the trigger while stationary
            // was silently dropped.
            //
            // The daemon's updateDragCursor is cheap for pending drags
            // (returns early without running dragMoved), so the rapid fire
            // of modifier-change events during a drag no longer causes the
            // overlay destroy/create churn that prompted discussion #310's
            // sibling regression.
            const bool bypassed =
                m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen
                || m_dragBypassedForEngine;
            const bool shouldForward =
                bypassed || shouldForwardDragTicks() || m_cachedZoneSelectorEnabled || !m_triggersLoaded;
            if (shouldForward) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                    {m_dragTracker->draggedWindowId(), qRound(pos.x()), qRound(pos.y()),
                     static_cast<int>(m_currentModifiers), static_cast<int>(m_currentMouseButtons)},
                    QStringLiteral("updateDragCursor - modifier/button change"));
            }
        } else {
            // Position-only change: drive cursor tracking through DragTracker's
            // event-driven path. This eliminates QTimer jitter from the compositor
            // frame path — updates arrive at input-device cadence (throttled to
            // ~30Hz inside DragTracker to avoid D-Bus flooding).
            m_dragTracker->updateCursorPosition(pos);
        }
    }

    // Track which screen the cursor is on for shortcut screen detection.
    // Only send a D-Bus call when the cursor actually crosses to a different monitor
    // (or virtual screen), not on every pixel move. This gives the daemon accurate
    // cursor-based screen info on Wayland where QCursor::pos() is unreliable for
    // background processes.
    const QPoint roundedPos(qRound(pos.x()), qRound(pos.y()));
    auto* output = KWin::effects->screenAt(roundedPos);
    QString connectorName;
    QString effectiveScreenId;
    if (output) {
        connectorName = output->name();
        // Resolve to virtual screen ID if subdivisions exist
        effectiveScreenId = resolveEffectiveScreenId(roundedPos, output);
        if (effectiveScreenId != m_lastEffectiveScreenId) {
            m_lastEffectiveScreenId = effectiveScreenId;
            m_lastCursorOutput = connectorName;
            if (m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                    {effectiveScreenId});
            }
        }
    }

    // Focus follows mouse: activate autotile window under cursor when not dragging.
    // Reuse effectiveScreenId computed above to avoid redundant resolveEffectiveScreenId call.
    if (!m_dragTracker->isDragging() && output) {
        m_tilingHandler->handleCursorMoved(pos, effectiveScreenId);
        // Snapping FFM runs alongside autotile FFM. The two are disjoint: autotile FFM
        // bails when the cursor screen is not an autotile screen, and snapping FFM only
        // acts on windows in the snap tiled set (which live on snapping-mode screens), so
        // at most one of them activates a window for any given cursor position.
        m_snapHandler->handleCursorMoved(pos, effectiveScreenId);
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete, bool forceImmediate)
{
    // Convert the D-Bus-sourced int to the typed enum at this boundary;
    // the library API only accepts SequenceMode. Unknown ints fall back
    // to AllAtOnce — same behaviour as Profile::fromJson.
    //
    // forceImmediate overrides the user's choice rather than being folded into
    // it: the caller is not expressing a preference, it is stating that this
    // batch cannot be split without tearing, so the setting has nothing to say.
    const PhosphorAnimation::SequenceMode mode =
        (!forceImmediate && m_cachedAnimationSequenceMode == static_cast<int>(PhosphorAnimation::SequenceMode::Cascade))
        ? PhosphorAnimation::SequenceMode::Cascade
        : PhosphorAnimation::SequenceMode::AllAtOnce;
    PhosphorAnimation::applyStaggeredOrImmediate(this, count, mode, m_cachedAnimationStaggerInterval, applyFn,
                                                 onComplete);
}

} // namespace PlasmaZones
