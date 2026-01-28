// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/globals.h> // For ElectricBorder enum

#include <QObject>
#include <QTimer>
#include <QDBusInterface>
#include <QPoint>
#include <QRect>
#include <QSet>

namespace PlasmaZones {

/**
 * @brief KWin C++ Effect for PlasmaZones
 *
 * This effect detects window drag operations and keyboard modifiers,
 * then communicates with the PlasmaZones daemon via D-Bus.
 *
 * Unlike JavaScript effects, C++ effects have full access to:
 * - Qt D-Bus API (QDBusInterface)
 * - Keyboard modifier state via QGuiApplication
 * - Window move/resize state via isUserMove()
 */
class PlasmaZonesEffect : public KWin::Effect
{
    Q_OBJECT

public:
    PlasmaZonesEffect();
    ~PlasmaZonesEffect() override;

    // Effect metadata
    static bool supported();
    static bool enabledByDefault();

    // Effect interface
    void reconfigure(ReconfigureFlags flags) override;
    bool isActive() const override;

private Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow* w);
    void slotWindowClosed(KWin::EffectWindow* w);
    void pollWindowMoves();
    void slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);
    void slotScreenGeometryChanged();
    void slotSettingsChanged();

    // Phase 1 Keyboard Navigation handlers
    void slotMoveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry);
    void slotFocusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId);
    void slotRestoreWindowRequested();
    void slotToggleWindowFloatRequested(bool shouldFloat);
    void slotSwapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId,
                                  const QString& zoneGeometry);
    void slotRotateWindowsRequested(bool clockwise, const QString& rotationData);

private:
    // Window management
    void setupWindowConnections(KWin::EffectWindow* w);

    // Window identification
    QString getWindowId(KWin::EffectWindow* w) const;
    bool shouldHandleWindow(KWin::EffectWindow* w) const;
    bool shouldAutoSnapWindow(KWin::EffectWindow* w) const;
    bool isWindowSticky(KWin::EffectWindow* w) const;
    void updateWindowStickyState(KWin::EffectWindow* w);

    // D-Bus communication
    void callDragStarted(const QString& windowId, const QRectF& geometry);
    void callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods);
    void callDragStopped(KWin::EffectWindow* window, const QString& windowId);
    void callSnapToLastZone(KWin::EffectWindow* window);
    void ensureDBusInterface();
    void ensureWindowTrackingInterface();
    void ensureZoneDetectionInterface();
    void connectNavigationSignals();
    void syncFloatingWindowsFromDaemon();

    // Navigation helpers
    KWin::EffectWindow* getActiveWindow() const;
    QString queryAdjacentZone(const QString& currentZoneId, const QString& direction);
    QString queryFirstZoneInDirection(const QString& direction);
    QString queryZoneGeometry(const QString& zoneId);
    QString queryZoneGeometryForScreen(const QString& zoneId, const QString& screenName);
    QString getWindowScreenName(KWin::EffectWindow* w) const;

    /**
     * @brief Emit navigationFeedback D-Bus signal
     * @param success Whether the action succeeded
     * @param action The action type (e.g., "move", "focus", "push", "restore", "float")
     * @param reason Failure reason if !success (e.g., "no_window", "no_adjacent_zone")
     */
    void emitNavigationFeedback(bool success, const QString& action, const QString& reason = QString());

    // Apply snap geometry to window
    void applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry);

    // Extract stable ID from full window ID (strips pointer address)
    // Stable ID = windowClass:resourceName (without pointer address)
    // This allows matching windows across KWin restarts
    static QString extractStableId(const QString& windowId);

    // reserveScreenEdges() and unreserveScreenEdges() have been removed. The daemon
    // disables KWin Quick Tile via kwriteconfig6. Reserving edges would turn on the
    // electric edge effect, which we don't want.

public Q_SLOTS:
    // Handle electric border activation - return true to consume the event
    // and prevent KWin Quick Tile from triggering
    bool borderActivated(KWin::ElectricBorder border) override;

private:
    // State
    KWin::EffectWindow* m_draggedWindow = nullptr;
    QString m_draggedWindowId;
    QPointF m_lastCursorPos;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;

    // D-Bus interfaces (lazy initialization)
    std::unique_ptr<QDBusInterface> m_dbusInterface; // WindowDrag interface
    std::unique_ptr<QDBusInterface> m_windowTrackingInterface; // WindowTracking interface
    std::unique_ptr<QDBusInterface> m_zoneDetectionInterface; // ZoneDetection interface

    // Float tracking (Phase 1 keyboard navigation)
    QSet<QString> m_floatingWindows;

    // Polling timer for detecting window moves
    QTimer m_pollTimer;

    // Screen geometry change debouncing
    // The virtualScreenGeometryChanged signal can fire rapidly (monitor connect/disconnect,
    // arrangement changes, etc.) which causes windows to be unnecessarily resnapped.
    // We debounce to only apply changes after 500ms of no further signals.
    QTimer m_screenChangeDebounce;
    bool m_pendingScreenChange = false;
    QRect m_lastVirtualScreenGeometry;

    // Apply debounced screen geometry change
    void applyScreenGeometryChange();

    // Load exclusion settings from daemon
    void loadExclusionSettings();

    // Cached exclusion settings (loaded from daemon via D-Bus)
    bool m_excludeTransientWindows = true;
    int m_minimumWindowWidth = 200;
    int m_minimumWindowHeight = 150;
};

} // namespace PlasmaZones
