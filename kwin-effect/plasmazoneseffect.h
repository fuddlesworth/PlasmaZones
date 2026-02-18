// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/globals.h> // For ElectricBorder enum

#include <QJsonArray>
#include <QObject>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QDBusInterface>
#include <QHash>
#include <QPointer>
#include <QRect>

#include <functional>

namespace PlasmaZones {

// Forward declarations for helper classes
class NavigationHandler;
class WindowAnimator;
class DragTracker;

/**
 * @brief Pre-parsed activation trigger (avoids QVariant unboxing in hot path)
 *
 * Each trigger has a modifier (enum value) and optional mouseButton bitmask.
 * Parsed once in loadCachedSettings() from the QVariantList received via D-Bus.
 */
struct ParsedTrigger {
    int modifier = 0;
    int mouseButton = 0;
};

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

    void prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                     KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                     KWin::WindowPaintData& data) override;
    void grabbedKeyboardEvent(QKeyEvent* e) override;

private Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow* w);
    void slotWindowClosed(KWin::EffectWindow* w);
    void slotWindowActivated(KWin::EffectWindow* w);
    void slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);
    void slotScreenGeometryChanged();
    void slotSettingsChanged();

    // Keyboard Navigation handlers
    void slotMoveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry);
    void slotFocusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId);
    void slotRestoreWindowRequested();
    void slotToggleWindowFloatRequested(bool shouldFloat);
    void slotSwapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId,
                                  const QString& zoneGeometry);
    void slotRotateWindowsRequested(bool clockwise, const QString& rotationData);
    void slotResnapToNewLayoutRequested(const QString& resnapData);
    void slotSnapAllWindowsRequested(const QString& screenName);
    void slotCycleWindowsInZoneRequested(const QString& directive, const QString& unused);
    void slotPendingRestoresAvailable();
    void slotReapplyWindowGeometriesRequested();
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating);
    void slotRunningWindowsRequested();
    void slotRestoreSizeDuringDrag(const QString& windowId, int width, int height);
    void slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson);

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
    void callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods, int mouseButtons);
    void callDragStopped(KWin::EffectWindow* window, const QString& windowId);
    void callCancelSnap();
    void callSnapToLastZone(KWin::EffectWindow* window);
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
    bool ensureOverlayInterface(const char* methodName);

    /**
     * @brief Build candidate windows for Snap Assist (excluding just-snapped and already-snapped)
     * @param excludeWindowId Window ID to exclude (the one just snapped)
     * @param screenName Screen where snap occurred (filter by same screen)
     * @param snappedWindowIds Set of full window IDs already snapped to zones (excluded).
     *        Uses full ID matching with stable ID fallback for single-instance apps.
     * @return JSON array of {windowId, kwinHandle, icon, caption}
     */
    QJsonArray buildSnapAssistCandidates(const QString& excludeWindowId, const QString& screenName,
                                          const QSet<QString>& snappedWindowIds = QSet<QString>()) const;

    /**
     * @brief Async D-Bus chain: getSnappedWindows → buildCandidates → showSnapAssist
     *
     * Shared by callDragStopped and showSnapAssistContinuationIfNeeded.
     * All D-Bus calls are async to prevent compositor freeze (see discussion #158).
     *
     * @param excludeWindowId Window to exclude from candidates (empty for continuation)
     * @param screenName Target screen name
     * @param emptyZonesJson JSON string of empty zones from daemon
     */
    void asyncShowSnapAssist(const QString& excludeWindowId, const QString& screenName,
                             const QString& emptyZonesJson);

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
     * @brief Async query zone ID for a window from daemon
     * @param windowId The window identifier
     * @param callback Called with zone ID or empty string if not snapped/error
     */
    void queryZoneForWindowAsync(const QString& windowId, std::function<void(const QString&)> callback);

    /**
     * @brief Ensure pre-snap geometry is stored for a window before snapping
     * @param w The effect window
     * @param windowId The window identifier
     * @note Checks if geometry exists, stores current geometry if not
     */
    void ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId, const QRectF& preCapturedGeometry = QRectF());

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
     * @brief Check if a window is floating (full windowId with stableId fallback)
     * @param windowId The window identifier (full or stable)
     * @return true if window is floating
     */
    bool isWindowFloating(const QString& windowId) const;

    void notifyWindowClosed(KWin::EffectWindow* w);
    void notifyWindowActivated(KWin::EffectWindow* w);

    // Navigation helpers
    KWin::EffectWindow* getActiveWindow() const;
    void queryAdjacentZoneAsync(const QString& currentZoneId, const QString& direction, std::function<void(const QString&)> callback);
    void queryFirstZoneInDirectionAsync(const QString& direction, const QString& screenName, std::function<void(const QString&)> callback);
    void queryZoneGeometryForScreenAsync(const QString& zoneId, const QString& screenName, std::function<void(const QString&)> callback);
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
    // When allowDuringDrag is true, applies immediately even if window is in user move state (for FancyZones-style)
    // retriesLeft caps the deferred-retry chain (avoids unbounded timers if isUserMove gets stuck)
    void applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag = false, int retriesLeft = 20);

    // Async D-Bus helper for 5-arg snap replies (x, y, w, h, shouldSnap).
    // iface must remain valid for the duration of the async call (caller guarantees
    // ownership via unique_ptr member; the reference is only used to initiate the call,
    // not captured in the async lambda).
    // onSnapSuccess: optional callback when snap is applied, receives (windowId, screenName)
    void tryAsyncSnapCall(QDBusAbstractInterface& iface, const QString& method, const QList<QVariant>& args,
                          QPointer<KWin::EffectWindow> window, const QString& windowId,
                          bool storePreSnap, std::function<void()> fallback,
                          std::function<void(const QString&, const QString&)> onSnapSuccess = nullptr);

    // Shared async dispatch: watch a QDBusPendingCall returning QString and invoke callback
    void dispatchAsyncStringReply(QDBusPendingCall call, std::function<void(const QString&)> callback);

    /**
     * If there are empty zones and unsnapped candidates, show Snap Assist.
     * Used to continue Snap Assist after each snap until all zones filled or no candidates.
     */
    void showSnapAssistContinuationIfNeeded(const QString& screenName);

    // Extract stable ID from full window ID (strips pointer address)
    // Stable ID = windowClass:resourceName (without pointer address)
    // This allows matching windows across KWin restarts
    static QString extractStableId(const QString& windowId);

    /**
     * @brief Derive short name from window class for icon/app display
     * X11: "resourceName resourceClass" → first part (e.g., "dolphin")
     * Wayland app_id: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
     */
    static QString deriveShortNameFromWindowClass(const QString& windowClass);

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
    Qt::MouseButtons m_currentMouseButtons = Qt::NoButton;
    bool m_keyboardGrabbed = false;

    // D-Bus interfaces (lazy initialization)
    // Note: WindowDrag interface uses QDBusMessage::createMethodCall directly
    // (no QDBusInterface) to avoid synchronous D-Bus introspection that could
    // block the compositor thread during startup. See callDragMoved() etc.
    std::unique_ptr<QDBusInterface> m_windowTrackingInterface; // WindowTracking interface
    std::unique_ptr<QDBusInterface> m_zoneDetectionInterface; // ZoneDetection interface
    std::unique_ptr<QDBusInterface> m_overlayInterface; // Overlay interface (Snap Assist)
    std::unique_ptr<QDBusInterface> m_settingsInterface; // Settings interface

    // Screen geometry change debouncing
    // The virtualScreenGeometryChanged signal can fire rapidly (monitor connect/disconnect,
    // arrangement changes, etc.) which causes windows to be unnecessarily resnapped.
    // We debounce to only apply changes after 500ms of no further signals.
    QTimer m_screenChangeDebounce;
    bool m_pendingScreenChange = false;
    QRect m_lastVirtualScreenGeometry;

    // Apply debounced screen geometry change
    void applyScreenGeometryChange();
    // Fetch getUpdatedWindowGeometries from daemon and apply (used by resolution change and reapply request)
    void fetchAndApplyWindowGeometries();
    void applyWindowGeometriesFromJson(const QString& geometriesJson);

    // Reapply guard: avoid overlapping async reapply runs; pending request runs once after current finishes
    bool m_reapplyInProgress = false;
    bool m_reapplyPending = false;

    // Load cached settings from daemon (exclusions, activation triggers, etc.)
    void loadCachedSettings();

    /**
     * @brief Check if any activation trigger is currently held locally
     *
     * Replicates the daemon's anyTriggerHeld() logic using cached trigger
     * settings and current modifier/button state from slotMouseChanged().
     * Used to gate D-Bus dragMoved calls — if no trigger is held, no toggle
     * mode, and zone selector disabled, we skip the D-Bus call entirely.
     * This eliminates 60Hz D-Bus traffic during non-zone window drags.
     */
    bool anyLocalTriggerHeld() const;

    /**
     * @brief Map DragModifier enum value to Qt modifier flags
     *
     * Must stay in sync with WindowDragAdaptor::checkModifier() in the daemon.
     * The enum values are defined in src/core/interfaces.h (DragModifier).
     */
    static bool checkLocalModifier(int modifierSetting, Qt::KeyboardModifiers mods);

    /**
     * @brief Detect activation trigger and grab keyboard if needed
     *
     * Sets m_dragActivationDetected and grabs keyboard when an activation
     * trigger is first detected during a drag. Returns true if activation
     * was detected (either previously or just now).
     */
    bool detectActivationAndGrab();

    /**
     * @brief Send the deferred dragStarted D-Bus call to the daemon
     *
     * Called lazily on first activation detection or zone selector need.
     * Uses stored pending drag info from the DragTracker::dragStarted signal.
     * No-op if already sent for the current drag.
     */
    void sendDeferredDragStarted();

    // Cached exclusion settings (loaded from daemon via D-Bus)
    bool m_excludeTransientWindows = true;
    int m_minimumWindowWidth = 200;
    int m_minimumWindowHeight = 150;
    bool m_snapAssistEnabled = false; // false until loaded from D-Bus (avoids race at startup)

    // Cached activation settings (loaded from daemon via D-Bus, updated on settingsChanged)
    // Used for local trigger checking to gate D-Bus calls (see anyLocalTriggerHeld)
    //
    // Defaults are PERMISSIVE (matching old always-send behavior) so that during the
    // startup window before async loads complete, no D-Bus calls are incorrectly skipped.
    // Once real settings arrive, they override these conservative defaults.
    QVariantList m_cachedDragActivationTriggers; // raw D-Bus data, kept for reload
    QVector<ParsedTrigger> m_parsedTriggers; // pre-parsed from QVariantList at load time (avoids QVariant unboxing in hot path)
    bool m_cachedToggleActivation = false;
    bool m_cachedZoneSelectorEnabled = true; // true until proven false — ensures dragMoved passes through at startup

    // Per-drag activation tracking: set once any activation trigger is detected
    // during the current drag. Stays true for the remainder of the drag so
    // the daemon receives all subsequent cursor updates (needed for hold/release
    // cycles and overlay hide/show).
    bool m_dragActivationDetected = false;

    // Deferred dragStarted: D-Bus call is only sent when zones are actually needed.
    // Pending info is stored from DragTracker::dragStarted signal and sent on first
    // activation or zone selector trigger. This avoids KGlobalAccel register/unregister
    // overhead on every non-zone window drag.
    bool m_dragStartedSent = false;
    QString m_pendingDragWindowId;
    QRectF m_pendingDragGeometry;

    // Cursor screen tracking (for daemon shortcut screen detection on Wayland)
    // Updated in slotMouseChanged() whenever the cursor crosses to a different monitor.
    // Reported to daemon via cursorScreenChanged D-Bus call.
    QString m_lastCursorScreenName;
};

} // namespace PlasmaZones
