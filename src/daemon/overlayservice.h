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
#include <QSize>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>

#include <PhosphorLayer/Role.h>
#include <PhosphorOverlay/ShellState.h>

#include "../core/interfaces.h"
#include <PhosphorZones/Layout.h>

namespace PhosphorLayer {
class ILayerShellTransport;
class IScreenProvider;
class Surface;
class SurfaceFactory;
// Role is a value type - full definition pulled in via Role.h above.
} // namespace PhosphorLayer

namespace PhosphorOverlay {
class ShellHost;
} // namespace PhosphorOverlay

namespace PhosphorAnimationLayer {
class SurfaceAnimator;
} // namespace PhosphorAnimationLayer

namespace PhosphorAnimation {
class PhosphorProfileRegistry;
} // namespace PhosphorAnimation

namespace PhosphorZones {
class IZoneLayoutRegistry;
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

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PlasmaZones {
class ShaderRegistry;
class SnapAssistThumbnailProvider;
class DmabufTextureProvider;
}
namespace PhosphorScreens {
class ScreenManager;
}
class QQuickWindow;
class QQuickItem;
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
        // Library-owned shell-host state: borrowed pointer into the
        // PhosphorOverlay::ShellHost-owned map. Set by the
        // @c ensurePassiveShellFor shim after @c ShellHost::ensureShell
        // materializes a lib-side entry; the pointer is stable across
        // map operations because ShellHost stores entries through
        // raw owning pointers. nullptr until the first ensure call wires
        // it up.
        //
        // The pointee is the single source of truth for shell mechanism
        // fields (shellSurface / shellWindow / physScreen / slots);
        // writes route through @c ShellHost::ensureShell /
        // @c ShellHost::destroyShell.
        //
        // Per-content "is this slot wired up?" sentinels (overlayPhysScreen
        // / zoneSelectorPhysScreen / ...) live below - they're PZ-content
        // state, not lib-mechanism state.
        PhosphorOverlay::ShellState* shell = nullptr;

        /// Convenience accessors that resolve the named PZ slot through
        /// the shell's generic slot map. Returns nullptr when no shell
        /// is wired up, or when the QML aliasing in PassiveOverlayShell.qml
        /// did not expose the requested slot Item.
        QQuickItem* osdSlot() const;
        QQuickItem* snapAssistSlot() const;
        QQuickItem* layoutPickerSlot() const;
        QQuickItem* zoneSelectorSlot() const;
        QQuickItem* mainOverlaySlot() const;

        // overlayPhysScreen != nullptr is the sentinel for "main overlay
        // mode is active on this screen" - set in createOverlayWindow,
        // cleared in destroyOverlayWindow / by the PreDestroyCallback
        // registered on m_shellHost (OverlayService::unwirePassiveShellSlots).
        QScreen* overlayPhysScreen = nullptr;
        QRect overlayGeometry;
        QMetaObject::Connection overlayGeomConnection; ///< geometryChanged connection for overlay
        // Cache key for the last successful labelsTexture rebuild on this window.
        // Hashes (size, showNumbers, font settings, per-zone {number,x,y,w,h}). When
        // updateLabelsTextureForWindow is called with the same hash, both the sparse
        // glyph-tile payload rebuild AND the labelsTexture property write (with its
        // value compare) are skipped. 0 = never computed / cache invalid.
        quint64 labelsTextureHash = 0;
        QScreen* zoneSelectorPhysScreen = nullptr;
        /// Intended geometry of the zone selector inside its shell. On
        /// Wayland LayerShell, QWindow::geometry() is unreliable until
        /// the compositor acknowledges surface position; this field
        /// stores the geometry we requested so hit-testing in
        /// updateSelectorPosition() has a stable reference.
        QRect zoneSelectorGeometry;
    };

    /// @param screenManager Borrowed; must outlive this service.
    /// @param shaderRegistry Borrowed; must outlive this service. Used by
    ///                 every overlay path that resolves a shader by id.
    ///                 Nullable - passing nullptr disables shader-based
    ///                 overlays entirely (tests that don't exercise shaders).
    /// @param profileRegistry Borrowed; must outlive this service.
    ///                 Threaded into the SurfaceAnimator that drives every
    ///                 overlay show/hide. Composition roots (the daemon)
    ///                 own a single PhosphorProfileRegistry instance and
    ///                 hand it through here - the singleton accessor is
    ///                 gone (Phase A3 of the architecture refactor).
    /// @param parent Qt parent.
    explicit OverlayService(PhosphorScreens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                            PhosphorAnimation::PhosphorProfileRegistry* profileRegistry, QObject* parent = nullptr);
    ~OverlayService() override;

    // IOverlayService interface
    bool isVisible() const override;
    void show() override;
    void showAtPosition(int cursorX, int cursorY) override;
    void hide() override;
    void toggle() override;

    void updateLayout(PhosphorZones::Layout* layout) override;
    void updateSettings(ISettings* settings) override;
    void setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry);
    void updateGeometries() override;

    // PhosphorZones::Zone highlighting for overlay display (IOverlayService interface)
    void highlightZone(const QString& zoneId) override;
    void highlightZones(const QStringList& zoneIds) override;
    void clearHighlight() override;

    // Mid-drag idle / resume - see IOverlayService for rationale.
    void setIdleForDragPause() override;
    void refreshFromIdle() override;

    // Additional methods
    void setLayout(PhosphorZones::Layout* layout);
    PhosphorZones::Layout* layout() const
    {
        return m_layout;
    }

    void setSettings(ISettings* settings);
    void setLayoutManager(PhosphorZones::IZoneLayoutRegistry* layoutManager);

    /// Inject the daemon-owned tile-algorithm registry. Required when
    /// autotile entries should appear in @ref visibleLayoutCount /
    /// @ref layoutListForScreen output. Borrowed - caller owns it and
    /// must keep it alive for the service's lifetime.
    void setAlgorithmRegistry(PhosphorTiles::ITileAlgorithmRegistry* registry);

    /// Inject the daemon's bundle-owned autotile layout source. Optional -
    /// when set, @ref buildUnifiedLayoutList reuses its internal preview
    /// cache across calls instead of constructing a transient source per
    /// call (which throws away the cache). Borrowed - caller owns it and
    /// must keep it alive for the service's lifetime.
    ///
    /// @note Expected to be called at most once. The service does not
    /// subscribe to the source's own signals - replacing the pointer
    /// later would not require a disconnect today, but matching the
    /// "set-once after construction" discipline used by every other
    /// setAutotileLayoutSource call site keeps the contract uniform.
    void setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source);
    PhosphorScreens::ScreenManager* screenManager() const
    {
        return m_screenManager;
    }
    void setCurrentVirtualDesktop(int desktop);
    /// This screen's current virtual desktop under Plasma 6.7 per-output virtual
    /// desktops (#648). Delegates to the layout registry — the single source of
    /// truth for the per-output desktop map — so overlay resolution matches
    /// layout resolution; falls back to the global desktop when no registry is
    /// wired.
    int currentVirtualDesktopForScreen(const QString& screenId) const;
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

    /// Reconcile overlay state for a physical-screen reconfig event.
    /// Wired to both @c ScreenManager::virtualScreensChanged (add /
    /// remove / re-cache of virtual screens under @p physicalScreenId)
    /// and @c ScreenManager::virtualScreenRegionsChanged (swap /
    /// rotate / boundary resize). The handler is heavy but only runs
    /// when overlays are visible (active drag), so the cost is bounded.
    void onVirtualScreensChanged(const QString& physicalScreenId);

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
     * @brief Pre-create the per-screen passive overlay shell for all connected
     * screens. Drives both the layout-OSD and navigation-OSD show paths since
     * they share a single per-screen surface (Phase-2 unification).
     *
     * First-time QML compilation of PassiveOverlayShell.qml takes ~100-300ms
     * (component loading, scene graph creation, Wayland layer-shell
     * registration). Call this early (deferred from daemon start) so the
     * first layout switch OSD or keyboard navigation action appears
     * instantly instead of blocking the event loop.
     *
     * Idempotent - subsequent calls are no-ops thanks to the
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

    /**
     * @brief Prime a freshly-created Surface's render pipeline.
     *
     * Surface::warmUp() pre-loads the QML scene graph but DOES NOT map the
     * wl_surface or initialise the Vulkan swapchain - those happen on the
     * first Surface::show(). For surfaces that drive shader-exclusive
     * transitions through SurfaceAnimator, the first show racing the
     * map+swapchain init also races the QSGLayer's first capture for the
     * shader's iChannel0: the layer's source-item hasn't rendered yet, so
     * the shader's first frame samples an empty/stale FBO and visibly
     * flashes for shaders whose iTime=0 frame is opaque (popin/morph/
     * pixelate/glitch).
     *
     * This helper does a sacrificial show+hide on the surface so the
     * compositor maps the wl_surface, Vulkan initialises the swapchain,
     * the QSGLayer renders at least one frame, and the surface lands
     * back in State::Hidden with the QQuickWindow still mapped (every
     * surface that uses this helper has SurfaceConfig::keepMappedOnHide
     * = true, so the wl_surface stays alive across the hide).
     *
     * Surfaces are tracked in m_primingSurfaces so a user-triggered show
     * landing during the prime window can cancel the queued hide via
     * cancelSurfacePrime() and avoid a race that would visibly hide the
     * user's freshly-shown content. The QML during prime has mode="" /
     * no content (loader.sourceComponent==null), so the user sees
     * nothing during the cycle even if the prime show animation fires.
     */
    void primeSurfaceRenderPipeline(PhosphorLayer::Surface* surface);

    /**
     * @brief Cancel a queued prime hide for @p surface.
     *
     * Call right BEFORE a user-triggered Surface::show() to disarm the
     * frameSwapped one-shot connection installed by primeSurface-
     * RenderPipeline. No-op for surfaces that aren't currently priming.
     */
    void cancelSurfacePrime(PhosphorLayer::Surface* surface);

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
    void showSnapAssist(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                        const PhosphorProtocol::SnapAssistCandidateList& candidates) override;
    void hideSnapAssist() override;
    bool isSnapAssistVisible() const override;
    bool setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                const QByteArray& pixels) override;
    bool setWindowThumbnailDmabuf(const QString& compositorHandle, const DmabufThumbnailDesc& desc) override;

    // PhosphorZones::Layout Picker overlay (interactive layout browser + resnap)
    void showLayoutPicker(const QString& screenId = QString());
    bool isLayoutPickerVisible() const override;

    /// Forwarders to the active picker slot's QML moveSelection /
    /// confirmSelection functions. Used by global-accel callbacks
    /// (registered by WindowDragAdaptor on picker show) since the
    /// shell is kbd-None and the picker content's QML Shortcuts can't
    /// fire. No-op when no picker is visible.
    void pickerMoveSelection(int dx, int dy);
    void pickerConfirmSelection();

    /// Re-push the context lock state to any open zone selector and the layout
    /// picker. Called when window rules change at runtime (e.g. a `LockContext`
    /// rule is toggled, re-prioritised or re-matched) so an already-visible
    /// overlay's lock affordance updates in place instead of waiting for the
    /// next show — mirroring how `ISettings::settingsChanged` refreshes the
    /// selectors for manual-lock edits. The authoritative block is still the
    /// live re-check at commit (the selector hit-test and `isScreenLocked`);
    /// this only keeps the visual in sync.
    void refreshContextLockState();

    /// Re-apply the (possibly rule-overridden) overlay shader / style to a
    /// currently-displaying overlay after a window-rule change. A rule edit bumps
    /// the rule-set revision (so the next `LayoutRegistry::resolveContextOverlay`
    /// read drops its now-stale cache) but does not itself re-query it for the
    /// live main overlay; this flips any
    /// shader↔non-shader slot whose type the new style override changed, then
    /// re-pushes each window's shader id/params. A no-op when the overlay is
    /// hidden — the next `show()` re-resolves via `initializeOverlay`. Mirrors
    /// the `overlayDisplayModeChanged` / `enableShaderEffectsChanged` wiring for
    /// the equivalent global settings.
    void refreshOverlayPropertiesIfShown();

public Q_SLOTS:
    // hideLayoutOsd / hideNavigationOsd intentionally absent. Phase-5
    // dismiss path: QML auto-dismiss timer → loaded content's
    // dismissRequested() → host PassiveOverlayShell re-emits → wired by
    // createWarmedOsdSurface to Surface::hide() → SurfaceAnimator::beginHide
    // → PhosphorLayer::Surface flips Qt::WindowTransparentForInput on the
    // still-mapped QWindow. No C++ slot runs on dismiss - destroying the
    // QQuickWindow would re-introduce the blocking ~QQuickWindow Vulkan
    // teardown that the warm-surface design is meant to avoid. Pre-warmed
    // OSD windows are reused for the daemon's entire lifetime.
    void hideLayoutPicker() override;

    // Shader error reporting from QML
    void onShaderError(const QString& errorLog);

private Q_SLOTS:
    void onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson);
    void onLayoutPickerSelected(const QString& layoutId);
    /// Receiver for the unified passive shell's `osdDismissRequested`
    /// QML signal. Resolves the emitting shell window via `sender()`,
    /// finds the matching m_screenStates entry, and runs an animated
    /// slot-hide via SurfaceAnimator::beginHide on (shellSurface,
    /// osdSlotItem) - the shell wl_surface itself stays mapped, only
    /// the OSD slot Item's opacity animates to 0.
    void onOsdDismissRequested();

    /// Receiver for the shell's `snapAssistDismissRequested` QML signal
    /// (backdrop click + the Escape global accel routes to
    /// `hideSnapAssist` directly). Same animator-driven slot-hide
    /// pattern as onOsdDismissRequested.
    void onSnapAssistDismissRequested();

    /// Receiver for the shell's `layoutPickerDismissRequested` QML
    /// signal (backdrop click). Routes to hideLayoutPicker.
    void onLayoutPickerDismissRequested();

private:
    // Sync CAVA service state (start/stop/reconfigure) with current settings AND
    // whether the overlay is actually displaying content — CAVA runs only while
    // the overlay (un-idled) or shader preview is on screen.
    void syncCavaState();
    // Whether the overlay is actively displaying content right now: visible and
    // not in the warm-idled drag-pause/drag-end state (or the shader preview is
    // up). The overlay QQuickWindows are kept alive across drags to dodge an
    // NVIDIA teardown deadlock, so "visible" alone stays true at rest — this is
    // the predicate that gates the 60 Hz shader render loop + CAVA.
    bool isOverlayDisplaying() const;
    // Defer stopping the render loop + CAVA by a short grace period after the
    // overlay goes idle, so rapid drag thrash keeps everything warm; the
    // deferred stop re-checks isOverlayDisplaying() before acting.
    void scheduleIdleQuiesce();

    // Refresh zone selector and overlay windows that are currently visible.
    // Skips hidden windows - showZoneSelector()/show() refresh before showing.
    void refreshVisibleWindows();

    // Connect to a PhosphorZones::Layout's layoutModified signal so live edits from the editor
    // (shader id/params, zone geometry, appearance) propagate to the live overlay
    // without waiting for a layout switch or daemon restart.
    void observeLayoutForLiveEdits(PhosphorZones::Layout* layout);

    // Stop observing a layout (e.g. because the layout registry just removed it).
    // Disconnects the per-layout layoutModified signal and erases the entry
    // from m_observedLayouts. Idempotent - calling for an unobserved layout
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

    void updateLabelsTextureForWindow(QQuickItem* slot, const QVariantList& patched, QScreen* screen,
                                      PhosphorZones::Layout* screenLayout);
    QVariantList buildZonesList(QScreen* screen) const;
    QVariantList buildZonesList(const QString& screenId, QScreen* physScreen) const;
    /// Build the popup / picker layouts list for @p screenId.
    ///
    /// @p autotilePreviewCanvas - when non-empty, autotile algorithm
    ///   previews are computed against this rect rather than the default
    ///   square canvas. Pass the target screen's available geometry size
    ///   when the consumer is per-screen (layout picker, OSD) so
    ///   aspect-sensitive algorithms (BSP, fibonacci, …) preview along
    ///   the same split axis the live tiler will render. Empty (default)
    ///   keeps the legacy square-canvas behaviour for screen-agnostic
    ///   consumers.
    QVariantList buildLayoutsList(const QString& screenId = QString(), QSize autotilePreviewCanvas = {}) const;
    /// Per-screen layout-family filter used for the zone selector.
    /// `manual` enables PhosphorZones layout entries; `autotile` enables
    /// algorithm previews. Both default-true is "show everything"; the
    /// resolver narrows to a single family when the screen has an
    /// explicit assignment.
    struct LayoutIncludeFlags
    {
        bool manual = true;
        bool autotile = true;
    };
    /// Resolve the per-screen include filter. buildLayoutsList (the popup
    /// model) and visibleLayoutCount (used by isNearTriggerEdge to size
    /// the keep-visible bar) both go through here so the trigger geometry
    /// matches the rendered popup row count.
    LayoutIncludeFlags resolvePerScreenLayoutInclude(const QString& screenId) const;
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

    /// True when the snapping overlay must NOT show on @p screenId for the current
    /// desktop/activity: either the context is on a disable list, OR its default
    /// layout assignment is suppressed (the global "don't assign by default"
    /// setting, or a per-context rule) and nothing is explicitly assigned. Folds
    /// the two gates so every overlay / selector activation site treats a
    /// suppressed context exactly like a disabled one.
    bool isSnappingContextInactive(const QString& screenId) const;

    // PhosphorLayer infrastructure - owns the wlr-layer-shell binding, screen
    // enumeration, and Surface factory for all overlay-style windows. Members
    // ordered so factory is destroyed before provider/transport (factory keeps
    // raw pointers to the other two).
    std::unique_ptr<PhosphorLayer::IScreenProvider> m_screenProvider;
    std::unique_ptr<PhosphorLayer::ILayerShellTransport> m_transport;
    /// Raw pointer to Daemon-owned registry. Valid for the lifetime of
    /// this OverlayService because m_animationShaderRegistry is declared
    /// before m_overlayService in daemon.h - reverse destruction order
    /// guarantees the registry outlives this service.
    PhosphorAnimationShaders::AnimationShaderRegistry* m_animShaderRegistry = nullptr;

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

    // ShellHost owns the per-screen layer-shell shell state (surfaces,
    // windows, slot Items) and the sticky creation-failure spam-guard.
    // The daemon's PerScreenOverlayState below caches a borrowed
    // ShellState* pointer that points into m_shellHost's owning map.
    //
    // ~OverlayService explicitly resets m_shellHost AFTER draining
    // m_screenStates and BEFORE implicit member destruction, so the lib
    // dtor's PreDestroyCallback re-fire (for any entry the explicit
    // drain missed) runs while m_screenStates and friends are still
    // alive. The decl order below (m_screenStates before m_shellHost)
    // also makes reverse-destruction order safe - m_shellHost
    // (declared later) destroys FIRST, while m_screenStates is still
    // alive - even if a future change removes the explicit reset.
    QHash<QString, PerScreenOverlayState> m_screenStates;
    std::unique_ptr<PhosphorOverlay::ShellHost> m_shellHost;

    QPointer<PhosphorZones::Layout> m_layout;
    QPointer<ISettings> m_settings;
    PhosphorZones::IZoneLayoutRegistry* m_layoutManager =
        nullptr; ///< Borrowed; nullable (setLayoutManager(nullptr) detaches)
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives service
    ShaderRegistry* m_shaderRegistry = nullptr; ///< Borrowed; outlives service
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr; ///< Borrowed; outlives service (optional)
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    QList<QPointer<PhosphorZones::Layout>> m_observedLayouts; ///< Layouts we watch for live edits

    // Precise disconnect handles for signal sources whose slots are lambdas
    // (disconnect(src, sig, this, nullptr) would sever ALL slots matching -
    // safe today but trap-prone if a second connection is ever added).
    QMetaObject::Connection m_shadersChangedConnection;
    // Debounce layoutModified → refreshVisibleWindows. layoutModified fires on
    // every Q_PROPERTY change (e.g. per-frame during a zone drag), so
    // coalescing prevents redundant rebuilds of zone variant lists + label
    // texture re-uploads. Guarded by the single-shot timer pattern: first
    // fire starts a timer; subsequent fires before the timer elapses do
    // nothing; the timer callback runs refreshVisibleWindows once.
    bool m_refreshCoalescePending = false;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based); global
                                     // fallback for currentVirtualDesktopForScreen
                                     // when no layout registry is wired (#648).
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

    // Layout-OSD and Navigation-OSD content share a single per-screen
    // PerScreenOverlayState::shell->shellWindow() plus per-slot QQuickItems
    // (PassiveOverlayShell.qml) post-Phase-2 unification. No separate
    // per-mode window pointers.

    // Shader preview overlay (editor dialog)
    QPointer<PhosphorLayer::Surface> m_shaderPreviewSurface;
    QQuickWindow* m_shaderPreviewWindow = nullptr;
    QPointer<QScreen> m_shaderPreviewScreen;
    QString m_shaderPreviewShaderId; // Shader ID for param translation in updateShaderPreview
    QString m_shaderPreviewScreenId; // Virtual screen ID from showShaderPreview (avoids re-resolving from QScreen*)

    // Snap Assist (window picker after snapping). Post-shell-migration
    // snap-assist is an Item slot inside the per-screen passive shell;
    // these track *which* screen's shell currently shows it (singleton
    // across all screens) and whether it's logically visible.
    QString m_snapAssistScreenId;
    bool m_snapAssistVisible = false;
    QVariantList m_snapAssistCandidates; // Mutable copy for async thumbnail updates
    // Bounded LRU cache + QML image provider. Constructed eagerly in the
    // OverlayService ctor (before the SurfaceManager) so @ref m_thumbnailProvider
    // is non-null for the daemon's entire lifetime - the previous lazy
    // pattern left a window between OverlayService construction and first
    // surface creation where setSnapAssistThumbnail silently dropped. The
    // owned unique_ptr releases ownership to the QQmlEngine the moment the
    // engine is created (engineConfigurator below); after that the engine
    // owns the provider and outlives every QML reference into it.
    //
    // Lifetime invariant - single-threaded teardown with no event-loop
    // pumping during the destructor window. Concretely:
    //
    //   1. ~QQmlEngine body destroys the registered image providers -
    //      the underlying SnapAssistThumbnailProvider object is freed
    //      here.
    //   2. ~QObject body emits `destroyed`; the lambda installed in
    //      engineConfigurator fires and sets m_thumbnailProvider to
    //      nullptr.
    //
    // Between (1) and (2) the borrowed raw pointer is briefly dangling
    // - the lambda runs *after* the provider has been deleted because
    // C++ destruction order is derived-then-base. Safety in this window
    // rests on two independent facts, not on ordering:
    //
    //   - ~QQmlEngine does not pump the main-thread event loop, so no
    //     posted D-Bus dispatch / QObject event can run on this thread
    //     during the window. Same-thread readers physically cannot
    //     witness the dangling pointer.
    //   - Cross-thread reads go through QML's image-loader path
    //     (requestImage); Qt drains in-flight image requests as part of
    //     the engine teardown that destroys the providers, so no
    //     worker-thread reader is in flight either.
    //
    // m_thumbnailProvider is std::atomic so the null-out in the
    // ~QQmlEngine destroyed lambda is visible to any concurrent reader
    // (image-loader worker threads). This makes the safety structural
    // rather than relying on the single-threaded teardown invariant.
    std::unique_ptr<SnapAssistThumbnailProvider> m_thumbnailProviderOwned;
    std::atomic<SnapAssistThumbnailProvider*> m_thumbnailProvider{nullptr};
    // Zero-copy GPU thumbnail provider (PLASMAZONES_DMABUF_THUMBNAILS). Same
    // ownership pattern as m_thumbnailProvider: constructed eagerly, ownership
    // released to the QQmlEngine in engineConfigurator, borrowed atomic pointer
    // nulled when the engine tears it down. Registered under the separate
    // DmabufTextureProvider::ProviderId (Texture-type) image scheme. The full
    // delete→null teardown-window safety analysis above (no-event-loop-pumping
    // during ~QQmlEngine + std::atomic null-out) applies identically here.
    std::unique_ptr<DmabufTextureProvider> m_dmabufTextureProviderOwned;
    std::atomic<DmabufTextureProvider*> m_dmabufTextureProvider{nullptr};
    // Single-shot idle-grace timer: started on every hideSnapAssist and
    // stopped on showSnapAssist. If snap-assist stays dismissed long enough
    // for it to fire, it clears the thumbnail cache so its bounded (~6 MB
    // worst-case) pixel buffers don't sit resident for the rest of the
    // session. Rapid dismiss/re-show continuations restart it before it
    // fires, keeping the warm cache. Lazily created (parented to this).
    QTimer* m_snapAssistCacheTrimTimer = nullptr;
    // Layout Picker (interactive layout browser). Post-shell-migration
    // the picker is an Item slot inside the per-screen passive shell;
    // these track which screen's shell currently shows it (singleton
    // across all screens) and whether it's logically visible.
    QString m_layoutPickerScreenId;
    bool m_layoutPickerVisible = false;

    bool m_screenAddedConnected = false; // Guard for screenAdded connection (lambdas can't use UniqueConnection)
    /// Surfaces currently in the prime cycle (between primeSurfaceRender-
    /// Pipeline's show and its frameSwapped-driven hide). User-triggered
    /// show paths must call cancelSurfacePrime before their own
    /// Surface::show() so the queued prime-hide doesn't race the user's
    /// content off the screen.
    QSet<PhosphorLayer::Surface*> m_primingSurfaces;
    /// Per-surface frameSwapped Connection (only for the window-armed
    /// stage of priming; the warm-pending stage doesn't use it).
    /// cancelSurfacePrime explicitly disconnects the entry here so the
    /// queued hide-on-first-paint lambda doesn't fire after a user-show
    /// - without this, the connection lives until next paint and leaks
    /// one slot per prime cycle for the surface's lifetime under rapid
    /// show/hide toggling.
    QHash<PhosphorLayer::Surface*, QMetaObject::Connection> m_primingFrameConnections;
    /// Per-surface destroyed-signal Connection. Replaces an earlier
    /// per-surface Qt dynamic-property gate which
    /// leaked across OverlayService instances (test fixtures, daemon
    /// hot-restart) - a fresh service that re-encountered the same
    /// Surface* would skip wiring its own cleanup. Per-instance
    /// tracking ensures each service installs exactly one slot per
    /// surface; the surface's own destruction auto-disconnects via
    /// `this`-receiver-context, and we drop the entry from the slot.
    QHash<PhosphorLayer::Surface*, QMetaObject::Connection> m_primingDestroyedConnections;
    // "Notifications have been pre-warmed" flag. With LayoutOsd and
    // NavigationOsd unified onto a single per-screen passive overlay
    // shell, this single flag gates whether the screenAdded hot-plug
    // lambda auto-creates the shell for a newly-attached screen.
    // Set by warmUpNotifications().
    bool m_notificationsWarmed = false;

    // Keep-alive is managed by m_surfaceManager (PhosphorSurfaces::SurfaceManager).

    // Remembered so ~OverlayService can disconnect the D-Bus PrepareForSleep
    // subscription explicitly rather than relying on QDBusConnection's
    // internal receiver-destroyed detection (which works, but leaves a dead
    // entry in the connection's slot table for the rest of the session).
    bool m_prepareForSleepConnected = false;

    // Deduplicate navigation feedback (prevent duplicate OSDs from Qt signal + D-Bus signal)
    QString m_lastNavigationActionKey; // "action:reason" composite key
    QString m_lastNavigationScreenId;
    QElapsedTimer m_lastNavigationTime;

    void createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom);
    void destroyZoneSelectorWindow(const QString& screenId);
    void updateZoneSelectorWindow(const QString& screenId);
    void showLayoutOsdImpl(PhosphorZones::Layout* layout, const QString& screenId, bool locked);
    /// Tear down the per-screen passive overlay shell. Deletes the
    /// shell PhosphorLayer::Surface (and its QQuickWindow + every slot
    /// QQuickItem owned by it). Called from
    /// `destroyAllWindowsForPhysicalScreen` on screen hot-plug cleanup.
    void destroyPassiveShell(const QString& screenId);

    /// Lazily create the per-screen PassiveOverlayShell + return the
    /// state entry (or nullptr if creation failed). The shell is the
    /// unified host for kbd-None per-content slots (currently OSD;
    /// subsequent migration steps fold zone-selector / main-overlay /
    /// snap-assist / picker in). Wires the shell's QML signals
    /// (osdDismissRequested, …) to the C++ animator-driven slot-hide
    /// path.
    PerScreenOverlayState* ensurePassiveShellFor(const QString& effectiveId, QScreen* physScreen);

    /// PostCreate hook registered with the ShellHost. Caches the per-content
    /// slot Items by their QML object names (osdSlotItem / snapAssistSlotItem
    /// / ...), wires up QML signal handlers, then primes the rendering
    /// pipeline. Runs from inside ShellHost::ensureShell immediately after
    /// the surface and window are recorded in ShellState. The QML object
    /// names are PZ-shell-specific so this method, not the library, owns
    /// the lookup grammar.
    void wirePassiveShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState);

    /// PreDestroy hook registered with the ShellHost. Nulls every
    /// PZ-content sentinel on the daemon's PerScreenOverlayState so a
    /// stale signal handler firing during teardown doesn't reach into a
    /// half-destroyed scene graph. Runs from inside ShellHost::destroyShell
    /// before the library schedules the shell surface for deletion.
    void unwirePassiveShellSlots(const QString& screenId);

    /// Slot-hide animation completion - flips the OSD slot Item's
    /// `visible` to false once the SurfaceAnimator's hide leg settles,
    /// so a subsequent show with no content state writes doesn't paint a
    /// stale prior-frame opaque card before the next show's beginShow
    /// reasserts opacity = 0 → 1. Called from the lambda passed to
    /// `beginHide`. Per-screen-id keying tolerates surface destruction
    /// during the hide leg.
    void onOsdSlotHideCompleted(const QString& effectiveId);

    /// Shared property-push for layout-OSD content. Used by both
    /// showLayoutOsdImpl (PhosphorZones::Layout* path) and the
    /// showLayoutOsd(QString,...) overload (autotile / pre-built-zones
    /// path). The struct lets the two callers diverge only on the values
    /// they compute, not on the property-write sequence.
    struct LayoutOsdContentParams
    {
        QString id; ///< layoutId - UUID for manual layouts, "autotile:..." for algorithms
        QString name; ///< layoutName as shown in the OSD label
        QVariantList zones; ///< pre-built zone variant list (empty for locked-with-no-zones)
        int category = 0; ///< PhosphorZones::LayoutCategory enum value
        bool autoAssign = false; ///< per-layout autoAssign flag (raw, pre-OR with global)
        bool globalAutoAssign = false; ///< master "auto-assign for all layouts" toggle (#370)
        bool locked = false; ///< draws lock badge + " (Locked)" suffix
        qreal screenAspectRatio = 16.0 / 9.0;
        QString aspectRatioClass = QStringLiteral("any");
        bool showMasterDot = false;
        bool producesOverlappingZones = false;
        QString zoneNumberDisplay = QStringLiteral("all");
        int masterCount = 1;
    };
    void pushLayoutOsdContent(QObject* osdSlot, const LayoutOsdContentParams& params);

    void destroyIfTypeMismatch(const QString& screenId);
    void createShaderPreviewWindow(QScreen* screen, const QString& screenId = QString());
    void destroyShaderPreviewWindow();

    /// Destroy all overlay, OSD, zone selector, snap assist, and layout picker windows
    /// backed by the given physical screen. Used by both virtualScreensChanged and handleScreenRemoved.
    void destroyAllWindowsForPhysicalScreen(QScreen* screen);

    /// Animator-driven slot-hide completion for snap-assist. Mirrors
    /// onOsdSlotHideCompleted: flips slot.visible=false + clears
    /// `loaded` so a subsequent show toggles it false→true freshly.
    void onSnapAssistSlotHideCompleted(const QString& effectiveId);

    /// Animator-driven slot-hide completion for layout-picker. Mirrors
    /// onSnapAssistSlotHideCompleted pattern.
    void onLayoutPickerSlotHideCompleted(const QString& effectiveId);

    /// Animator-driven slot-hide completion for zone-selector.
    void onZoneSelectorSlotHideCompleted(const QString& effectiveId);

    /// Hide the zone-selector slot on a single screen via the animator,
    /// so a fading-out selector doesn't stack behind an incoming
    /// OSD/popup. Mirrors hideZoneSelector but scoped to one screen and
    /// does NOT flip the global m_zoneSelectorVisible flag - the
    /// selector stays "logically visible" from the daemon's POV (the
    /// drag is still active), it's just hidden ON THIS SCREEN to make
    /// room for a sibling overlay.
    void hideZoneSelectorSlotOnScreen(const QString& effectiveId);

    /// Re-show the zone-selector slot on a single screen via the
    /// animator. Inverse of hideZoneSelectorSlotOnScreen - used by the
    /// snap-assist / picker dismiss paths to restore the selector
    /// after a temporary slot-hide. Idempotent: bails when the slot is
    /// already visible.
    void showZoneSelectorSlotOnScreen(const QString& effectiveId, QScreen* physScreen, const QRect& targetGeom);

    /// Conditionally restore the zone-selector slot on @p effectiveId
    /// after a sibling slot finished hiding. Re-shows iff the drag is
    /// still logically active (@c m_zoneSelectorVisible) AND the screen
    /// retains its captured (physScreen, geometry) state. Centralizes
    /// the symmetric restore pattern used by every slot-hide completion
    /// (onOsdSlotHideCompleted, onSnapAssistSlotHideCompleted,
    /// onLayoutPickerSlotHideCompleted).
    void restoreZoneSelectorAfterHide(const QString& effectiveId);

    /// Drive the per-screen shell wl_surface map state from slot
    /// visibility. Shell uses keepMappedOnHide=true; Surface::show()
    /// /hide() flip Qt::WindowTransparentForInput. The flip only
    /// happens through Surface's state machine, not through slot-level
    /// animator hides - without this helper the shell never re-enters
    /// Hidden after first show, and the input region eats every click
    /// for the daemon's lifetime.
    ///
    /// Called after every slot setVisible toggle. Idempotent:
    /// isLogicallyShown() guards re-show; the all-slots-hidden
    /// predicate guards the hide.
    void syncPassiveShellSurfaceState(const QString& effectiveId);

    /// Run `syncPassiveShellSurfaceState` for every per-screen state that
    /// owns @p surface - used after a slot setVisible(true) + beginShow
    /// pair when the call site doesn't already have the matching effective
    /// screen id in scope. The body lookup walks the small state map (≤ a
    /// handful of screens in practice) and forwards the per-screen sync.
    /// Without this, slot-show paths leave the input region in whatever
    /// state Surface::show() set it to (cleared = grabbing) until the
    /// matching slot-hide-completion handler eventually flips it back -
    /// reading as "OSD eats clicks for its full lifetime" to the user.
    void syncPassiveShellSurfaceStateForSurface(PhosphorLayer::Surface* surface);

    /**
     * @brief Construct the SurfaceAnimator and register per-Role configs.
     *
     * Phase 5 of the phosphor-animation roadmap: a single library-driven
     * animator drives show/hide across every overlay (LayoutOsd,
     * NavigationOsd, LayoutPicker, ZoneSelector, SnapAssist) using
     * Profile-resolved curves shared with in-window animations. Called
     * exactly once from the ctor; the animator's lifetime is tied to
     * `*this`.
     *
     * @param profileRegistry Borrowed; threaded into the SurfaceAnimator's
     *                        constructor. Must outlive the animator.
     */
    void setupSurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& profileRegistry);

    /**
     * @brief Re-register every per-role config with the supplied shader profile
     *        tree. Called from setSettings (initial wire-up after settings
     *        become available) and from the @c shaderProfileTreeChanged signal
     *        (live reload when the user edits the tree).
     *
     * @c registerConfigForRole only affects subsequent Surface::show()/hide()
     * lookups - surfaces mid-animation keep the config they bound at
     * beginShow/beginHide. That mirrors motion-tree live-reload semantics.
     *
     * A default-constructed tree (empty baseline + no overrides) resolves each
     * path to its built-in default shader (via @c resolveShaderWithDefault):
     * overlay show/hide paths to "fade", paths without a default to empty
     * (motion-only). Used during the initial @c setupSurfaceAnimator pass
     * before @c m_settings is wired; the live tree later applies user
     * overrides on top.
     */
    void applyShaderProfilesToAnimator(const PhosphorAnimationShaders::ShaderProfileTree& tree);

    /** Update a candidate's thumbnail in m_snapAssistCandidates and push to QML.
     *  @return true iff the image was inserted into the bounded LRU cache.
     *          False if the provider was torn down (engine destroyed) or the
     *          image was null after format conversion. */
    bool updateSnapAssistCandidateThumbnail(const QString& compositorHandle, QImage image);

    /** Push a resolved thumbnail image:// URL into the live snap-assist
     *  candidate list (and QML) for @p compositorHandle. Shared tail of the
     *  raw-pixel and dma-buf thumbnail paths. @return true (the URL is already
     *  stored in its provider regardless of whether snap-assist is visible). */
    bool applyCandidateThumbnailUrl(const QString& compositorHandle, const QString& providerUrl);

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
    bool prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface, QQuickItem*& outOsdSlot,
                                QScreen*& outPhysScreen, QRect& screenGeom, qreal& aspectRatio,
                                const QString& screenId = QString());

    /**
     * @brief Parameters for @ref createLayerSurface, kept as a named-member
     *        aggregate so call sites read top-to-bottom rather than relying
     *        on positional arg ordering. Required fields up top, optional
     *        below; Qt6 designated-init form is `LayerSurfaceParams{.qmlUrl=...}`.
     */
    struct LayerSurfaceParams
    {
        // Required.
        QUrl qmlUrl = {}; ///< QML file (Window-rooted - PZ's overlay QML convention)
        QScreen* screen = nullptr; ///< target screen (physical; virtual-screen positioning is the caller's job)
        PhosphorLayer::Role role = {}; ///< protocol-level preset (see phosphor_roles.h)
        const char* windowType = ""; ///< debug/telemetry label

        // Optional - explicit `= {}` suppresses GCC's
        // -Wmissing-field-initializers warning on designated-init call sites
        // that omit these. (For `QUrl` / `Role` above the same is true; we
        // just want one consistent style across the struct.)
        QVariantMap windowProperties = {}; ///< Applied as dynamic properties before QML loads.
        std::optional<PhosphorLayer::Anchors> anchorsOverride =
            std::nullopt; ///< Overrides the role's anchors (virtual-screen positioning).
        std::optional<QMargins> marginsOverride =
            std::nullopt; ///< Overrides the role's margins (virtual-screen positioning).
        bool keepMappedOnHide = false; ///< See SurfaceConfig::keepMappedOnHide.
        /// Initial swapchain size committed during warm-up. Empty (default)
        /// → screen geometry, which guarantees a non-zero buffer for every
        /// anchor configuration but allocates a full-screen Vulkan swapchain
        /// (~25 MB at 4K × triple buffer on NVIDIA). Non-empty → a swapchain
        /// proportional to the passed size. The eventual visible size is
        /// still driven by per-show setWidth/setHeight; this only governs
        /// the warm-up commit. Forwarded verbatim to
        /// SurfaceConfig::initialSize, including the empty-as-unset sentinel.
        QSize initialSize = {};
    };

    /**
     * @brief Create a PhosphorLayer::Surface for a layer-shell-backed overlay window.
     *
     * Every overlay, OSD, zone selector, snap assist, layout picker, and shader
     * preview in OverlayService goes through this single helper. Returns a surface
     * that has been warmed up (window created, QML loaded, transport attached) but
     * is hidden - callers decide when to call @c surface->show() or keep it warm
     * for pre-warmed OSDs.
     *
     * @return the surface on success; nullptr on failure (warnings logged internally).
     */
    PhosphorLayer::Surface* createLayerSurface(LayerSurfaceParams params);

    /**
     * @brief Create a warmed OSD-style surface and wire its dismiss signal.
     *
     * Common pattern for ensurePassiveShellFor (and the LayoutPicker
     * surface in snapassist.cpp): (1) caller builds a per-instance
     * scope-prefixed Role via @ref PhosphorRoles::makePerInstanceRole,
     * (2) this helper calls createLayerSurface with keepMappedOnHide=true,
     * (3) string-connects the QML-side `dismissRequested()` signal to
     * `Surface::hide()` so the auto-dismiss timer (or backdrop click for
     * the picker) can drive the library animator without a C++ slot in
     * the loop.
     *
     * Returns the warmed Surface on success; nullptr on failure (warning
     * logged inside createLayerSurface). Caller installs the surface +
     * window pointers into PerScreenOverlayState.
     *
     * @param role           Fully-formed per-instance role (use
     *                       PhosphorRoles::makePerInstanceRole to build).
     * @param qmlUrl         QML file to load.
     * @param physScreen     Target physical screen.
     * @param windowType     Debug/telemetry label.
     * @param screenId       Effective screen id (physical or virtual). Used
     *                       to size the warm-up surface to the right screen
     *                       rect and to pick virtual-screen-aware anchors +
     *                       margins. Optional for callers that don't have
     *                       an id yet - they fall back to physScreen's full
     *                       geometry with AnchorAll.
     */
    PhosphorLayer::Surface* createWarmedOsdSurface(const PhosphorLayer::Role& role, const QUrl& qmlUrl,
                                                   QScreen* physScreen, const char* windowType,
                                                   const QString& screenId = QString());

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
    // Single-shot grace timer that quiesces the render loop + CAVA after the
    // overlay goes idle (see scheduleIdleQuiesce). Cancelled if the overlay
    // displays again within the grace window.
    QTimer* m_idleQuiesceTimer = nullptr;
    // True while the overlay is in the warm-idled state (blanked + _idled, but
    // its QQuickWindows kept alive). Distinct from m_visible, which stays true
    // across drags because the windows are never torn down.
    bool m_overlayIdled = false;

    // PhosphorZones::Zone data version for shader synchronization
    int m_zoneDataVersion = 0;

    // PhosphorZones::Layout filter: which types to include in zone picker (set by Daemon)
    bool m_includeManualLayouts = true;
    bool m_includeAutotileLayouts = false;

    // Screens excluded from overlay display (autotile-managed screens)
    QSet<QString> m_excludedScreens;
};

} // namespace PlasmaZones
