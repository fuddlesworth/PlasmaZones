// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragtracker.h"
#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

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
    m_velocitySampleIndex = 0;
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

QPointF DragTracker::releaseVelocity() const
{
    return m_releaseVelocity;
}

void DragTracker::updateCursorPosition(const QPointF& cursorPos)
{
    if (!m_draggedWindow) {
        return;
    }
    // Always track latest position for forceEnd()/callDragStopped() to use
    m_lastCursorPos = cursorPos;

    // Record every cursor update for velocity estimation (unthrottled)
    const qint64 now = m_dragMovedThrottle.elapsed();
    m_velocitySamples[m_velocitySampleIndex % VelocitySampleCount] = {cursorPos, now};
    m_velocitySampleIndex++;
    // Prevent integer overflow: at ~1000 Hz cursor updates, an int overflows INT_MAX
    // after ~34 minutes of dragging, causing negative modulo and OOB array writes.
    // Reset to VelocitySampleCount to preserve the "we have enough samples" semantic
    // for the qMin check in finishDrag().
    if (m_velocitySampleIndex >= VelocitySampleCount * 2)
        m_velocitySampleIndex = VelocitySampleCount;
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
    // Compute release velocity from the last two sufficiently-spaced samples
    m_releaseVelocity = QPointF(0, 0);
    if (m_velocitySampleIndex >= 2) {
        const int lastIdx = (m_velocitySampleIndex - 1) % VelocitySampleCount;
        // Search backwards for a sample at least 5ms older to avoid noise
        for (int offset = 1; offset < qMin(m_velocitySampleIndex, VelocitySampleCount); ++offset) {
            const int prevIdx = ((m_velocitySampleIndex - 1 - offset) % VelocitySampleCount + VelocitySampleCount)
                % VelocitySampleCount;
            const qint64 dtMs = m_velocitySamples[lastIdx].timestampMs - m_velocitySamples[prevIdx].timestampMs;
            if (dtMs >= 5) {
                const QPointF dp = m_velocitySamples[lastIdx].position - m_velocitySamples[prevIdx].position;
                m_releaseVelocity = dp / (dtMs / 1000.0); // pixels per second
                break;
            }
        }
    }

    // Copy raw pointer before clearing — QPointer auto-nulls when the window is destroyed
    auto* windowToSnap = m_draggedWindow.data();
    QString windowIdToSnap = m_draggedWindowId;

    // Clear state first to prevent re-entry issues
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();
    m_velocitySampleIndex = 0;

    Q_EMIT dragStopped(windowToSnap, windowIdToSnap, cancelled);
}

void DragTracker::handleWindowClosed(KWin::EffectWindow* window)
{
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

void DragTracker::reset()
{
    m_draggedWindow = nullptr;
    m_draggedWindowId.clear();
    m_lastCursorPos = QPointF();
    m_dragMovedThrottle.invalidate();
}

} // namespace PlasmaZones
