// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

    // Called by effect's poll timer
    void pollWindowMoves();

    // Called when window is closed during drag
    void handleWindowClosed(KWin::EffectWindow* window);

    // Reset state (e.g., when effect is reconfigured)
    void reset();

Q_SIGNALS:
    void dragStarted(KWin::EffectWindow* window, const QString& windowId, const QRectF& geometry);
    void dragMoved(const QString& windowId, const QPointF& cursorPos);
    void dragStopped(KWin::EffectWindow* window, const QString& windowId);

private:
    PlasmaZonesEffect* m_effect;
    KWin::EffectWindow* m_draggedWindow = nullptr;
    QString m_draggedWindowId;
    QPointF m_lastCursorPos;
};

} // namespace PlasmaZones
