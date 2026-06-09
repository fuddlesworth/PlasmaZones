// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorAnimation/StaggerTimer.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <core/output.h>

#include <QLoggingCategory>

#include "../autotilehandler.h"
#include "../dragtracker.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    Q_UNUSED(oldpos)
    Q_UNUSED(oldmodifiers)

    const bool modifiersChanged = (m_currentModifiers != modifiers);
    const bool buttonsChanged = (oldbuttons != buttons);

    if (buttonsChanged && m_dragTracker->isDragging()) {
        qCInfo(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->"
                         << static_cast<int>(buttons);
    }

    if (modifiersChanged) {
        m_currentModifiers = modifiers;
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);
    }
    m_currentMouseButtons = buttons;

    if (m_dragTracker->isDragging()) {
        if ((oldbuttons & Qt::LeftButton) && !(buttons & Qt::LeftButton)) {
            // Primary button released = drag is over. Force-end regardless of whether
            // other buttons (e.g. right-click for zone activation) are still held.
            //
            // KWin keeps isUserMove() true while any button is held, so
            // windowFinishUserMovedResized wouldn't fire until ALL buttons are
            // released. forceEnd() gives immediate snap response on LMB release.
            //
            // After forceEnd, applySnapGeometry will defer (retry every 100 ms)
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
            const bool bypassed = m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen
                || m_dragBypassedForAutotile;
            const bool shouldForward =
                bypassed || detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded;
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
            if (m_daemonServiceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                    {effectiveScreenId});
            }
        }
    }

    // Focus follows mouse: activate autotile window under cursor when not dragging.
    // Reuse effectiveScreenId computed above to avoid redundant resolveEffectiveScreenId call.
    if (!m_dragTracker->isDragging() && output) {
        m_autotileHandler->handleCursorMoved(pos, effectiveScreenId);
        // Snapping FFM runs alongside autotile FFM. The two are disjoint: autotile FFM
        // bails when the cursor screen is not an autotile screen, and snapping FFM only
        // acts on windows in the snap tiled set (which live on snapping-mode screens), so
        // at most one of them activates a window for any given cursor position.
        handleSnapCursorMoved(pos, effectiveScreenId);
    }
}

void PlasmaZonesEffect::handleSnapCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_snappingFocusFollowsMouse) {
        return;
    }

    // Pause FFM while a transient/popup/special window is active so hovering a
    // snapped window beneath it does not dismiss it — e.g. an emoji picker or
    // notification opened over a snapped window, where moving the cursor across
    // the underlying window's exposed area would otherwise activate it and send
    // the popup to the background. A snapped or normal tileable active window
    // does not pause FFM. Scoped to the cursor's screen (mirrors
    // AutotileHandler::handleCursorMoved, discussion #461): a transient window
    // active on another monitor must not freeze FFM on the monitor the cursor is
    // on. Our own full-screen overlays never count as the kind of active window
    // worth protecting.
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        // Cheap overlay-class check first, then the heavier screen resolution
        // (mirrors the autotile guard's predicate ordering). The predicate is
        // deliberately wider than the under-cursor occlusion guard below: there
        // we only look *through* a snapped window, but here a normal tileable
        // active window (a regular app the user is working in, not a popup) must
        // not pause FFM either. Only a non-snapped, non-tileable active window
        // (dialog/popup/excluded app) is worth protecting. Both of autotile's
        // guards key off the same tileable/shouldHandle membership; snap's managed
        // set (snapped) is narrower than tileable, so here the pause guard accepts
        // the extra isTileableWindow case the occlusion guard below does not.
        if (!isOwnOverlayClass(active->windowClass()) && getWindowScreenId(active) == screenId
            && !isWindowMarkedSnapped(getWindowId(active)) && !isTileableWindow(active)) {
            return;
        }
    }

    // Find the topmost snapped window under the cursor (stacking order top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        if (!w || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        // Cheap geometry test before the windowClass()/windowId allocations below.
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through our own overlay/editor layer-shell surfaces — they are full-screen
        // and always topmost, so a bail here would kill FFM whenever an overlay is up
        // (mirrors the autotile FFM guard).
        if (isOwnOverlayClass(w->windowClass())) {
            continue;
        }
        // The window directly under the cursor is not snapped (a floating dialog, popup,
        // or excluded app occluding a snapped window beneath). Don't look through it to
        // focus the snapped window — that would steal focus from what the user is pointing
        // at. Mirrors AutotileHandler::handleCursorMoved's occlusion guard.
        if (!isWindowMarkedSnapped(getWindowId(w))) {
            return;
        }
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op.
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete)
{
    // Convert the D-Bus-sourced int to the typed enum at this boundary;
    // the library API only accepts SequenceMode. Unknown ints fall back
    // to AllAtOnce — same behaviour as Profile::fromJson.
    const PhosphorAnimation::SequenceMode mode =
        (m_cachedAnimationSequenceMode == static_cast<int>(PhosphorAnimation::SequenceMode::Cascade))
        ? PhosphorAnimation::SequenceMode::Cascade
        : PhosphorAnimation::SequenceMode::AllAtOnce;
    PhosphorAnimation::applyStaggeredOrImmediate(this, count, mode, m_cachedAnimationStaggerInterval, applyFn,
                                                 onComplete);
}

} // namespace PlasmaZones
