// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragtracker.h"
#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

DragTracker::DragTracker(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void DragTracker::handleWindowStartMoveResize(KWin::EffectWindow* w)
{
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

void DragTracker::handleWindowFinishMoveResize(KWin::EffectWindow* w)
{
    // Not our window — either already ended by forceEnd(), or was a resize we didn't track
    if (w != m_draggedWindow) {
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
    KWin::EffectWindow* windowToSnap = m_draggedWindow;
    QString windowIdToSnap = m_draggedWindowId;

    // Clear state first to prevent re-entry issues
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();

    Q_EMIT dragStopped(windowToSnap, windowIdToSnap, cancelled);
}

void DragTracker::handleWindowClosed(KWin::EffectWindow* window)
{
    if (m_draggedWindow == window) {
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();
    }
}

void DragTracker::reset()
{
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();
    m_lastCursorPos = QPointF();
    m_dragMovedThrottle.invalidate();
}

} // namespace PlasmaZones
