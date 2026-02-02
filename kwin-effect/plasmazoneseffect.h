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
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSet>

namespace PlasmaZones {

// Forward declarations for helper classes
class NavigationHandler;
class WindowAnimator;
class DragTracker;

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

    // Animation interface for autotiling
    void prePaintWindow(KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                     KWin::EffectWindow* w, int mask, QRegion region,
                     KWin::WindowPaintData& data) override;
    void postPaintWindow(KWin::EffectWindow* w) override;

private Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow* w);
    void slotWindowClosed(KWin::EffectWindow* w);
    void slotWindowActivated(KWin::EffectWindow* w);
    void pollWindowMoves();
    void slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);
    void slotScreenGeometryChanged();
    void slotSettingsChanged();

    // Phase 2.3: Autotile geometry and focus handlers
    void slotAutotileWindowRequested(const QString& windowId, int x, int y, int width, int height);
    void slotAutotileFocusWindowRequested(const QString& windowId);

    // Phase 1 Keyboard Navigation handlers
    void slotMoveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry);
    void slotFocusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId);
    void slotRestoreWindowRequested();
    void slotToggleWindowFloatRequested(bool shouldFloat);
    void slotSwapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId,
                                  const QString& zoneGeometry);
    void slotRotateWindowsRequested(bool clockwise, const QString& rotationData);
    void slotCycleWindowsInZoneRequested(const QString& directive, const QString& unused);
    void slotPendingRestoresAvailable();

private:
    // Window management
    void setupWindowConnections(KWin::EffectWindow* w);

    // Window identification
    QString getWindowId(KWin::EffectWindow* w) const;
    bool shouldHandleWindow(KWin::EffectWindow* w) const;
    bool shouldAutoSnapWindow(KWin::EffectWindow* w) const;
    bool hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const;
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

    /**
     * @brief Ensure WindowTracking D-Bus interface is ready for use
     * @param methodName Name of the calling method (for debug logging)
     * @return true if interface is valid and ready, false otherwise
     * Consolidates interface validation pattern
     */
    bool ensureWindowTrackingReady(const char* methodName);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Parse JSON zone geometry string to QRect
     * @param json JSON string with x, y, width, height fields
     * @return Valid QRect on success, invalid QRect on parse error
     */
    QRect parseZoneGeometry(const QString& json) const;

    /**
     * @brief Query zone ID for a window from daemon
     * @param windowId The window identifier
     * @return Zone ID or empty string if not snapped/error
     */
    QString queryZoneForWindow(const QString& windowId);

    /**
     * @brief Ensure pre-snap geometry is stored for a window before snapping
     * @param w The effect window
     * @param windowId The window identifier
     * @note Checks if geometry exists, stores current geometry if not
     */
    void ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId);

    /**
     * @brief Build a map of stable window IDs to EffectWindow pointers
     * @param filterHandleable If true, only include windows passing shouldHandleWindow()
     * @return Hash map of stableId -> EffectWindow*
     */
    QHash<QString, KWin::EffectWindow*> buildWindowMap(bool filterHandleable = true) const;

    /**
     * @brief Get the active window if valid, emit navigation feedback on failure
     * @param action The action name for feedback (e.g., "move", "swap")
     * @return Valid EffectWindow* or nullptr (feedback already emitted)
     */
    KWin::EffectWindow* getValidActiveWindowOrFail(const QString& action);

    /**
     * @brief Check if a window is floating by its stable ID
     * @param stableId The stable window identifier (without pointer address)
     * @return true if window is floating
     */
    bool isWindowFloating(const QString& stableId) const;

    // Phase 2.1: Window event notifications for autotiling
    void notifyWindowAdded(KWin::EffectWindow* w);
    void notifyWindowClosed(KWin::EffectWindow* w);
    void notifyWindowActivated(KWin::EffectWindow* w);

    // Phase 2.3: Autotile geometry application
    void connectAutotileSignals();
    void loadAutotileSettings();
    KWin::EffectWindow* findWindowById(const QString& windowId) const;
    void applyAutotileGeometry(KWin::EffectWindow* window, const QRect& geometry, bool animate = true);

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
     * @param sourceZoneId Optional source zone ID for OSD highlighting
     * @param targetZoneId Optional target zone ID for OSD highlighting
     * @param screenName Screen name where navigation occurred (for OSD placement)
     */
    void emitNavigationFeedback(bool success, const QString& action, const QString& reason = QString(),
                                const QString& sourceZoneId = QString(), const QString& targetZoneId = QString(),
                                const QString& screenName = QString());

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

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class access methods
    // These methods are used by NavigationHandler, WindowAnimator, and DragTracker
    // ═══════════════════════════════════════════════════════════════════════════════
public:
    // D-Bus interface access for helpers
    QDBusInterface* windowTrackingInterface() const { return m_windowTrackingInterface.get(); }

    // Current keyboard modifiers (for drag tracking)
    Qt::KeyboardModifiers currentModifiers() const { return m_currentModifiers; }

private:
    // Friend classes for helpers
    friend class NavigationHandler;
    friend class WindowAnimator;
    friend class DragTracker;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class instances
    // ═══════════════════════════════════════════════════════════════════════════════
    std::unique_ptr<NavigationHandler> m_navigationHandler;
    std::unique_ptr<WindowAnimator> m_windowAnimator;
    std::unique_ptr<DragTracker> m_dragTracker;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;

    // D-Bus interfaces (lazy initialization)
    std::unique_ptr<QDBusInterface> m_dbusInterface; // WindowDrag interface
    std::unique_ptr<QDBusInterface> m_windowTrackingInterface; // WindowTracking interface
    std::unique_ptr<QDBusInterface> m_zoneDetectionInterface; // ZoneDetection interface

    // Phase 2.1: Track windows notified to daemon via windowAdded
    // Only send windowClosed for windows in this set (avoids D-Bus calls for untracked windows)
    QSet<QString> m_notifiedWindows;

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
