// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>

namespace PlasmaZones {

DragTracker::DragTracker(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void DragTracker::handleWindowStartMoveResize(KWin::EffectWindow* w)
{
    // Stamp the compositor's own move/resize state BEFORE any tracking
    // filter: resizes and shouldHandleWindow-rejected windows are never
    // tracked below, but they still hold KWin's move filter, and
    // compositorMoveResizeActive() must answer for exactly that span.
    if (w) {
        m_interactiveWindow = w;
    }
    // Only track moves, not resizes
    if (!w || !w->isUserMove() || w->isUserResize()) {
        return;
    }

    if (!m_effect->shouldHandleWindow(w)) {
        return;
    }

    // Already tracking a drag — shouldn't happen (KWin: one interactive move at a time)
    if (m_draggedWindow) {
        return;
    }

    m_draggedWindow = w;
    m_draggedWindowId = m_effect->getWindowId(w);
    m_lastCursorPos = KWin::effects->cursorPos();
    m_dragMovedThrottle.start();

    qCInfo(lcEffect) << "Window move started -" << w->windowClass();
    Q_EMIT dragStarted(w, m_draggedWindowId, w->frameGeometry());
}

void DragTracker::noteWiredWindowMoveState(KWin::EffectWindow* w)
{
    // Deliberately does NOT start tracking a drag: this window's move began
    // before we were listening, so there was no dragStarted for it and
    // synthesising one here would emit a drag-start every consumer would have
    // to unwind at a drop it never saw begin. Only the compositor-state stamp
    // is recovered, which is what the input-interception guards read.
    if (!w || m_interactiveWindow) {
        return;
    }
    if (w->isUserMove() || w->isUserResize()) {
        m_interactiveWindow = w;
        qCInfo(lcEffect) << "Adopting in-flight interactive move at wiring time -" << w->windowClass();
    }
}

void DragTracker::handleWindowFinishMoveResize(KWin::EffectWindow* w)
{
    // The compositor's move is over — this signal is KWin's truth, unlike
    // forceEnd(), which fires on LMB release while KWin can still be
    // holding the move for other buttons.
    if (w && w == m_interactiveWindow) {
        m_interactiveWindow = nullptr;
    }
    // Not our window — either already ended by forceEnd(), or was a resize we
    // didn't track. The explicit null test matters: with no drag tracked,
    // m_draggedWindow is null too, so a null w would compare equal and fall
    // through to finishDrag, emitting dragStopped(nullptr, empty id) at every
    // consumer for a drag that never existed.
    if (!w || w != m_draggedWindow) {
        return;
    }

    // forceEnd() handles normal drag end (LMB release). If we get here, the move was
    // cancelled externally (Escape key, compositor ended it, fullscreen transition).
    qCInfo(lcEffect) << "Window move cancelled (finished without button release)";
    finishDrag(/*cancelled=*/true);
}

void DragTracker::forceEnd(const QPointF& cursorPos)
{
    if (!m_draggedWindow) {
        return;
    }

    qCInfo(lcEffect) << "Force-ending drag (button released)";

    m_lastCursorPos = cursorPos;
    finishDrag(/*cancelled=*/false);
}

void DragTracker::updateCursorPosition(const QPointF& cursorPos)
{
    if (!m_draggedWindow) {
        return;
    }
    // Always track latest position for forceEnd()/callDragStopped() to use
    m_lastCursorPos = cursorPos;
    // Throttle dragMoved signals to ~30Hz. slotMouseChanged fires at input
    // device rate (often 1000Hz on gaming mice); sending a D-Bus call for
    // every pixel of movement would add ~10-50μs of message serialization
    // per event on the compositor thread — far more than needed for zone
    // detection which has no perceptible benefit above 30fps.
    if (m_dragMovedThrottle.elapsed() >= 32) {
        m_dragMovedThrottle.start();
        Q_EMIT dragMoved(m_draggedWindowId, cursorPos);
    }
}

void DragTracker::finishDrag(bool cancelled)
{
    // Copy raw pointer before clearing — QPointer auto-nulls when the window is destroyed
    auto* windowToSnap = m_draggedWindow.data();
    QString windowIdToSnap = m_draggedWindowId;

    // Clear state first to prevent re-entry issues
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();

    Q_EMIT dragStopped(windowToSnap, windowIdToSnap, cancelled);
}

void DragTracker::handleWindowClosed(KWin::EffectWindow* window)
{
    // A closed window cannot hold KWin's move filter, but its EffectWindow
    // outlives the close (Deleted), so the QPointer would not auto-null.
    if (window && window == m_interactiveWindow) {
        m_interactiveWindow = nullptr;
    }
    if (m_draggedWindow == window) {
        qCInfo(lcEffect) << "Drag: window closed, cancelled";
        // Don't call finishDrag() — it would pass the mid-destruction window pointer
        // through dragStopped, causing use-after-free in callDragStopped's geometry queries.
        // Instead, clear state and emit with nullptr so the receiver skips the snap.
        QString windowIdToCancel = m_draggedWindowId;
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();
        Q_EMIT dragStopped(nullptr, windowIdToCancel, /*cancelled=*/true);
    }
}

/// Silent state wipe: clears the tracked window WITHOUT emitting dragStopped.
/// ZERO callers today. Kept as the teardown a destructor or a hard
/// compositor-loss path would want, where there is no live consumer left to
/// receive the signal — but a caller that reaches for it during a NORMAL drag
/// would strand every downstream consumer waiting on dragStopped. Use
/// forceEnd() for that.
///
/// Note for any future caller: clearing m_interactiveWindow here does not end
/// the compositor's move. If KWin is still holding one, compositorMoveResizeActive()
/// starts answering false while the move filter is live, which re-opens the
/// interception-steal window the guard exists to close. A compositor-loss path
/// (no compositor, no move) is fine; a mid-gesture caller is not.
void DragTracker::reset()
{
    m_interactiveWindow = nullptr;
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();
    m_lastCursorPos = QPointF();
    m_dragMovedThrottle.invalidate();
}

} // namespace PlasmaZones
