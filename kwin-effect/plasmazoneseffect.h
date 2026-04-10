// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/globals.h> // For ElectricBorder enum
#include <scene/borderradius.h>
#include <QJsonArray>
#include <QObject>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QDBusPendingCall>
#include <QHash>
#include <QPointer>
#include <QRect>

#include <functional>

namespace KWin {
class OutlinedBorderItem;
class SurfaceItem;
}

namespace PlasmaZones {

// Forward declarations for helper classes
class AutotileHandler;
class NavigationHandler;
class ScreenChangeHandler;
class SnapAssistHandler;
class WindowAnimator;
class DragTracker;

/**
 * @brief Pre-parsed activation trigger (avoids QVariant unboxing in hot path)
 *
 * Each trigger has a modifier (enum value) and optional mouseButton bitmask.
 * Parsed once in loadCachedSettings() from the QVariantList received via D-Bus.
 */
struct ParsedTrigger
{
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
 * - Qt D-Bus API (QDBusMessage + async calls, no QDBusInterface)
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

    void prePaintScreen(KWin::ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    void prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    // paintScreen override removed — borders are now rendered natively by KWin's
    // scene graph (OutlinedBorderItem), no custom GL drawing needed.
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
    void slotSettingsChanged();

    // Keyboard Navigation handlers
    // Daemon-driven navigation: daemon computes geometry/target and emits these signals
    void slotApplyGeometryRequested(const QString& windowId, const QString& geometryJson, const QString& zoneId,
                                    const QString& screenId);
    void slotActivateWindowRequested(const QString& windowId);

    // Float toggle: daemon handles full flow via toggleFloatForWindow
    void slotToggleWindowFloatRequested(bool shouldFloat);

    // Daemon-driven batch operations (rotate, resnap)
    void slotApplyGeometriesBatch(const QString& batchJson, const QString& action);
    void slotRaiseWindowsRequested(const QStringList& windowIds);

    // Snap-all (effect collects candidates, daemon computes assignments)
    void slotSnapAllWindowsRequested(const QString& screenId);
    void slotPendingRestoresAvailable();
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    void slotRunningWindowsRequested();
    void slotRestoreSizeDuringDrag(const QString& windowId, int width, int height);
    void slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId,
                                               const QString& geometryJson);

    // Snap-mode minimize/unminimize float tracking
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);

    // Daemon lifecycle
    void slotDaemonReady();

private:
    // Window management
    void setupWindowConnections(KWin::EffectWindow* w);

    // Window identification
    QString getWindowId(KWin::EffectWindow* w) const;
    bool shouldHandleWindow(KWin::EffectWindow* w) const;
    bool isTileableWindow(KWin::EffectWindow* w) const;

    /**
     * @brief Reject Plasma shell layer-shell surfaces by window class.
     *
     * On Wayland, KDE notification popups, system tray overlays, the emoji
     * picker, the OSD, and krunner are layer-shell surfaces that don't
     * reliably set KWin's isNotification()/isPopupWindow() metadata, so they
     * slip past the type-based filters in shouldHandleWindow() and
     * notifyWindowActivated(). Class-based rejection is authoritative —
     * these are never zone-managed regardless of how KWin labels them, and
     * every stray activation/minimize event they generate caused the autotile
     * churn that balloons the master window to 100% on every notification
     * (discussion #271).
     */
    static bool isPlasmaShellSurface(const QString& windowClass);
    bool hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const;
    bool isWindowSticky(KWin::EffectWindow* w) const;
    void updateWindowStickyState(KWin::EffectWindow* w);

    // D-Bus communication

    /**
     * @brief Fire-and-forget async D-Bus call with error logging.
     *
     * Creates a QDBusMessage, sends it asynchronously, and attaches a
     * watcher that logs warnings on failure. No reply data is used.
     *
     * @param interface  D-Bus interface (e.g., DBus::Interface::Autotile)
     * @param method     D-Bus method name
     * @param args       Method arguments
     * @param logContext Human-readable label for the warning log
     */
    void fireAndForgetDBusCall(const QString& interface, const QString& method, const QVariantList& args,
                               const QString& logContext = {});

    void callDragStarted(const QString& windowId, const QRectF& geometry);
    void callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods, int mouseButtons);
    void callDragStopped(KWin::EffectWindow* window, const QString& windowId);
    void callCancelSnap();
    void callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete = nullptr);
    void connectNavigationSignals();
    void syncFloatingWindowsFromDaemon();

    /**
     * @brief Check if daemon is registered and ready for D-Bus calls
     * @param methodName Name of the calling method (for debug logging)
     * @return true if daemon is registered and ready
     */
    bool isDaemonReady(const char* methodName) const;

    /**
     * @brief Create an async D-Bus method call and return the pending result.
     *
     * Uses QDBusMessage::createMethodCall (no QDBusInterface) to avoid
     * synchronous D-Bus introspection that blocks the compositor thread.
     *
     * @param interface  D-Bus interface name
     * @param method     D-Bus method name
     * @param args       Method arguments
     * @return QDBusPendingCall for attaching a watcher
     */
    QDBusPendingCall asyncMethodCall(const QString& interface, const QString& method, const QVariantList& args = {});

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
     * @brief Ensure pre-snap geometry is stored for a window before snapping
     * @param w The effect window
     * @param windowId The window identifier
     * @note Checks if geometry exists, stores current geometry if not
     */
    void ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                     const QRectF& preCapturedGeometry = QRectF());

    /**
     * @brief Build a map of full window IDs to EffectWindow pointers
     *
     * Keys are full window IDs (appId|uuid) from getWindowId(),
     * so two windows of the same app get separate entries. Callers that
     * receive daemon data keyed by appId should do a linear scan fallback
     * when the exact full ID is not found.
     *
     * @param filterHandleable If true, only include windows passing shouldHandleWindow()
     * @return Hash map of fullWindowId -> EffectWindow*
     */
    QHash<QString, KWin::EffectWindow*> buildWindowMap(bool filterHandleable = true) const;

    /**
     * @brief Get the active window if valid, emit navigation feedback on failure
     * @param action The action name for feedback (e.g., "move", "swap")
     * @return Valid EffectWindow* or nullptr (feedback already emitted)
     */
    KWin::EffectWindow* getValidActiveWindowOrFail(const QString& action);

    /**
     * @brief Check if a window is floating (full windowId with appId fallback)
     * @param windowId The window identifier (full or appId-only)
     * @return true if window is floating
     */
    bool isWindowFloating(const QString& windowId) const;

    void notifyWindowClosed(KWin::EffectWindow* w);
    void notifyWindowActivated(KWin::EffectWindow* w);
    KWin::EffectWindow* findWindowById(const QString& windowId) const;

    /**
     * @brief All windows matching windowId (exact or same appId).
     * Used by autotile to disambiguate when multiple windows share an appId (e.g. two Firefox).
     */
    QVector<KWin::EffectWindow*> findAllWindowsById(const QString& windowId) const;

    // Navigation helpers
    KWin::EffectWindow* getActiveWindow() const;

    /**
     * @brief Build a stable EDID-based screen identifier from a KWin::Output.
     *
     * Mirrors the daemon's Utils::screenIdentifier() exactly: tries
     * QScreen::serialNumber(), normalizes hex, falls back to sysfs EDID
     * header serial. This ensures both sides produce identical screen IDs
     * regardless of which EDID field KWin's Output::serialNumber() returns.
     *
     * Format: "manufacturer:model:serial" — falls back to connector name
     * when EDID fields are empty.
     */
    QString outputScreenId(const KWin::LogicalOutput* output) const;
    QString getWindowScreenId(KWin::EffectWindow* w) const;

    /**
     * @brief Emit navigationFeedback D-Bus signal
     * @param success Whether the action succeeded
     * @param action The action type (e.g., "move", "focus", "push", "restore", "float")
     * @param reason Failure reason if !success (e.g., "no_window", "no_adjacent_zone")
     * @param sourceZoneId Optional source zone ID for OSD highlighting
     * @param targetZoneId Optional target zone ID for OSD highlighting
     * @param screenId Screen identifier where navigation occurred (for OSD placement)
     */
    void emitNavigationFeedback(bool success, const QString& action, const QString& reason = QString(),
                                const QString& sourceZoneId = QString(), const QString& targetZoneId = QString(),
                                const QString& screenId = QString());

    // Apply snap geometry to window.
    // When allowDuringDrag is true, applies immediately even if window is in user move state (snap-on-hover).
    // When false and the window is being dragged, defers via windowFinishUserMovedResized signal.
    void applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag = false,
                           bool skipAnimation = false);
    void repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo);

    // Async D-Bus helper for 5-arg snap replies (x, y, w, h, shouldSnap).
    // Uses QDBusMessage::createMethodCall (no QDBusInterface) to avoid synchronous introspection.
    // onSnapSuccess: optional callback when snap is applied, receives (windowId, screenId)
    void tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                          QPointer<KWin::EffectWindow> window, const QString& windowId, bool storePreSnap,
                          std::function<void()> fallback,
                          std::function<void(const QString&, const QString&)> onSnapSuccess = nullptr,
                          bool skipAnimation = false, std::function<void()> onComplete = nullptr);

    // Extract app identity from window ID (portion before the '|' separator)
    // Format: "appId|internalUuid" → returns "appId"
    static QString extractAppId(const QString& windowId);

    /**
     * @brief Derive short name from app ID for icon/app display
     * Reverse-DNS: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
     * Simple name: "firefox" → as-is
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
    // Animation sequence mode: 0=all at once, 1=one by one in zone order (for batch snaps)
    int cachedAnimationSequenceMode() const
    {
        return m_cachedAnimationSequenceMode;
    }
    int animationDurationMs() const
    {
        return m_cachedAnimationDuration;
    }
    int cachedAnimationStaggerInterval() const
    {
        return m_cachedAnimationStaggerInterval;
    }

    /**
     * @brief Apply a series of operations with optional stagger timing.
     *
     * When sequence mode is "one by one" and stagger interval > 0, each
     * applyFn(i) call is delayed by i * staggerInterval ms (cascading).
     * Otherwise all calls are immediate.
     *
     * @param count       Number of items to process.
     * @param applyFn     Called with index [0, count). Must capture by value
     *                    (lambda may fire asynchronously via QTimer).
     * @param onComplete  Optional callback after all items are processed.
     */
    void applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                   const std::function<void()>& onComplete = nullptr);

private:
    // Friend classes for helpers
    friend class AutotileHandler;
    friend class NavigationHandler;
    friend class ScreenChangeHandler;
    friend class SnapAssistHandler;
    friend class WindowAnimator;
    friend class DragTracker;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class instances
    // ═══════════════════════════════════════════════════════════════════════════════
    std::unique_ptr<AutotileHandler> m_autotileHandler;

    // Per-window native borders (scene graph items).
    // item is QPointer because OutlinedBorderItem is parented to WindowItem —
    // Qt parent-child ownership may destroy it before removeWindowBorder() runs.
    struct WindowBorder
    {
        QPointer<KWin::OutlinedBorderItem> item;
        QMetaObject::Connection geometryConnection;
        QPointer<KWin::SurfaceItem> clippedSurface;
        KWin::BorderRadius savedSurfaceRadius;
    };
    QHash<QString, WindowBorder> m_windowBorders; // windowId → border

    void updateWindowBorder(const QString& windowId, KWin::EffectWindow* w);
    void removeWindowBorder(const QString& windowId);
    void updateAllBorders();
    void clearAllBorders();

    std::unique_ptr<NavigationHandler> m_navigationHandler;
    std::unique_ptr<ScreenChangeHandler> m_screenChangeHandler;
    std::unique_ptr<SnapAssistHandler> m_snapAssistHandler;
    std::unique_ptr<WindowAnimator> m_windowAnimator;
    std::unique_ptr<DragTracker> m_dragTracker;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;
    Qt::MouseButtons m_currentMouseButtons = Qt::NoButton;
    bool m_keyboardGrabbed = false;

    // D-Bus communication uses QDBusMessage::createMethodCall exclusively
    // (no QDBusInterface) to avoid synchronous D-Bus introspection that blocks
    // the compositor thread. See asyncMethodCall() and fireAndForgetDBusCall().

    // Screen change debouncing and reapply handled by ScreenChangeHandler

    // Load cached settings from daemon (exclusions, activation triggers, etc.)
    void loadCachedSettings();

    /**
     * @brief Async helper for loading a single daemon setting.
     *
     * Sends getSetting(name) via raw QDBusMessage (no QDBusInterface), unwraps
     * the QDBusVariant, and calls onValue with the extracted QVariant.
     * Used by loadCachedSettings() to eliminate 13 identical watcher blocks.
     */
    template<typename Fn>
    void loadSettingAsync(const QString& name, Fn&& onValue);

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

    // User-configured exclusion lists — cached from daemon for shouldHandleWindow() gating.
    // The daemon also enforces these for keyboard navigation, but the effect needs them
    // for drag operations and window lifecycle reporting (slotWindowAdded, dragTracker).
    QStringList m_excludedApplications;
    QStringList m_excludedWindowClasses;

    // Minimum window size for autotile eligibility. Windows smaller than this
    // are rejected by isEligibleForAutotileNotify() to prevent small utility
    // windows (emoji picker, color picker, etc.) from entering the tiling tree.
    // Defaults match ConfigDefaults::minimumWindowWidth/Height() (200/150).
    // The async loadSettingAsync() call in loadCachedSettings() overrides
    // with the user's actual setting once daemon settings arrive via D-Bus.
    // Until then, these defaults keep the min-size filter active from
    // effect load — preventing small ephemeral windows (Steam splash,
    // Electron notification popups) from entering the autotile tree during
    // the startup race window.
    int m_cachedMinWindowWidth = 200;
    int m_cachedMinWindowHeight = 150;

    // Autotile: true when the current drag was started on an autotile screen
    // (callDragStarted was skipped). Captured at drag start so the drag end
    // handler uses the same decision, preventing a race where m_autotileScreens
    // changes mid-drag (e.g., async D-Bus signal) and leaves the popup visible.
    bool m_dragBypassedForAutotile = false;
    QString m_dragBypassScreenId; // Screen at drag start (for float D-Bus call on drag end)

    // Cached activation settings (loaded from daemon via D-Bus, updated on settingsChanged)
    // Used for local trigger checking to gate D-Bus calls (see anyLocalTriggerHeld)
    //
    // Defaults are PERMISSIVE (matching old always-send behavior) so that during the
    // startup window before async loads complete, no D-Bus calls are incorrectly skipped.
    // Once real settings arrive, they override these conservative defaults.
    QVariantList m_cachedDragActivationTriggers; // raw D-Bus data, kept for reload
    QVector<ParsedTrigger>
        m_parsedTriggers; // pre-parsed from QVariantList at load time (avoids QVariant unboxing in hot path)
    bool m_triggersLoaded =
        false; // false until D-Bus reply arrives — permissive default bypasses trigger gating (#175)
    bool m_cachedToggleActivation = false;
    bool m_cachedZoneSelectorEnabled = true; // true until proven false — ensures dragMoved passes through at startup
    int m_cachedAnimationSequenceMode = 0; // 0=all at once, 1=one by one in zone order
    int m_cachedAnimationDuration = 150; // ms, fallback until loaded from daemon
    int m_cachedAnimationStaggerInterval = 30; // ms between each window start when animating one by one (cascading)

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

    // Windows floated by drag on autotile screens. The daemon emits
    // applyGeometryRequested to restore pre-autotile geometry on float,
    // but drag-to-float should keep the window where the user dropped it.
    // Entries are consumed (removed) when slotApplyGeometryRequested skips
    // the geometry restore for a drag-floated window.
    QSet<QString> m_dragFloatedWindowIds;

    // Autotile: true when the current drag was started on an autotile screen

    /**
     * @brief Encode a QIcon as a data:image/png;base64 URL string.
     * @param icon The icon to encode
     * @param size The pixel size to render
     * @return Data URL string, or empty string on failure
     */
    static QString iconToDataUrl(const QIcon& icon, int size);

    // Snap-mode: windows floated due to minimize (mirrors autotile's m_minimizeFloatedWindows)
    QSet<QString> m_minimizeFloatedWindows;

    // Cached daemon D-Bus service registration state.
    // Updated via QDBusServiceWatcher signals (registration/unregistration) to avoid
    // synchronous isServiceRegistered() calls that block the compositor thread.
    bool m_daemonServiceRegistered = false;
    bool m_daemonReadyRestoresDone = false; ///< set after slotDaemonReady snap restores dispatched

    // Screen ID cache: connector name → EDID screen ID (manufacturer:model:serial).
    // Avoids repeated QScreen iteration and sysfs reads during drag (~30Hz).
    // Cleared on screen geometry changes (add/remove/reconfigure).
    mutable QHash<QString, QString> m_screenIdCache;

    // Cursor output tracking (for daemon shortcut screen detection on Wayland)
    // Stores the connector name of the last output the cursor was on.
    // Used for deduplication only — the actual D-Bus call sends the EDID screen ID.
    QString m_lastCursorOutput;
};

} // namespace PlasmaZones
