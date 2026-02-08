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

    // Detect start of drag
    if (movingWindow && !m_draggedWindow) {
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
            qCDebug(lcEffect) << "Window went fullscreen mid-drag, stopping tracking";
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
        qCInfo(lcEffect) << "Window move finished";
        // Save the window pointer before clearing
        KWin::EffectWindow* windowToSnap = m_draggedWindow;
        QString windowIdToSnap = m_draggedWindowId;

        // Clear state first to prevent re-entry issues
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();

        Q_EMIT dragStopped(windowToSnap, windowIdToSnap);
    }
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
}

} // namespace PlasmaZones
