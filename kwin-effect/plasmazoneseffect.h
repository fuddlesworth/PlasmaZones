// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <compositor_bridge.h>
#include <dbus_types.h>
#include <trigger_parser.h>

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

#include "shared/virtualscreenid.h"

namespace KWin {
class OutlinedBorderItem;
class SurfaceItem;
}

namespace PlasmaZones {

// Mirror of core/enums.h AutotileDragBehavior. The effect can't include daemon
// headers (KWin plugin ABI constraints), so the values are duplicated here.
// MUST stay in sync with core/enums.h — bump both in the same commit.
enum class EffectAutotileDragBehavior : int {
    Float = 0, ///< Drag-to-float (PlasmaZones default)
    Reorder = 1, ///< Drag-to-reorder (Krohnkite-style)
};

// Forward declarations for helper classes
class AutotileHandler;
class KWinCompositorBridge;
class NavigationHandler;
class ScreenChangeHandler;
class SnapAssistHandler;
class WindowAnimator;
class DragTracker;

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
    void slotApplyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                    const QString& screenId, bool sizeOnly);
    void slotActivateWindowRequested(const QString& windowId);

    // Float toggle is entirely daemon-local — no effect-side slot needed.

    // Daemon tells the effect the drag routing has flipped mid-drag (cursor
    // crossed a virtual-screen boundary that changes autotile↔snap mode).
    // Effect applies the transition: entering/exiting autotile bypass,
    // canceling snap overlay, etc.
    void slotDragPolicyChanged(const QString& windowId, const PlasmaZones::DragPolicy& newPolicy);

    // Daemon-driven batch operations (rotate, resnap)
    void slotApplyGeometriesBatch(const WindowGeometryList& geometries, const QString& action);
    void slotRaiseWindowsRequested(const QStringList& windowIds);

    // Snap-all (effect collects candidates, daemon computes assignments)
    void slotSnapAllWindowsRequested(const QString& screenId);
    void slotPendingRestoresAvailable();
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    void slotRunningWindowsRequested();
    void slotRestoreSizeDuringDrag(const QString& windowId, int width, int height);
    void slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                             const PlasmaZones::EmptyZoneList& emptyZones);
    void slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y, int width,
                                               int height);

    // Snap-mode minimize/unminimize float tracking
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);

    // Daemon lifecycle
    void slotDaemonReady();

private:
    /// Continuation of slotDaemonReady() after the registerBridge reply has
    /// confirmed the daemon speaks a compatible protocol version. Separated
    /// so none of the state-pushing D-Bus calls can fire against a daemon
    /// that rejected the bridge handshake.
    void continueDaemonReadySetup();

public:
    /**
     * @brief Window identification — returns the opaque instance id.
     *
     * This is KWin's internalId() as a UUID string, which the daemon uses
     * as its runtime primary key. Populates m_appIdByInstance as a
     * side-effect so appIdForInstance() can answer quickly for subsequent
     * callers.
     */
    QString getWindowId(KWin::EffectWindow* w) const;

    /**
     * @brief Extract the compositor-supplied stable instance token.
     *
     * Alias for getWindowId() — kept so callers that want to be explicit
     * about wanting the instance id can say so. Both return the same bare
     * UUID string.
     */
    QString getWindowInstanceId(KWin::EffectWindow* w) const;

    /**
     * @brief Current app class for a window, read live from KWin.
     *
     * Prefers desktopFileName; falls back to normalized windowClass. Mutable —
     * KWin emits windowClassChanged / desktopFileNameChanged when the class
     * updates (Electron/CEF apps).
     */
    QString getWindowAppId(KWin::EffectWindow* w) const;

    /**
     * @brief Current app class for a windowId string (instance id).
     *
     * Accepts either a bare instance id or a legacy "appId|uuid" composite —
     * normalized via effectExtractInstanceId.
     *
     * Resolution order:
     *   1. Cached class for this instance id (m_appIdByInstance, populated
     *      by getWindowId()).
     *   2. Walk the stacking order to find a live EffectWindow with that
     *      internalId(), then call getWindowAppId(). Result is cached.
     *   3. Return empty if the window isn't currently known.
     *
     * A mid-session class mutation (Electron/CEF apps) returns the latest
     * value instead of a frozen first-seen parse.
     */
    QString appIdForInstance(const QString& windowId);

private:
    // Window management
    void setupWindowConnections(KWin::EffectWindow* w);

    /**
     * @brief Push current metadata for a window to the daemon's WindowRegistry.
     *
     * Safe to call unconditionally on every observation — the daemon de-dupes.
     * Called from slotWindowAdded for initial registration, and from
     * windowClassChanged / desktopFileNameChanged handlers for live updates.
     */
    void pushWindowMetadata(KWin::EffectWindow* w);

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
     * @brief Fire endDrag and apply the returned DragOutcome.
     *        Single entry point for drag-end dispatch,
     *        regardless of autotile bypass or snap path.
     *
     * @param window Dragged window (QPointer-protected in the async reply)
     * @param windowId Window identifier
     * @param cancelled True if the drag was cancelled (Escape / external)
     */
    void callEndDrag(KWin::EffectWindow* window, const QString& windowId, bool cancelled);
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

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

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
    AutotileHandler* autotileHandler() const
    {
        return m_autotileHandler.get();
    }

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
    /// Access the compositor bridge (for shared code that needs compositor-agnostic window ops)
    ICompositorBridge* compositorBridge() const
    {
        return m_compositorBridge.get();
    }

    /// Clear the EDID-based screen ID cache (call on screen add/remove/reconfigure)
    void clearScreenIdCache()
    {
        m_screenIdCache.clear();
    }

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
    friend class KWinCompositorBridge;
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

    // instance id → last observed appId. Populated lazily by getWindowId()
    // and pushWindowMetadata() so appIdForInstance() can answer without
    // walking the stacking order. Entries are removed in slotWindowClosed().
    //
    // This mirrors (but is independent of) the daemon's WindowRegistry — the
    // effect needs its own local view because appIdForInstance() is called
    // on hot paths (findWindowById, drag/snap decision logic) and shouldn't
    // round-trip over D-Bus.
    QHash<QString, QString> m_appIdByInstance;

    // Policy returned from the daemon's beginDrag for the currently-active
    // drag. Async-populated a few ms after the
    // drag starts; until then, conservative defaults apply (snap-path
    // with streaming) so the worst-case UX is a brief zone-overlay flash
    // rather than a dead drag. Cleared at drag end.
    DragPolicy m_currentDragPolicy;

    // Frame-geometry shadow push state. Effect debounces windowFrameGeometryChanged
    // signals per-window to ~50ms and pushes the latest geometry to the daemon via
    // WindowTracking::setFrameGeometry. Populates the daemon's frame-geometry
    // shadow used by daemon-local shortcut handlers (float toggle, etc.) so they
    // can read fresh geometry without a round-trip.
    QHash<QString, QRect> m_pendingFrameGeometry;
    QTimer* m_frameGeometryFlushTimer = nullptr;
    void flushPendingFrameGeometry();

    void updateWindowBorder(const QString& windowId, KWin::EffectWindow* w);
    void removeWindowBorder(const QString& windowId);
    void updateAllBorders();
    void clearAllBorders();

    std::unique_ptr<NavigationHandler> m_navigationHandler;
    std::unique_ptr<ScreenChangeHandler> m_screenChangeHandler;
    std::unique_ptr<SnapAssistHandler> m_snapAssistHandler;
    std::unique_ptr<WindowAnimator> m_windowAnimator;
    std::unique_ptr<DragTracker> m_dragTracker;
    std::unique_ptr<ICompositorBridge> m_compositorBridge;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;
    Qt::MouseButtons m_currentMouseButtons = Qt::NoButton;
    bool m_keyboardGrabbed = false;

    // D-Bus communication uses QDBusMessage::createMethodCall exclusively
    // (no QDBusInterface) to avoid synchronous D-Bus introspection that blocks
    // the compositor thread. See DBusHelpers::asyncCall() and DBusHelpers::fireAndForget().

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
    /**
     * @brief Detect activation trigger and grab keyboard if needed
     *
     * Sets m_dragActivationDetected and grabs keyboard when an activation
     * trigger is first detected during a drag. Returns true if activation
     * was detected (either previously or just now).
     */
    bool detectActivationAndGrab();

    // beginDrag is called unconditionally at drag-start; the deferred-send
    // optimization is obsolete now that the daemon always knows about the drag.

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
    QVector<ParsedTrigger> m_parsedTriggers; // pre-parsed via TriggerParser::parseTriggers() at load time (avoids
                                             // QVariant unboxing in hot path)
    bool m_triggersLoaded =
        false; // false until D-Bus reply arrives — permissive default bypasses trigger gating (#175)
    bool m_cachedToggleActivation = false;
    bool m_cachedAutotileDragInsertToggle = false;
    // AutotileDragBehavior cached so the synchronous drag-start fast path can
    // decide whether to skip the handleDragToFloat(immediate=true) call.
    // Refreshed by loadCachedSettings on every settingsChanged D-Bus
    // notification. Unknown values clamp to the safe default (Float) rather
    // than the highest-known value so an older effect build against a newer
    // daemon doesn't silently enter the wrong mode.
    EffectAutotileDragBehavior m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
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
    QString m_snapDragStartScreenId; // Virtual screen at snap-mode drag start (for VS crossing on drop)

    // Windows floated by drag on autotile screens. The daemon emits
    // applyGeometryRequested to restore pre-autotile geometry on float,
    // but drag-to-float should keep the window where the user dropped it.
    // Entries are consumed (removed) when slotApplyGeometryRequested skips
    // the geometry restore for a drag-floated window.
    QSet<QString> m_dragFloatedWindowIds;

    // Autotile: true when the current drag was started on an autotile screen

    // Snap-mode: windows floated due to minimize (mirrors autotile's m_minimizeFloatedWindows)
    QSet<QString> m_minimizeFloatedWindows;

    // Cached daemon D-Bus service registration state.
    // Updated via QDBusServiceWatcher signals (registration/unregistration) to avoid
    // synchronous isServiceRegistered() calls that block the compositor thread.
    // --- Daemon readiness / virtual screen fetch gate state ---
    bool m_daemonServiceRegistered = false;
    bool m_daemonReadyRestoresDone = false; ///< set after slotDaemonReady snap restores dispatched

    /// Pre-computed zone geometries for pending snap restores (appId → pixel rect).
    /// Fetched once from daemon on ready; consumed in slotWindowAdded for instant
    /// teleport (no D-Bus round-trip visible flash).
    QHash<QString, QRect> m_snapRestoreCache;
    bool m_virtualScreensReady = false; ///< set after all fetchVirtualScreenConfig replies arrive
    int m_pendingVsConfigReplies = 0; ///< countdown for fetchAllVirtualScreenConfigs async replies
    uint64_t m_vsConfigGeneration = 0; ///< generation counter for fetchAllVirtualScreenConfigs
    bool m_daemonReadyWindowStateProcessed = false; ///< re-entrancy guard for processDaemonReadyWindowState

    // Screen ID cache: connector name → EDID screen ID (manufacturer:model:serial).
    // Avoids repeated QScreen iteration and sysfs reads during drag (~30Hz).
    // Cleared on screen geometry changes (add/remove/reconfigure).
    mutable QHash<QString, QString> m_screenIdCache;

    // Window ID cache: EffectWindow* → "appId|uuid" (populated on first getWindowId call,
    // cleared in slotWindowClosed/windowDeleted). Eliminates 3-5 QString allocations per
    // getWindowId call across all hot paths (~1000-3000 allocs/sec during drag).
    mutable QHash<KWin::EffectWindow*, QString> m_windowIdCache;
    // Reverse lookup: windowId → EffectWindow* (for O(1) findWindowById)
    mutable QHash<QString, KWin::EffectWindow*> m_windowIdReverse;

    // Per-window tracked screen ID for cross-screen move detection.
    // Replaces the per-window `new QString` heap allocation that was leaked.
    QHash<KWin::EffectWindow*, QString> m_trackedScreenPerWindow;

    // Cursor output tracking (for daemon shortcut screen detection on Wayland)
    // Stores the connector name of the last output the cursor was on.
    // Used for deduplication only — the actual D-Bus call sends the EDID screen ID.
    QString m_lastCursorOutput;

    // Last effective screen ID reported to daemon (physical or virtual).
    // Used for deduplication of cursorScreenChanged D-Bus calls when virtual
    // screens subdivide a physical monitor — detects sub-screen crossings.
    QString m_lastEffectiveScreenId;

private:
    /**
     * @brief A single virtual screen subdivision within a physical monitor.
     *
     * Virtual screens divide a physical monitor into independent sub-screens,
     * each with its own zones, autotile state, etc. The daemon manages
     * definitions; the effect fetches them via D-Bus and resolves positions.
     *
     * Named EffectVirtualScreenDef to avoid collision with the daemon's
     * VirtualScreenDef (which has many more fields).
     */
    struct EffectVirtualScreenDef
    {
        QString id; ///< e.g., "Dell:U2722D:115107/vs:0"
        QRect geometry; ///< Absolute geometry in global compositor coords
    };

    /// Physical screen ID -> list of virtual screens (empty = no subdivisions)
    QHash<QString, QVector<EffectVirtualScreenDef>> m_virtualScreenDefs;

    /**
     * @brief Resolve a global point to the effective screen ID (virtual-aware).
     *
     * If the physical screen (from output) has virtual subdivisions, returns
     * the virtual screen ID whose geometry contains pos. Otherwise returns
     * the physical screen ID unchanged.
     *
     * @param pos Global compositor-space point
     * @param output The KWin output the point is on
     * @return Effective screen ID (virtual or physical)
     */
    QString resolveEffectiveScreenId(const QPoint& pos, const KWin::LogicalOutput* output) const;

    /// Fetch virtual screen config from daemon for a single physical screen
    void fetchVirtualScreenConfig(const QString& physicalScreenId, uint64_t generation = 0);

    /// Fetch virtual screen configs for all connected physical screens
    void fetchAllVirtualScreenConfigs();

    /// Process window state that depends on virtual screen definitions being loaded.
    /// Called from fetchAllVirtualScreenConfigs completion callback after all
    /// async D-Bus replies have arrived.
    void processDaemonReadyWindowState();

private Q_SLOTS:
    /// Handle daemon signal when virtual screen definitions change
    void onVirtualScreensChanged(const QString& physicalScreenId);
};

} // namespace PlasmaZones
