// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QPointF>
#include <QRectF>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Tracks window drag state and detects drag start/move/stop
 *
 * Responsible for:
 * - Tracking which window is being dragged
 * - Detecting drag start, movement, and stop
 * - Managing cursor position during drag
 *
 * It delegates the actual D-Bus calls and window filtering to the effect.
 */
class DragTracker : public QObject
{
    Q_OBJECT

public:
    explicit DragTracker(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // State queries
    bool isDragging() const { return m_draggedWindow != nullptr; }
    KWin::EffectWindow* draggedWindow() const { return m_draggedWindow; }
    QString draggedWindowId() const { return m_draggedWindowId; }
    QPointF lastCursorPos() const { return m_lastCursorPos; }

    // Called by effect's poll timer (start/end detection only)
    void pollWindowMoves();

    // Event-driven cursor position update during drag. Called from slotMouseChanged
    // instead of the poll timer, eliminating QTimer jitter from the compositor frame
    // path. Throttled to ~30Hz internally to avoid D-Bus flooding.
    void updateCursorPosition(const QPointF& cursorPos);

    // Force-end drag when a relevant mouse button is released.
    // Called from slotMouseChanged to end the drag immediately at button release,
    // rather than waiting for the poll timer. Fires on either LMB release or
    // activation-button release (e.g. RMB), whichever comes first. This is
    // essential because KWin keeps isUserMove() true until ALL buttons are released.
    void forceEnd(const QPointF& cursorPos);

    // Called when window is closed during drag
    void handleWindowClosed(KWin::EffectWindow* window);

    // Reset state (e.g., when effect is reconfigured)
    void reset();

Q_SIGNALS:
    void dragStarted(KWin::EffectWindow* window, const QString& windowId, const QRectF& geometry);
    void dragMoved(const QString& windowId, const QPointF& cursorPos);
    void dragStopped(KWin::EffectWindow* window, const QString& windowId, bool cancelled);

private:
    // Clear drag state and emit dragStopped (shared by pollWindowMoves and forceEnd)
    // cancelled = true when the drag ended externally (e.g. Escape cancelled interactive move)
    // cancelled = false when the drag ended normally (mouse button released)
    void finishDrag(bool cancelled);

    PlasmaZonesEffect* m_effect;
    KWin::EffectWindow* m_draggedWindow = nullptr;
    QString m_draggedWindowId;
    QPointF m_lastCursorPos;

    // After forceEnd(), suppress new drag detection for this window until isUserMove() clears
    KWin::EffectWindow* m_forceEndedWindow = nullptr;

    // Throttle event-driven dragMoved signals to ~30Hz (32ms intervals).
    // Without throttling, 1000Hz mouse input would flood D-Bus.
    QElapsedTimer m_dragMovedThrottle;
};

} // namespace PlasmaZones
