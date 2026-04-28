// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMargins>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>

#include <PhosphorLayer/Role.h>

#include "../core/interfaces.h"
#include <PhosphorZones/Layout.h>

namespace PhosphorLayer {
class ILayerShellTransport;
class IScreenProvider;
class Surface;
class SurfaceFactory;
// Role is a value type — full definition pulled in via Role.h above.
} // namespace PhosphorLayer

namespace PhosphorAnimationLayer {
class SurfaceAnimator;
} // namespace PhosphorAnimationLayer

namespace PhosphorZones {
class Zone;
}

namespace PhosphorLayout {
class ILayoutSource;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PhosphorAudio {
class IAudioSpectrumProvider;
}

namespace PhosphorSurfaces {
class SurfaceManager;
}

namespace PlasmaZones {
class WindowThumbnailService;
class ShaderRegistry;
}
namespace Phosphor::Screens {
class ScreenManager;
}
class QQuickWindow;
class QScreen;
class QTimer;

namespace PlasmaZones {

/**
 * @brief Manages zone overlay windows
 *
 * Creates and manages overlay windows per screen, updates appearance
 * from settings, and provides zone highlighting/visual feedback.
 */
class OverlayService : public IOverlayService
{
    Q_OBJECT

    Q_PROPERTY(bool visible READ isVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool zoneSelectorVisible READ isZoneSelectorVisible NOTIFY zoneSelectorVisibilityChanged)

public:
    /**
     * @brief Per-screen overlay state, grouping window pointers, physical screen
     * references, and geometry that were previously stored in parallel QHash maps.
     */
    struct PerScreenOverlayState
    {
        // PhosphorLayer-backed lifecycle handles. Own the QQuickWindow via their
        // internal transport. The parallel QQuickWindow* fields below are convenience
        // accessors cached from surface->window() at create time and preserved so the
        // hundreds of `window->setProperty(...)` call sites in overlayservice/*.cpp
        // don't each need to reach through a surface pointer.
        PhosphorLayer::Surface* overlaySurface = nullptr;
        PhosphorLayer::Surface* zoneSelectorSurface = nullptr;
        // Single per-screen surface that hosts both LayoutOsd and NavigationOsd
        // content via NotificationOverlay.qml's mode-driven Loader. The two
        // OSD types share PzRoles::OsdBase (FullscreenOverlay layer, AnchorAll,
        // no keyboard, click-through) and are never simultaneously visible,
        // so they collapse to one wl_surface / QSGRenderThread / VkSwapchainKHR
        // per effective screen. LayoutPickerOverlay uses a different protocol
        // shape (CenteredModal with exclusive keyboard) and keeps its own
        // surface — see pz_roles.h for the role definitions.
        PhosphorLayer::Surface* notificationSurface = nullptr;

        QQuickWindow* overlayWindow = nullptr;
        QScreen* overlayPhysScreen = nullptr;
        QRect overlayGeometry;
        QMetaObject::Connection overlayGeomConnection; ///< geometryChanged connection for overlay
        // Cache key for the last successful labelsTexture rebuild on this window.
        // Hashes (size, showNumbers, font settings, per-zone {number,x,y,w,h}). When
        // updateLabelsTextureForWindow is called with the same hash, both the 23 MB
        // QImage rebuild AND Qt's QVariant(QImage) property-write equality compare
        // are skipped. 0 = never computed / cache invalid.
        quint64 labelsTextureHash = 0;
        QQuickWindow* zoneSelectorWindow = nullptr;
        QScreen* zoneSelectorPhysScreen = nullptr;
        /// Intended geometry of the zone selector window. On Wayland LayerShell, QWindow::geometry()
        /// is unreliable until the compositor acknowledges the surface position. This field stores
        /// the geometry we requested so hit-testing in updateSelectorPosition() can use it immediately.
        QRect zoneSelectorGeometry;
        QQuickWindow* notificationWindow = nullptr;
        QScreen* notificationPhysScreen = nullptr;
    };

    /// @param screenManager Borrowed; must outlive this service.
    /// @param shaderRegistry Borrowed; must outlive this service. Used by
    ///                 every overlay path that resolves a shader by id.
    ///                 Nullable — passing nullptr disables shader-based
    ///                 overlays entirely (tests that don't exercise shaders).
    /// @param parent Qt parent.
    explicit OverlayService(Phosphor::Screens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                            QObject* parent = nullptr);
    ~OverlayService() override;

    // IOverlayService interface
    bool isVisible() const override;
    void show() override;
    void showAtPosition(int cursorX, int cursorY) override;
    void hide() override;
    void toggle() override;

    void updateLayout(PhosphorZones::Layout* layout) override;
    void updateSettings(ISettings* settings) override;
    void updateGeometries() override;

    // PhosphorZones::Zone highlighting for overlay display (IOverlayService interface)
    void highlightZone(const QString& zoneId) override;
    void highlightZones(const QStringList& zoneIds) override;
    void clearHighlight() override;

    // Mid-drag idle / resume — see IOverlayService for rationale.
    void setIdleForDragPause() override;
    void refreshFromIdle() override;

    // Additional methods
    void setLayout(PhosphorZones::Layout* layout);
    PhosphorZones::Layout* layout() const
    {
        return m_layout;
    }

    void setSettings(ISettings* settings);
    void setLayoutManager(PhosphorZones::LayoutRegistry* layoutManager);

    /// Inject the daemon-owned tile-algorithm registry. Required when
    /// autotile entries should appear in @ref visibleLayoutCount /
    /// @ref layoutListForScreen output. Borrowed — caller owns it and
    /// must keep it alive for the service's lifetime.
    void setAlgorithmRegistry(PhosphorTiles::ITileAlgorithmRegistry* registry);

    /// Inject the daemon's bundle-owned autotile layout source. Optional —
    /// when set, @ref buildUnifiedLayoutList reuses its internal preview
    /// cache across calls instead of constructing a transient source per
    /// call (which throws away the cache). Borrowed — caller owns it and
    /// must keep it alive for the service's lifetime.
    ///
    /// @note Expected to be called at most once. The service does not
    /// subscribe to the source's own signals — replacing the pointer
    /// later would not require a disconnect today, but matching the
    /// "set-once after construction" discipline used by every other
    /// setAutotileLayoutSource call site keeps the contract uniform.
    void setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source);
    Phosphor::Screens::ScreenManager* screenManager() const
    {
        return m_screenManager;
    }
    void setCurrentVirtualDesktop(int desktop);
    void setCurrentActivity(const QString& activityId);

    /**
     * @brief Set which layout types appear in the zone picker
     *
     * When autotile mode is active, show only dynamic layouts.
     * When manual mode is active, show only manual layouts.
     * The autotile feature gate (KCM setting) controls whether dynamic layouts
     * are ever visible.
     */
    void setLayoutFilter(bool includeManual, bool includeAutotile);

    /**
     * @brief Set screens to exclude from overlay display
     *
     * Used to suppress the overlay on autotile-managed screens in mixed
     * multi-monitor mode. The overlay will not be shown or updated on
     * screens whose names appear in the set.
     */
    void setExcludedScreens(const QSet<QString>& screenIds);

    // Screen management
    void setupForScreen(QScreen* screen);
    void removeScreen(QScreen* screen);
    void handleScreenAdded(QScreen* screen);
    void handleScreenRemoved(QScreen* screen);

    // PhosphorZones::Zone selector management (IOverlayService interface)
    bool isZoneSelectorVisible() const override;
    void showZoneSelector(const QString& targetScreenId = QString()) override;
    void hideZoneSelector() override;
    void updateSelectorPosition(int cursorX, int cursorY) override;
    void scrollZoneSelector(int angleDeltaY) override;

    // Mouse position for shader effects
    void updateMousePosition(int cursorX, int cursorY) override;

    // Filtered layout count for trigger edge computation
    int visibleLayoutCount(const QString& screenId) const override;

    // Selected zone from zone selector (IOverlayService interface)
    bool hasSelectedZone() const override;
    QString selectedLayoutId() const override
    {
        return m_selectedLayoutId;
    }
    int selectedZoneIndex() const override
    {
        return m_selectedZoneIndex;
    }
    QRect getSelectedZoneGeometry(QScreen* screen) const override;
    QRect getSelectedZoneGeometry(const QString& screenId) const override;
    void clearSelectedZone() override;

    // PhosphorZones::Layout OSD (visual preview when switching layouts)
    // screenId: target screen (empty = screen under cursor, fallback to primary)
    void showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId = QString());
    void showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                       bool autoAssign = false, const QString& screenId = QString(), bool showMasterDot = false,
                       bool producesOverlappingZones = false, const QString& zoneNumberDisplay = QStringLiteral("all"),
                       int masterCount = 1);
    void showLockedLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId = QString());
    void showDisabledOsd(const QString& reason, const QString& screenId = QString());

    /**
     * @brief Pre-create the unified NotificationOverlay window for all connected
     * screens. Drives both the layout-OSD and navigation-OSD show paths since
     * they share a single per-screen surface (Phase-2 unification).
     *
     * First-time QML compilation of NotificationOverlay.qml takes ~100-300ms
     * (component loading, scene graph creation, Wayland layer-shell
     * registration). Call this early (deferred from daemon start) so the
     * first layout switch OSD or keyboard navigation action appears
     * instantly instead of blocking the event loop.
     *
     * Idempotent — subsequent calls are no-ops thanks to the
     * m_notificationsWarmed latch and per-screen window guard.
     */
    void warmUpNotifications();

private:
    /**
     * @brief Install the QGuiApplication::screenAdded hook for the
     * notification overlay so hot-plugged monitors get a per-screen window
     * after the initial warm-up. Idempotent via m_screenAddedConnected
     * (lambdas can't use UniqueConnection).
     */
    void ensureOsdScreenAddedConnected();

public:
    // Navigation OSD (feedback for keyboard navigation)
    void showNavigationOsd(bool success, const QString& action, const QString& reason,
                           const QString& sourceZoneId = QString(), const QString& targetZoneId = QString(),
                           const QString& screenId = QString());

    // Shader preview overlay (editor Shader Settings dialog - dedicated window avoids multi-pass clear issues)
    void showShaderPreview(int x, int y, int width, int height, const QString& screenId, const QString& shaderId,
                           const QString& shaderParamsJson, const QString& zonesJson) override;
    void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                             const QString& zonesJson) override;
    void hideShaderPreview() override;

    // Snap Assist overlay (window picker after snapping)
    void showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                        const SnapAssistCandidateList& candidates) override;
    void hideSnapAssist() override;
    bool isSnapAssistVisible() const override;
    void setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl) override;

    // PhosphorZones::Layout Picker overlay (interactive layout browser + resnap)
    void showLayoutPicker(const QString& screenId = QString());
    bool isLayoutPickerVisible() const;

    /**
     * @brief Pre-create the Layout Picker QML window on the primary screen.
     *
     * The picker's first show otherwise pays ~50-100 ms for Wayland layer-
     * shell surface creation + Vulkan swapchain init + QML compilation.
     * Pre-warming on daemon start moves that cost off the user's hot path
     * so the very first picker invocation is instant. Subsequent shows on
     * the same screen reuse the warmed surface; cross-screen shows fall
     * back to destroy+recreate (wlr-layer-shell v3 anchors are immutable
     * post-attach).
     */
    void warmUpLayoutPicker();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

public Q_SLOTS:
    // hideLayoutOsd / hideNavigationOsd intentionally removed in the L3 v2
    // refactor: the QML side's _osdDismissed property (bound into
    // Qt.WindowTransparentForInput) is the sole mechanism for dismissing an
    // OSD — no C++ slot needs to run in response, because destroying the
    // QQuickWindow would re-introduce the blocking ~QQuickWindow Vulkan
    // teardown that the refactor is designed to avoid. Pre-warmed OSD
    // windows are reused for the daemon's entire lifetime.
    void hideLayoutPicker();
    void onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry);

    // Shader error reporting from QML
    void onShaderError(const QString& errorLog);

private Q_SLOTS:
    void onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson);
    void onLayoutPickerSelected(const QString& layoutId);

private:
    // Sync CAVA service state (start/stop/reconfigure) with current settings.
    void syncCavaState();

    // Refresh zone selector and overlay windows that are currently visible.
    // Skips hidden windows — showZoneSelector()/show() refresh before showing.
    void refreshVisibleWindows();

    // Connect to a PhosphorZones::Layout's layoutModified signal so live edits from the editor
    // (shader id/params, zone geometry, appearance) propagate to the live overlay
    // without waiting for a layout switch or daemon restart.
    void observeLayoutForLiveEdits(PhosphorZones::Layout* layout);

    // Stop observing a layout (e.g. because PhosphorZones::LayoutRegistry just removed it).
    // Disconnects the per-layout layoutModified signal and erases the entry
    // from m_observedLayouts. Idempotent — calling for an unobserved layout
    // is a no-op.
    void stopObservingLayout(PhosphorZones::Layout* layout);

    // Hide overlay/selector windows on screens where the current context is disabled,
    // then update remaining visible windows. Used by setCurrentVirtualDesktop/Activity.
    void hideDisabledAndRefresh();

    void createOverlayWindow(QScreen* screen);
    void destroyOverlayWindow(QScreen* screen);
    void dismissOverlayWindow(QScreen* screen);
    void updateOverlayWindow(QScreen* screen);
    void recreateOverlayWindowsOnTypeMismatch();

    /**
     * @brief Create/destroy/update overlay windows keyed by screen ID
     *
     * Virtual-screen-aware overloads. The screenId can be a physical screen ID
     * or a virtual screen ID (format "physicalId/vs:N"). The physScreen is the
     * backing QScreen* for Wayland layer-shell parenting.
     */
    void createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry);
    void destroyOverlayWindow(const QString& screenId);
    void dismissOverlayWindow(const QString& screenId);
    void updateOverlayWindow(const QString& screenId, QScreen* physScreen);

    // Move a live overlay entry from oldKey to newKey. Used when the effective
    // screen id for the same physical monitor flips between a virtual variant
    // ("...:115107/vs:0") and the bare physical id ("...:115107"), so the
    // existing QQuickWindow + VkSwapchainKHR is reused instead of torn down.
    // Returns true if a rekey happened.
    bool rekeyOverlayState(const QString& oldKey, const QString& newKey);

    // Install a QScreen::geometryChanged watcher that keeps the per-screen
    // overlay window's size / stored geometry / margins in sync with the
    // physical monitor's new bounds. Shared by createOverlayWindow and
    // rekeyOverlayState so both call sites route through the same lambda.
    // sid is captured by value so the watcher keeps working after a rekey.
    QMetaObject::Connection installOverlayGeometryWatcher(QScreen* physScreen, const QString& screenId, bool isVS);

    // Debug-build invariant check: every m_screenStates entry either has a key
    // present in targetIds or is a distinct physical monitor from every target.
    // Catches orphan accumulation from effective-id jitter. No-op in release.
    void validateScreenStateInvariant(const QStringList& targetIds) const;

    // Write _idled=true/false on every live overlay window based on which
    // VS the cursor is currently on. One-overlay-per-VS architecture: all
    // overlay windows stay alive for their lifetime, and per-window idle
    // state controls content.visible + Qt.WindowTransparentForInput via
    // the QML _idled property. Only the cursor's VS is un-idled in
    // single-monitor mode; every overlay is un-idled when showOnAllMonitors.
    // An empty activeEffectiveId idles every overlay (used by
    // setIdleForDragPause when no VS should be active).
    void applyIdleStateForCursor(const QString& activeEffectiveId, bool showOnAllMonitors);

    void updateLabelsTextureForWindow(QQuickWindow* window, const QVariantList& patched, QScreen* screen,
                                      PhosphorZones::Layout* screenLayout);
    QVariantList buildZonesList(QScreen* screen) const;
    QVariantList buildZonesList(const QString& screenId, QScreen* physScreen) const;
    QVariantList buildLayoutsList(const QString& screenId = QString()) const;
    QVariantMap zoneToVariantMap(PhosphorZones::Zone* zone, QScreen* screen,
                                 PhosphorZones::Layout* layout = nullptr) const;
    QVariantMap zoneToVariantMap(PhosphorZones::Zone* zone, const QString& screenId, QScreen* physScreen,
                                 const QRect& overlayGeometry, PhosphorZones::Layout* layout = nullptr) const;

    /**
     * @brief Resolve the layout for a given screen with fallback chain
     *
     * Tries: per-screen assignment → activeLayout → m_layout
     */
    PhosphorZones::Layout* resolveScreenLayout(QScreen* screen) const;
    PhosphorZones::Layout* resolveScreenLayout(const QString& screenId) const;

    // PhosphorLayer infrastructure — owns the wlr-layer-shell binding, screen
    // enumeration, and Surface factory for all overlay-style windows. Members
    // ordered so factory is destroyed before provider/transport (factory keeps
    // raw pointers to the other two).
    std::unique_ptr<PhosphorLayer::IScreenProvider> m_screenProvider;
    std::unique_ptr<PhosphorLayer::ILayerShellTransport> m_transport;
    /// Phase-5 SurfaceAnimator. Drives show/hide visual transitions for
    /// every Surface this service creates. Forward-declared to keep the
    /// phosphor-animation-layer header out of the daemon's public surface;
    /// the unique_ptr destructor only needs the type at .cpp definition
    /// time. MUST outlive m_surfaceFactory (the factory's Deps captures
    /// the animator pointer; surfaces it produces dispatch through it on
    /// every show/hide).
    std::unique_ptr<PhosphorAnimationLayer::SurfaceAnimator> m_surfaceAnimator;
    std::unique_ptr<PhosphorLayer::SurfaceFactory> m_surfaceFactory;

    // Managed surface lifecycle: shared QQmlEngine, Vulkan keep-alive, scope generation.
    std::unique_ptr<PhosphorSurfaces::SurfaceManager> m_surfaceManager;

    QHash<QString, PerScreenOverlayState> m_screenStates;
    QPointer<PhosphorZones::Layout> m_layout;
    QPointer<ISettings> m_settings;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives service
    ShaderRegistry* m_shaderRegistry = nullptr; ///< Borrowed; outlives service
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr; ///< Borrowed; outlives service (optional)
    Phosphor::Screens::ScreenManager* m_screenManager = nullptr;
    QList<QPointer<PhosphorZones::Layout>> m_observedLayouts; ///< Layouts we watch for live edits

    // Precise disconnect handles for signal sources whose slots are lambdas
    // (disconnect(src, sig, this, nullptr) would sever ALL slots matching —
    // safe today but trap-prone if a second connection is ever added).
    QMetaObject::Connection m_shadersChangedConnection;
    // Debounce layoutModified → refreshVisibleWindows. layoutModified fires on
    // every Q_PROPERTY change (e.g. per-frame during a zone drag), so
    // coalescing prevents redundant rebuilds of zone variant lists + label
    // texture re-uploads. Guarded by the single-shot timer pattern: first
    // fire starts a timer; subsequent fires before the timer elapses do
    // nothing; the timer callback runs refreshVisibleWindows once.
    bool m_refreshCoalescePending = false;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based)
    QString m_currentActivity; // Current KDE activity (empty = all activities)
    bool m_visible = false;
    bool m_zoneSelectorVisible = false;
    bool m_zoneSelectorRecreationPending =
        false; // Guard against re-entrant showZoneSelector during deferred recreation
    QString m_currentOverlayScreenId; // Effective screen ID overlay is shown on (single-monitor mode, for #136)

    // PhosphorZones::Zone selector selection tracking
    QString m_selectedLayoutId;
    int m_selectedZoneIndex = -1;
    QRectF m_selectedZoneRelGeo;

    // PhosphorZones::Layout OSD and Navigation OSD windows are stored in m_screenStates

    // Shader preview overlay (editor dialog)
    PhosphorLayer::Surface* m_shaderPreviewSurface = nullptr;
    QQuickWindow* m_shaderPreviewWindow = nullptr;
    QPointer<QScreen> m_shaderPreviewScreen;
    QString m_shaderPreviewShaderId; // Shader ID for param translation in updateShaderPreview
    QString m_shaderPreviewScreenId; // Virtual screen ID from showShaderPreview (avoids re-resolving from QScreen*)

    // Snap Assist overlay (window picker after snapping)
    PhosphorLayer::Surface* m_snapAssistSurface = nullptr;
    QQuickWindow* m_snapAssistWindow = nullptr;
    QPointer<QScreen> m_snapAssistScreen;
    QString m_snapAssistScreenId;
    std::unique_ptr<WindowThumbnailService> m_thumbnailService;
    QVariantList m_snapAssistCandidates; // Mutable copy for async thumbnail updates
    QStringList m_thumbnailCaptureQueue; // Sequential capture to avoid overwhelming KWin
    QHash<QString, QString> m_thumbnailCache; // compositorHandle -> dataUrl; reused across continuation
    // PhosphorZones::Layout Picker overlay (interactive layout browser)
    PhosphorLayer::Surface* m_layoutPickerSurface = nullptr;
    QQuickWindow* m_layoutPickerWindow = nullptr;
    QPointer<QScreen> m_layoutPickerScreen;
    QString m_layoutPickerScreenId;

    bool m_screenAddedConnected = false; // Guard for screenAdded connection (lambdas can't use UniqueConnection)
    // "Notifications have been pre-warmed" flag. With LayoutOsd and
    // NavigationOsd unified onto a single per-screen NotificationOverlay
    // surface, this single flag gates whether the screenAdded hot-plug
    // lambda auto-creates the per-new-screen notification window.
    // Set by warmUpNotifications().
    bool m_notificationsWarmed = false;

    // Keep-alive is managed by m_surfaceManager (PhosphorSurfaces::SurfaceManager).

    // Remembered so ~OverlayService can disconnect the D-Bus PrepareForSleep
    // subscription explicitly rather than relying on QDBusConnection's
    // internal receiver-destroyed detection (which works, but leaves a dead
    // entry in the connection's slot table for the rest of the session).
    bool m_prepareForSleepConnected = false;

    // Track screens where notification-window creation has failed, so the
    // spam-guard in createNotificationWindow only logs once per screen
    // regardless of which OSD path (layout-osd or navigation-osd) tried to
    // bring the surface up. Cleared in destroyAllWindowsForPhysicalScreen
    // when a hot-plug cycle reattaches the same physical monitor.
    QSet<QString> m_notificationCreationFailed;
    // Deduplicate navigation feedback (prevent duplicate OSDs from Qt signal + D-Bus signal)
    QString m_lastNavigationActionKey; // "action:reason" composite key
    QString m_lastNavigationScreenId;
    QElapsedTimer m_lastNavigationTime;

    void createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom);
    void destroyZoneSelectorWindow(const QString& screenId);
    void updateZoneSelectorWindow(const QString& screenId);
    void showLayoutOsdImpl(PhosphorZones::Layout* layout, const QString& screenId, bool locked);
    /// Create / destroy the per-screen NotificationOverlay window that hosts
    /// both LayoutOsd and NavigationOsd content via NotificationOverlay.qml's
    /// mode-driven Loader. Replaces the previous per-mode
    /// createLayoutOsdWindow / createNavigationOsdWindow pair — Phase-2
    /// surface unification (the two OSD types share PzRoles::OsdBase and are
    /// never simultaneously visible). Phase-5 keepMappedOnHide=true and
    /// dismissRequested → Surface::hide() wiring are done inside
    /// createWarmedOsdSurface.
    void createNotificationWindow(const QString& screenId, QScreen* physScreen);
    void destroyNotificationWindow(const QString& screenId);

    void destroyIfTypeMismatch(const QString& screenId);
    void createShaderPreviewWindow(QScreen* screen, const QString& screenId = QString());
    void destroyShaderPreviewWindow();

    /// Destroy all overlay, OSD, zone selector, snap assist, and layout picker windows
    /// backed by the given physical screen. Used by both virtualScreensChanged and handleScreenRemoved.
    void destroyAllWindowsForPhysicalScreen(QScreen* screen);

    void createSnapAssistWindow(QScreen* physScreen);
    void createSnapAssistWindowFor(QScreen* physScreen, const QRect& screenGeom, const QString& resolvedId);
    void destroySnapAssistWindow();

    void createLayoutPickerWindow(QScreen* physScreen);
    void createLayoutPickerWindowFor(QScreen* physScreen, const QRect& screenGeom, const QString& resolvedId);
    void destroyLayoutPickerWindow();

    /**
     * @brief Construct the SurfaceAnimator and register per-Role configs.
     *
     * Phase 5 of the phosphor-animation roadmap: a single library-driven
     * animator drives show/hide across every overlay (LayoutOsd,
     * NavigationOsd, LayoutPicker, ZoneSelector, SnapAssist) using
     * Profile-resolved curves shared with in-window animations. Called
     * exactly once from the ctor; the animator's lifetime is tied to
     * `*this`.
     */
    void setupSurfaceAnimator();

    /** Update a candidate's thumbnail in m_snapAssistCandidates and push to QML. */
    void updateSnapAssistCandidateThumbnail(const QString& compositorHandle, const QString& dataUrl);
    /** Process next thumbnail in queue (sequential capture to avoid KWin overload). */
    void processNextThumbnailCapture();

    /**
     * @brief Re-assert a window's screen and geometry before showing on Wayland
     *
     * The QPA plugin binds the Wayland output once during LayerSurface/platform
     * window construction. Set QWindow::screen() BEFORE the window is shown.
     */
    static void assertWindowOnScreen(QWindow* window, QScreen* screen, const QRect& geometry = QRect());

    /**
     * @brief Prepare layout OSD window for display
     * @param window Output: the prepared window (nullptr on failure)
     * @param outSurface Output: the backing PhosphorLayer::Surface (nullptr on failure)
     * @param screenGeom Output: screen geometry
     * @param aspectRatio Output: calculated aspect ratio
     * @param screenId Target screen (empty = primary)
     * @return true if window is ready, false on failure
     */
    bool prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface, QScreen*& outPhysScreen,
                                QRect& screenGeom, qreal& aspectRatio, const QString& screenId = QString());

    /**
     * @brief Parameters for @ref createLayerSurface, kept as a named-member
     *        aggregate so call sites read top-to-bottom rather than relying
     *        on positional arg ordering. Required fields up top, optional
     *        below; Qt6 designated-init form is `LayerSurfaceParams{.qmlUrl=...}`.
     */
    struct LayerSurfaceParams
    {
        // Required.
        QUrl qmlUrl = {}; ///< QML file (Window-rooted — PZ's overlay QML convention)
        QScreen* screen = nullptr; ///< target screen (physical; virtual-screen positioning is the caller's job)
        PhosphorLayer::Role role = {}; ///< protocol-level preset (see pz_roles.h)
        const char* windowType = ""; ///< debug/telemetry label

        // Optional — explicit `= {}` suppresses GCC's
        // -Wmissing-field-initializers warning on designated-init call sites
        // that omit these. (For `QUrl` / `Role` above the same is true; we
        // just want one consistent style across the struct.)
        QVariantMap windowProperties = {}; ///< Applied as dynamic properties before QML loads.
        std::optional<PhosphorLayer::Anchors> anchorsOverride =
            std::nullopt; ///< Overrides the role's anchors (virtual-screen positioning).
        std::optional<QMargins> marginsOverride =
            std::nullopt; ///< Overrides the role's margins (virtual-screen positioning).
        bool keepMappedOnHide = false; ///< See SurfaceConfig::keepMappedOnHide.
    };

    /**
     * @brief Create a PhosphorLayer::Surface for a layer-shell-backed overlay window.
     *
     * Every overlay, OSD, zone selector, snap assist, layout picker, and shader
     * preview in OverlayService goes through this single helper. Returns a surface
     * that has been warmed up (window created, QML loaded, transport attached) but
     * is hidden — callers decide when to call @c surface->show() or keep it warm
     * for pre-warmed OSDs.
     *
     * @return the surface on success; nullptr on failure (warnings logged internally).
     */
    PhosphorLayer::Surface* createLayerSurface(LayerSurfaceParams params);

    /**
     * @brief Create a warmed OSD-style surface and wire its dismiss signal.
     *
     * Common pattern for createNotificationWindow (and the LayoutPicker
     * surface in snapassist.cpp): (1) build a per-instance scope-prefixed
     * Role from the base role, (2) call createLayerSurface with
     * keepMappedOnHide=true, (3) string-connect the QML-side
     * `dismissRequested()` signal to `Surface::hide()` so the auto-dismiss
     * timer (or backdrop click for the picker) can drive the library
     * animator without a C++ slot in the loop.
     *
     * Returns the warmed Surface on success; nullptr on failure (warning
     * logged inside createLayerSurface). Caller installs the surface +
     * window pointers into PerScreenOverlayState.
     *
     * @param baseRole       Base role to scope-prefix (e.g. PzRoles::OsdBase).
     * @param scopePrefix    Prefix string template after the base role's
     *                       prefix (typically `"plasmazones-{kind}-{screenId}-{gen}"`).
     * @param qmlUrl         QML file to load.
     * @param physScreen     Target physical screen.
     * @param windowType     Debug/telemetry label.
     */
    PhosphorLayer::Surface* createWarmedOsdSurface(const PhosphorLayer::Role& baseRole, const QString& scopePrefix,
                                                   const QUrl& qmlUrl, QScreen* physScreen, const char* windowType);

    // Audio viz: push spectrum to overlay windows
    void onAudioSpectrumUpdated(const QVector<float>& spectrum);

    // Shader support methods
    bool useShaderForScreen(QScreen* screen) const;
    bool useShaderForScreen(const QString& screenId) const;
    bool anyScreenUsesShader() const;
    bool canUseShaders() const;
    void startShaderAnimation();
    void stopShaderAnimation();
    void updateShaderUniforms();
    void updateZonesForAllWindows();

    /**
     * @brief Initialize and show overlay for a given screen or cursor position
     * @param cursorScreen Screen where cursor is located (nullptr = show on all monitors)
     *
     * This is the common implementation for show() and showAtPosition().
     * Extracts ~100 lines of duplicate code from both methods.
     */
    void initializeOverlay(QScreen* cursorScreen, const QPoint& cursorPos = QPoint(-1, -1));

private Q_SLOTS:
    // System sleep/resume handler (connected to logind PrepareForSleep signal)
    void onPrepareForSleep(bool goingToSleep);

private:
    // Shader timing (shared across all monitors for synchronized effects)
    QElapsedTimer m_shaderTimer;
    std::atomic<qint64> m_lastFrameTime{0};
    std::atomic<int> m_frameCount{0};
    QTimer* m_shaderUpdateTimer = nullptr;
    mutable QMutex m_shaderTimerMutex;

    // Shader state
    bool m_zoneDataDirty = true;
    QString m_pendingShaderError;

    // Scope generation delegated to m_surfaceManager->nextScopeGeneration().

    // Audio spectrum provider (CAVA backend via phosphor-audio)
    std::unique_ptr<PhosphorAudio::IAudioSpectrumProvider> m_audioProvider;

    // PhosphorZones::Zone data version for shader synchronization
    int m_zoneDataVersion = 0;

    // PhosphorZones::Layout filter: which types to include in zone picker (set by Daemon)
    bool m_includeManualLayouts = true;
    bool m_includeAutotileLayouts = false;

    // Screens excluded from overlay display (autotile-managed screens)
    QSet<QString> m_excludedScreens;
};

} // namespace PlasmaZones
