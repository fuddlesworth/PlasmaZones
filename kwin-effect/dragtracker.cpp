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
    // Fast path: no drag active and no LMB held → nothing to detect.
    // Avoids iterating the full stacking order on every idle poll tick,
    // which previously ran 60x/sec on the compositor thread even when
    // no window was being dragged (see discussion #167).
    if (!m_draggedWindow && !(m_effect->m_currentMouseButtons & Qt::LeftButton)) {
        m_forceEndedWindow = nullptr; // Safe to clear — no button means no isUserMove
        return;
    }

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
            finishDrag(/*cancelled=*/true);
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
        // isUserMove went false without forceEnd — the interactive move was cancelled
        // externally (e.g. Escape key, or compositor ended it). Treat as cancelled so
        // the effect calls cancelSnap() instead of dragStopped(), preventing an
        // unwanted snap to the zone the cursor was hovering over.
        qCInfo(lcEffect) << "Window move cancelled (isUserMove went false without button release)";
        finishDrag(/*cancelled=*/true);
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

    finishDrag(/*cancelled=*/false);
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
