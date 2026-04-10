// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <dbus_types.h>
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

class IShortcutBackend;
class IOverlayService;
class IZoneDetector;
class LayoutManager; // Concrete type needed for signal connections
class ISettings;
class Layout;
class Zone;
class WindowTrackingAdaptor;
class AutotileEngine;

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

    /**
     * @brief Set the autotile engine for per-screen autotile checks
     *
     * When set, dragStopped() rejects snaps on autotile screens and
     * prepareHandlerContext() skips overlay display on them.
     * Pass nullptr during shutdown to prevent dangling pointer access.
     */
    void setAutotileEngine(AutotileEngine* engine)
    {
        m_autotileEngine = engine;
    }

    /**
     * @brief Set the shortcut backend for registering/unregistering shortcuts
     *
     * Must be called after construction, before any drag operations.
     * The backend is owned by ShortcutManager — this is a non-owning pointer.
     */
    void setShortcutBackend(IShortcutBackend* backend)
    {
        m_shortcutBackend = backend;
    }

public Q_SLOTS:
    /**
     * Called when window drag starts
     * @param windowId Unique window identifier
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @param mouseButtons Qt::MouseButtons flags for the button(s) that started the drag (for activation-by-mouse)
     * @note Parameters are double because KWin QML DBusCall sends JS numbers as D-Bus doubles
     */
    void dragStarted(const QString& windowId, double x, double y, double width, double height, int mouseButtons);

    /**
     * Called while window is being dragged (cursor moved)
     * @param windowId Unique window identifier
     * @param cursorX Cursor X position (int32 - matches KWin's QPoint)
     * @param cursorY Cursor Y position (int32 - matches KWin's QPoint)
     * @param modifiers Qt keyboard modifiers from KWin (int32 - Qt::KeyboardModifiers flags)
     * @param mouseButtons Qt::MouseButtons currently held (int32). Enables activation-by-mouse: hold this button during
     * drag to show overlay (same as modifier).
     */
    void dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons);

    /** Forward mouse wheel delta to zone selector for scrolling during drag. */
    void selectorScrollWheel(int angleDeltaY);

    /**
     * Called when window drag ends
     * @param windowId Unique window identifier
     * @param cursorX Cursor X at release (global; used for release screen detection)
     * @param cursorY Cursor Y at release (global)
     * @param modifiers Qt::KeyboardModifiers at release.
     * @param mouseButtons Qt::MouseButtons at release. With modifiers, used for SnapAssistTriggers.
     * @param snapX Output: X position for window
     * @param snapY Output: Y position for window
     * @param snapWidth Output: Width for window
     * @param snapHeight Output: Height for window
     * @param shouldApplyGeometry Output: True if KWin should apply the geometry
     * @param releaseScreenIdOut Output: Screen ID where the drag was released, for auto-fill on drop
     * @param restoreSizeOnly Output: If true with shouldApplyGeometry, effect applies only width/height at current
     * position (drag-to-unsnap)
     */
    void dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons, int& snapX,
                     int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                     QString& releaseScreenIdOut, bool& restoreSizeOnly, bool& snapAssistRequested,
                     PlasmaZones::EmptyZoneList& emptyZonesOut);

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
     * KWin effect subscribes and applies the geometry immediately for snap-on-hover behavior.
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

    /// Pre-parsed trigger (avoids QVariantMap unboxing on every dragMoved tick)
    struct ParsedTrigger
    {
        int modifier = 0;
        int mouseButton = 0;
    };

    // Check if modifier matches setting
    bool checkModifier(int modifierSetting, Qt::KeyboardModifiers mods) const;
    // Check if any trigger in a list matches current modifiers/mouse buttons
    bool anyTriggerHeld(const QVariantList& triggers, Qt::KeyboardModifiers mods, int mouseButtons) const;
    // Overload using pre-parsed triggers (hot path during drag)
    bool anyTriggerHeld(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods, int mouseButtons) const;
    // Parse QVariantList triggers into POD structs for repeated use
    static QVector<ParsedTrigger> parseTriggers(const QVariantList& triggers);

    // Helper: Find screen containing a point (returns primary screen if not found)
    QScreen* screenAtPoint(int x, int y) const;

    // Helper: Returns the effective (virtual-aware) screen ID for a cursor position.
    // Prefers virtual screen resolution via ScreenManager, falls back to physical screen.
    QString effectiveScreenIdAt(int x, int y) const;

    // Shared preamble for drag handler methods (DRY extraction)
    // Returns layout for the screen at (x,y), or nullptr if screen disabled/no layout.
    // Shows overlay if not visible. Sets outScreen to the resolved physical screen
    // and outScreenId to the virtual-aware screen identifier.
    Layout* prepareHandlerContext(int x, int y, QScreen*& outScreen, QString& outScreenId);

    // Compute bounding rectangle of multiple zones with gaps applied
    // screenId is the virtual-aware screen identifier for gap/padding lookups.
    QRectF computeCombinedZoneGeometry(const QVector<Zone*>& zones, QScreen* screen, Layout* layout,
                                       const QString& screenId) const;

    // Convert zone UUIDs to string list (for overlay service)
    static QStringList zoneIdsToStringList(const QVector<QUuid>& ids);

    // Refactored dragMoved helpers
    void handleZoneSpanModifier(int x, int y);
    void handleMultiZoneModifier(int x, int y);
    void hideOverlayAndClearZoneState();

    IOverlayService* m_overlayService;
    IZoneDetector* m_zoneDetector;
    LayoutManager* m_layoutManager; // Concrete type for signal connections
    ISettings* m_settings;
    WindowTrackingAdaptor* m_windowTracking;
    AutotileEngine* m_autotileEngine = nullptr; // Optional: per-screen autotile check
    IShortcutBackend* m_shortcutBackend = nullptr; // Non-owning: owned by ShortcutManager

    // Current drag state
    QString m_draggedWindowId;
    QRect m_originalGeometry;
    QString m_currentZoneId;
    QRect m_currentZoneGeometry;
    bool m_snapCancelled = false;
    bool m_triggerReleasedAfterCancel = false; // Tracks release→press cycle for retrigger after Escape
    bool m_activationToggled = false; // Current toggle state (on/off)
    bool m_prevTriggerHeld = false; // Previous frame's trigger state for edge detection
    bool m_overlayShown = false;
    QScreen* m_overlayScreen = nullptr; // Screen overlay is shown on (single-monitor mode only)
    QString m_overlayScreenId; // Virtual-aware screen ID for overlay tracking
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

    // Pre-parsed trigger caches (populated on dragStarted, used on every dragMoved tick)
    QVector<ParsedTrigger> m_cachedActivationTriggers;
    QVector<ParsedTrigger> m_cachedZoneSpanTriggers;

    // Last emitted zone geometry (emit only when changed)
    QRect m_lastEmittedZoneGeometry;
    bool m_restoreSizeEmittedDuringDrag = false;

    void registerCancelOverlayShortcut();
    void unregisterCancelOverlayShortcut();

    // Zone selector methods
    void checkZoneSelectorTrigger(int cursorX, int cursorY);
    bool isNearTriggerEdge(QScreen* screen, int cursorX, int cursorY, const QString& screenId = QString()) const;

    // Screen resolution helper (DRY: used by prepareHandlerContext, dragStopped, checkZoneSelectorTrigger)
    struct ScreenResolution
    {
        QString screenId; // effective (possibly virtual) screen ID
        QString physicalId; // physical screen ID
        QScreen* qscreen; // physical QScreen pointer
    };
    ScreenResolution resolveScreenAt(const QPointF& globalPos) const;

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
     * Unregisters the Escape shortcut that was kept alive for snap assist
     */
    void onSnapAssistDismissed();
};

} // namespace PlasmaZones
