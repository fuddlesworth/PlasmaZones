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

void DragTracker::pollWindowMoves()
{
    // Check all windows for user move state
    const auto windows = KWin::effects->stackingOrder();

    KWin::EffectWindow* movingWindow = nullptr;

    for (KWin::EffectWindow* w : windows) {
        if (w && w->isUserMove() && !w->isUserResize() && m_effect->shouldHandleWindow(w)) {
            movingWindow = w;
            break;
        }
    }

    // Clear force-end suppression once the window stops moving
    if (m_forceEndedWindow && (!movingWindow || movingWindow != m_forceEndedWindow)) {
        m_forceEndedWindow = nullptr;
    }

    // Detect start of drag
    if (movingWindow && !m_draggedWindow) {
        // Suppress re-entry after forceEnd() — this window's drag was already ended
        if (movingWindow == m_forceEndedWindow) {
            return;
        }

        m_draggedWindow = movingWindow;
        m_draggedWindowId = m_effect->getWindowId(movingWindow);
        m_lastCursorPos = KWin::effects->cursorPos();

        qCInfo(lcEffect) << "Window move started -" << movingWindow->windowClass();
        Q_EMIT dragStarted(movingWindow, m_draggedWindowId, movingWindow->frameGeometry());
    }
    // Detect ongoing drag - send position updates
    else if (movingWindow && movingWindow == m_draggedWindow) {
        // Re-check if window has gone fullscreen during drag
        if (movingWindow->isFullScreen()) {
            qCInfo(lcEffect) << "Window went fullscreen mid-drag, stopping tracking";
            m_draggedWindow = nullptr;
            m_draggedWindowId.clear();
            return;
        }
        QPointF cursorPos = KWin::effects->cursorPos();
        if (cursorPos != m_lastCursorPos) {
            m_lastCursorPos = cursorPos;
            Q_EMIT dragMoved(m_draggedWindowId, cursorPos);
        }
    }
    // Detect end of drag
    else if (!movingWindow && m_draggedWindow) {
        qCInfo(lcEffect) << "Window move finished (isUserMove went false)";
        finishDrag();
    }
}

void DragTracker::forceEnd(const QPointF& cursorPos)
{
    if (!m_draggedWindow) {
        return;
    }

    qCInfo(lcEffect) << "Force-ending drag (button released)";

    m_lastCursorPos = cursorPos;
    m_forceEndedWindow = m_draggedWindow;

    finishDrag();
}

void DragTracker::finishDrag()
{
    KWin::EffectWindow* windowToSnap = m_draggedWindow;
    QString windowIdToSnap = m_draggedWindowId;

    // Clear state first to prevent re-entry issues
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();

    Q_EMIT dragStopped(windowToSnap, windowIdToSnap);
}

void DragTracker::handleWindowClosed(KWin::EffectWindow* window)
{
    if (m_draggedWindow == window) {
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();
    }
    if (m_forceEndedWindow == window) {
        m_forceEndedWindow = nullptr;
    }
}

void DragTracker::reset()
{
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();
    m_lastCursorPos = QPointF();
    m_forceEndedWindow = nullptr;
}

} // namespace PlasmaZones
