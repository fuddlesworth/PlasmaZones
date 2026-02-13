// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QAction>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QRect>
#include <QUuid>
#include <QSet>
#include <QVector>
#include <memory>

class QScreen;

namespace PlasmaZones {

class IOverlayService;
class IZoneDetector;
class LayoutManager; // Concrete type needed for signal connections
class ISettings;
class Layout;
class Zone;
class WindowTrackingAdaptor;

/**
 * @brief D-Bus adaptor for window drag handling
 *
 * Provides D-Bus interface: org.plasmazones.WindowDrag
 *
 * Receives drag events from KWin script and handles:
 * - Modifier key detection (works on Wayland via QGuiApplication)
 * - Zone detection and highlighting
 * - Overlay visibility based on modifiers
 * - Window snapping via KWin D-Bus
 */
class PLASMAZONES_EXPORT WindowDragAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowDrag")

public:
    explicit WindowDragAdaptor(IOverlayService* overlay, IZoneDetector* detector, LayoutManager* layoutManager,
                               ISettings* settings, WindowTrackingAdaptor* windowTracking, QObject* parent = nullptr);
    ~WindowDragAdaptor() override = default;

public Q_SLOTS:
    /**
     * Called when window drag starts
     * @param windowId Unique window identifier
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @param appName Application name (for exclusion filtering)
     * @param windowClass Window class (for exclusion filtering)
     * @param mouseButtons Qt::MouseButtons flags for the button(s) that started the drag (for activation-by-mouse)
     * @note Parameters are double because KWin QML DBusCall sends JS numbers as D-Bus doubles
     */
    void dragStarted(const QString& windowId, double x, double y, double width, double height,
                     const QString& appName, const QString& windowClass, int mouseButtons);

    /**
     * Called while window is being dragged (cursor moved)
     * @param windowId Unique window identifier
     * @param cursorX Cursor X position (int32 - matches KWin's QPoint)
     * @param cursorY Cursor Y position (int32 - matches KWin's QPoint)
     * @param modifiers Qt keyboard modifiers from KWin (int32 - Qt::KeyboardModifiers flags)
     * @param mouseButtons Qt::MouseButtons currently held (int32). Enables activation-by-mouse: hold this button during drag to show overlay (same as modifier).
     */
    void dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons);

    /**
     * Called when window drag ends
     * @param windowId Unique window identifier
     * @param cursorX Cursor X at release (global; used for release screen detection)
     * @param cursorY Cursor Y at release (global)
     * @param snapX Output: X position for window
     * @param snapY Output: Y position for window
     * @param snapWidth Output: Width for window
     * @param snapHeight Output: Height for window
     * @param shouldApplyGeometry Output: True if KWin should apply the geometry
     * @param releaseScreenName Output: Screen name where the drag was released, for auto-fill on drop
     * @param restoreSizeOnly Output: If true with shouldApplyGeometry, effect applies only width/height at current position (drag-to-unsnap)
     */
    void dragStopped(const QString& windowId, int cursorX, int cursorY, int& snapX, int& snapY, int& snapWidth,
                     int& snapHeight, bool& shouldApplyGeometry, QString& releaseScreenName, bool& restoreSizeOnly,
                     bool& snapAssistRequested, QString& emptyZonesJson);

    /**
     * Cancel current snap operation (Escape key)
     */
    void cancelSnap();

    /**
     * Called when a window is closed during or after a drag operation
     * @param windowId Window ID that was closed
     * @note Cleans up any drag state associated with this window
     */
    void handleWindowClosed(const QString& windowId);

Q_SIGNALS:
    /**
     * Emitted when the zone geometry under the cursor changes during drag.
     * KWin effect subscribes and applies the geometry immediately for FancyZones-style snap-on-hover.
     */
    void zoneGeometryDuringDragChanged(const QString& windowId, int x, int y, int width, int height);

    /**
     * Emitted when the cursor leaves all zones during drag and the window was snapped.
     * KWin effect applies pre-snap size immediately (restore-size-only at current position).
     */
    void restoreSizeDuringDragChanged(const QString& windowId, int width, int height);

private:
    // Tolerance constants for geometry matching (fallback detection)
    // Position tolerance is generous due to KWin window decoration/shadow offsets
    static constexpr int PositionTolerance = 100;
    // Size tolerance is stricter - snapped windows should match zone size closely
    static constexpr int SizeTolerance = 20;

    // Check if modifier matches setting
    bool checkModifier(int modifierSetting, Qt::KeyboardModifiers mods) const;

    // Helper: Find screen containing a point (returns primary screen if not found)
    QScreen* screenAtPoint(int x, int y) const;

    // Shared preamble for drag handler methods (DRY extraction)
    // Returns layout for the screen at (x,y), or nullptr if screen disabled/no layout.
    // Shows overlay if not visible. Sets outScreen to the resolved screen.
    Layout* prepareHandlerContext(int x, int y, QScreen*& outScreen);

    // Compute bounding rectangle of multiple zones with gaps applied
    QRectF computeCombinedZoneGeometry(const QVector<Zone*>& zones, QScreen* screen, Layout* layout) const;

    // Convert zone UUIDs to string list (for overlay service)
    static QStringList zoneIdsToStringList(const QVector<QUuid>& ids);

    // Refactored dragMoved helpers
    void handleZoneSpanModifier(int x, int y);
    void handleMultiZoneModifier(int x, int y, Qt::KeyboardModifiers mods);
    void handleSingleZoneModifier(int x, int y);
    void hideOverlayAndClearZoneState();

    IOverlayService* m_overlayService;
    IZoneDetector* m_zoneDetector;
    LayoutManager* m_layoutManager; // Concrete type for signal connections
    ISettings* m_settings;
    WindowTrackingAdaptor* m_windowTracking;

    // Current drag state
    QString m_draggedWindowId;
    QRect m_originalGeometry;
    QString m_currentZoneId;
    QRect m_currentZoneGeometry;
    bool m_snapCancelled = false;
    bool m_mouseActivationLatched = false; // Latches mouse-button activation until drag ends
    bool m_overlayShown = false;
    QScreen* m_overlayScreen = nullptr; // Screen overlay is shown on (single-monitor mode only)
    bool m_zoneSelectorShown = false;
    int m_lastCursorX = 0;
    int m_lastCursorY = 0;
    bool m_wasSnapped = false; // True if window was snapped to a zone when drag started

    // Multi-zone state
    QVector<QUuid> m_currentAdjacentZoneIds; // Zone IDs (not pointers - zones owned by Layout)
    bool m_isMultiZoneMode = false;
    QRect m_currentMultiZoneGeometry; // Combined geometry for multi-zone

    // Paint-to-span state (zone span modifier)
    QSet<QUuid> m_paintedZoneIds; // Accumulates zones during paint-to-span drag
    bool m_modifierConflictWarned = false; // Logged once per drag, reset on next dragStarted

    // Escape shortcut to cancel overlay during drag (registered on drag start, unregistered on drag end)
    QAction* m_cancelOverlayAction = nullptr;

    // Last emitted zone geometry (emit only when changed, per .cursorrules)
    QRect m_lastEmittedZoneGeometry;
    bool m_restoreSizeEmittedDuringDrag = false;

    void registerCancelOverlayShortcut();
    void unregisterCancelOverlayShortcut();

    // Zone selector methods
    void checkZoneSelectorTrigger(int cursorX, int cursorY);
    bool isNearTriggerEdge(QScreen* screen, int cursorX, int cursorY) const;

    // dragStopped() helpers
    void hideOverlayAndSelector();
    void resetDragState(bool keepEscapeShortcut = false);

    // Pre-snap geometry helper (reduces code duplication)
    // Overload with captured values to prevent race conditions in dragStopped()
    void tryStorePreSnapGeometry(const QString& windowId);
    void tryStorePreSnapGeometry(const QString& windowId, bool wasSnapped, const QRect& originalGeometry);

private Q_SLOTS:
    /**
     * Called when the active layout changes mid-drag
     * Clears cached zone state to prevent stale geometry being used on snap
     */
    void onLayoutChanged();

    /**
     * Called when snap assist is dismissed (selection, timeout, click-away, etc.)
     * Unregisters the KGlobalAccel Escape shortcut that was kept alive for snap assist
     */
    void onSnapAssistDismissed();
};

} // namespace PlasmaZones
