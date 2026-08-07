// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION: this header is over 1200 lines, past the 1150 hard
// ceiling.
// The exception was granted in the same pull request that carried the file
// past the ceiling (the scroll tab strip), so it is a live decision rather
// than settled precedent, and it is recorded here for that reason.
//
// The case for it: OverlayService is the single façade every overlay surface
// goes through — zone overlay, selector, snap assist, OSD, cheatsheet, the
// scroll tab strip and the scrolling drop indicator — so its members are the
// per-screen state and per-role
// wiring those surfaces share. The implementation is already split by surface
// across daemon/overlayservice/*.cpp; splitting the class DECLARATION would
// scatter the per-screen ownership and teardown-order contract the member
// ordering encodes, exactly as documented on daemon.h.

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
#include <functional>
#include <memory>
#include <optional>

#include <PhosphorLayer/Role.h>
#include <PhosphorOverlay/ShellState.h>

#include "core/interfaces/interfaces.h"
#include "overlayservice_types.h"
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
struct ContextOverlayOverride;
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

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PlasmaZones {
class ShaderRegistry;
class SnapAssistThumbnailProvider;
class DmabufTextureProvider;
}
namespace PhosphorScreens {
class ScreenManager;
}
namespace PhosphorContext {
class IContextResolver;
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
    // No zoneSelectorVisible property: nothing reads one. The selector is
    // never exposed to QML as a bound property, so the property only ever
    // wrapped IOverlayService::isZoneSelectorVisible() and its
    // zoneSelectorVisibilityChanged notification, both of which remain and
    // are the supported way to observe it.

public:
    /// Per-screen overlay state (window pointers, physical screen references,
    /// geometry). Defined in overlayservice_types.h; aliased here so existing
    /// OverlayService::PerScreenOverlayState references keep working.
    using PerScreenOverlayState = PlasmaZones::PerScreenOverlayState;

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
    /// Borrowed Daemon-owned surface-shader registry, used to resolve the OSD's
    /// decoration pack (Stage d). Same lifetime contract as the animation
    /// registry: the registry is declared AFTER m_overlayService in daemon.h, so
    /// Daemon::stop() nulls this borrow (setSurfaceShaderRegistry(nullptr))
    /// BEFORE resetting the registry — the explicit teardown, not declaration
    /// order, is what prevents a dangling pointer during shutdown.
    void setSurfaceShaderRegistry(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry);
    void updateGeometries() override;

    // PhosphorZones::Zone highlighting for overlay display (IOverlayService interface)
    void highlightZone(const QString& zoneId) override;
    void highlightZones(const QStringList& zoneIds) override;
    void clearHighlight() override;

    // Mid-drag idle / resume - see IOverlayService for rationale.
    void setIdleForDragPause() override;
    void forgetCurrentScreen() override;
    void refreshFromIdle() override;

    // Additional methods
    void setLayout(PhosphorZones::Layout* layout);
    PhosphorZones::Layout* layout() const
    {
        return m_layout;
    }

    void setSettings(ISettings* settings);
    void setLayoutManager(PhosphorZones::IZoneLayoutRegistry* layoutManager);
    /// Late-bound daemon context resolver. The daemon creates it after this
    /// service and clears the borrow before resolver teardown.
    void setContextResolver(PhosphorContext::IContextResolver* resolver);

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

    /// Scroll-mode zone model for the navigation OSD: returns one entry per
    /// VISIBLE strip tile ({id: windowId, zoneNumber: the tile's 1-based slot
    /// in strip order}) for a scrolling screen, empty otherwise. A window with
    /// no visible tile carries no entry at all, so the list is not a census of
    /// the strip's windows. Daemon-injected
    /// (the overlay stays engine-agnostic); when it answers non-empty, the
    /// navigation OSD uses it in place of the layout's zone list so the
    /// "Zone %1" copy resolves and no snap layout is required on a
    /// scrolling screen. Same clear-before-destroy contract as the other
    /// injected closures.
    using ScrollZonesProvider = std::function<QVariantList(const QString& screenId)>;
    void setScrollZonesProvider(ScrollZonesProvider provider)
    {
        m_scrollZonesProvider = std::move(provider);
    }

    /// Whether the engine owning a screen consumes user-selectable layouts
    /// (IPlacementEngine::providesLayouts). Daemon-injected so the overlay
    /// stays engine-agnostic; resolvePerScreenLayoutInclude answers "no
    /// layouts at all" for a screen whose engine returns false (scrolling),
    /// which empties the layout picker's list so its show bails. (The
    /// drag-time popup is separately suppressed on engine-owned screens by
    /// WindowDragAdaptor's dragMoved gate; for it this is defence in
    /// depth.) Unset falls back to the assignment-based resolution. Same
    /// clear-before-destroy contract as the other injected closures.
    using LayoutsProvidedResolver = std::function<bool(const QString& screenId)>;
    void setLayoutsProvidedResolver(LayoutsProvidedResolver resolver)
    {
        m_layoutsProvidedResolver = std::move(resolver);
    }
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
    /// The card always wears the failure glyph "dialog-cancel". Both callers
    /// (showContextDisabledOsd and showNotAssignedOsd) explain why a
    /// requested change had no effect, which is what the glyph says. A
    /// positive announcement does not belong on this card at all — the
    /// scrolling mode switch, which briefly did reuse it, now renders its own
    /// strip preview.
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
     * Idempotent for the SAME screen set: the m_notificationsWarmed latch
     * and the per-screen window guard make a repeat call a no-op for screens
     * already warmed. A screen that appeared since the last call is still
     * warmed on the next one, which is the point of calling it again after a
     * hotplug rather than only once at start.
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
     * back in State::Hidden. The post-hide mapped guarantee is
     * conditional: SurfaceConfig::keepMappedOnHide is effects-gated
     * (createWarmedOsdSurface), so the wl_surface stays alive across
     * the prime's hide only while shaders or animations are enabled;
     * with both off the prime still warms the pipelines but the
     * wl_surface unmaps after the hide.
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

    // Shortcut cheatsheet overlay (display-only shortcut reference card).
    // Daemon-mediated push: the caller resolves the catalog + current mode
    // (per-screen tri-state) and hands them in; the service owns only slot
    // lifecycle. `currentMode` is "snapping" | "autotile" | "scrolling";
    // `autotileAvailable` / `scrollingAvailable` mirror the global feature
    // gates (when false the matching group hides regardless of mode — the
    // mode string alone lags the engine teardown on a disable).
    // `layoutsAvailable` is the bound screen's engine capability
    // (IPlacementEngine::providesLayouts): when false the catalog rows
    // tagged "layouts" hide, because those shortcuts answer with a
    // "not available" OSD on that screen.
    void showCheatsheet(const QString& screenId, const QVariantList& model, const QString& currentMode,
                        bool autotileAvailable, bool scrollingAvailable, bool layoutsAvailable);
    void hideCheatsheet() override;
    bool isCheatsheetVisible() const override;
    /// Re-push model/mode into an already-visible cheatsheet (live refilter
    /// on mode switch or rebind). No-op when hidden — the next show
    /// re-resolves everything anyway.
    void refreshCheatsheet(const QVariantList& model, const QString& currentMode, bool autotileAvailable,
                           bool scrollingAvailable, bool layoutsAvailable);
    /// Screen the visible cheatsheet is bound to; empty when hidden.
    QString cheatsheetScreenId() const;

    /// Tab indicators for tabbed scrolling columns on @p screenId (per
    /// screen, NOT a singleton). @p strips is a list of maps with x / y /
    /// width / height (absolute px, converted to shell coordinates here),
    /// position, and tabs ({windowId, title, active, urgent, colors?} list);
    /// empty hides the screen's indicators. `windowId` is load-bearing — it is
    /// what a tab click relays back to focus that window.
    ///
    /// Each tab is a click target; the surface stays click-through outside the
    /// indicator rects via the per-screen input region built here.
    ///
    /// The rects are always the POST-scroll ones and are written plainly, with
    /// no motion state alongside them. The indicators have a wl_surface to
    /// themselves and the compositor slides it by the scrolling strip's view
    /// offset, so they ride a scroll the same way the columns do.
    void updateScrollTabStrips(const QString& screenId, const QVariantList& strips);

    /// Drop-target indicator for a scrolling drag re-insert on @p screenId
    /// (per screen, NOT a singleton — a drag can cross screens). @p rect is
    /// the absolute-px slot the dragged window would land in, converted to
    /// shell coordinates here; an invalid or empty rect hides the indicator.
    ///
    /// Purely display: unlike the tab strips this installs NO input region,
    /// because it is painted underneath a cursor that is mid-drag and taking
    /// input there would break the drag it exists to describe.
    ///
    /// Scrolling needs a drawn indicator where autotile needs none. Autotile's
    /// feedback IS its live restructure, but the scroll engine detaches once at
    /// drag start and applies structure at drop, precisely because restructuring
    /// live slid the strip out from under a stationary cursor. So the target has
    /// to be painted rather than enacted.
    void updateScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate) override;

    /// Per-context PAINT overrides for @p screenId's tab indicator, resolved
    /// from the SetTabIndicator* context rules and layered over the config
    /// values when the indicator is drawn. Keyed by the overlay SLOT's own
    /// property names (tabStyle, gapsBetweenTabs, cornerRadius, activeColor,
    /// inactiveColor, urgentColor); an absent key falls through to config.
    /// Those names must match ScrollTabShell's scrollTabsSlot exactly —
    /// setProperty on an undeclared name silently creates a dead dynamic
    /// property instead of failing.
    ///
    /// Only the paint half arrives here. The indicator's GEOMETRY overrides go
    /// to the scrolling engine instead, because they change resolved rects and
    /// the engine has to size the column around them.
    ///
    /// An empty map clears the screen's overrides. Replays the cached strips
    /// so a rule change repaints a live indicator immediately, for the same
    /// reason the paint-settings hooks do.
    void setScrollTabIndicatorOverrides(const QString& screenId, const QVariantMap& overrides);

    /// Per-screen drop-indicator PAINT overrides from context rules, keyed by
    /// the QML property names the slot reads so the layering is one value()
    /// per property. An empty map clears the screen's overrides.
    ///
    /// Unlike the tab-strip twin this does NOT replay: the indicator only
    /// exists while a drag is in flight, and the next rect push during that
    /// drag re-reads the overrides. A rule change landing between drags is
    /// picked up by the drag that follows, which is the only time anyone can
    /// see it.
    void setScrollDropIndicatorOverrides(const QString& screenId, const QVariantMap& overrides);

    /// Per-DRAG drop-indicator colour overrides, resolved from the dragged
    /// window's rules at drag start. Outranks the per-context map above, which
    /// outranks the settings, which fall back to the theme. Cleared with an
    /// empty map when the drag ends; there is no screen key because exactly
    /// one window is dragged at a time.
    void setScrollDropIndicatorWindowOverrides(const QVariantMap& overrides) override;

    /// Re-push every screen's cached strip model through
    /// updateScrollTabStrips, whose own enabled check turns the replay into a
    /// show (toggle on) or an animated hide (toggle off). Needed because the
    /// engine's tabStripsChanged is change-latched and stays silent until the
    /// next structural change, so nothing else would repaint after the toggle
    /// moves. Called from the scrollingTabIndicatorEnabledChanged hook and from
    /// updateSettings, which covers the batch-setSettings case where the
    /// per-key signal never fires.
    void replayScrollTabStrips();

    /// Forwarders to the active picker slot's QML moveSelection /
    /// confirmSelection functions. Used by global-accel callbacks
    /// (registered by WindowDragAdaptor on picker show) since the
    /// shell is kbd-None and the picker content's QML Shortcuts can't
    /// fire. No-op when no picker is visible.
    void pickerMoveSelection(int dx, int dy);
    void pickerConfirmSelection();

    /// Re-push the context lock state to any open zone selector and the layout
    /// picker. Called when rules change at runtime (e.g. a `LockContext`
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
    /// the `overlayDisplayModeChanged` wiring for the equivalent global setting.
    void refreshOverlayPropertiesIfShown();

public Q_SLOTS:
    // hideLayoutOsd / hideNavigationOsd intentionally absent. Dismiss
    // path: QML auto-dismiss timer → loaded content's dismissRequested()
    // → shell window re-emits it as `osdDismissRequested` → wired by
    // wirePassiveShellSlots (shellhost_bridge.cpp) to
    // OverlayService::onOsdDismissRequested → ShellHost::hideSlot runs an
    // animator-driven slot-hide, then onOsdSlotHideCompleted flips
    // slot.visible=false and re-syncs surface state. The C++ slot never
    // destroys the QQuickWindow - that would re-introduce the blocking
    // ~QQuickWindow Vulkan teardown the warm-surface design avoids.
    // keepMappedOnHide is conditional (createWarmedOsdSurface): mapped
    // across hides only while shaders or animations are enabled; with
    // effects off the next syncSurfaceState unmaps the wl_surface.
    void hideLayoutPicker() override;

    // Shader error reporting from QML
    void onShaderError(const QString& errorLog);

private Q_SLOTS:
    void onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson);
    /// A tab of the scrolling indicator was clicked. Relays the canonical
    /// window id up as scrollTabActivated; the daemon focuses that window, so
    /// the tab it belongs to becomes the shown one through the ordinary focus
    /// path rather than by reaching into the strip from here.
    void onScrollTabActivated(const QString& windowId);
    void onLayoutPickerSelected(const QString& layoutId);
    /// Receiver for the unified passive shell's `osdDismissRequested`
    /// QML signal. Resolves the emitting shell window via `sender()`,
    /// finds the matching m_screenStates entry, and runs an animated
    /// slot-hide via SurfaceAnimator::beginHide on (shellSurface,
    /// osdSlotItem) - only the OSD slot Item's opacity animates to 0;
    /// the shell wl_surface itself stays mapped across the hide while
    /// shaders or animations are enabled (effects-gated keepMappedOnHide).
    void onOsdDismissRequested();

    /// Receiver for the shell's `snapAssistDismissRequested` QML signal
    /// (backdrop click + the Escape global accel routes to
    /// `hideSnapAssist` directly). Same animator-driven slot-hide
    /// pattern as onOsdDismissRequested.
    void onSnapAssistDismissRequested();

    /// Receiver for the shell's `layoutPickerDismissRequested` QML
    /// signal (backdrop click). Routes to hideLayoutPicker.
    void onLayoutPickerDismissRequested();

    /// Receiver for the shell's `cheatsheetDismissRequested` QML signal
    /// (backdrop click; the Escape ad-hoc grab routes to hideCheatsheet
    /// directly). Routes to hideCheatsheet.
    void onCheatsheetDismissRequested();

private:
    // Sync CAVA service state (start/stop/reconfigure) with current settings AND
    // whether the overlay is actually displaying content — CAVA runs only while
    // the overlay (un-idled) or shader preview is on screen.
    void syncCavaState();
    // Decoration slots (OSD / popups, across screens) that are visible AND carry
    // an audio-reactive surface pack right now. Drives CAVA gating + the
    // per-frame audio-spectrum push for daemon-surface decoration audio; empty
    // for the common case (no audio decoration), so it adds nothing then.
    QList<QQuickItem*> visibleAudioDecorationSlots() const;
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

    // Move a live overlay entry from oldKey to newKey, so the existing
    // QQuickWindow + VkSwapchainKHR is reused instead of torn down. SAME
    // FLAVOR ONLY: both keys virtual ("...:115107/vs:0" → "...:115107/vs:1")
    // or both bare physical. A virtual↔physical flip is refused, because it
    // changes the surface's anchor set and several compositors ignore
    // post-attach set_anchor — the caller falls back to destroy + recreate.
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
    /// Defined in overlayservice_types.h (hoisted with the other value
    /// types); aliased so existing OverlayService::LayoutIncludeFlags
    /// references keep working.
    using LayoutIncludeFlags = PlasmaZones::LayoutIncludeFlags;
    /// Resolve the per-screen include filter. buildLayoutsList (the popup
    /// model) and visibleLayoutCount (used by isNearTriggerEdge to size
    /// the keep-visible bar) both go through here so the trigger geometry
    /// matches the rendered popup row count. @p resolvedIdOut, when
    /// non-null, receives the id the decision was made for (connector
    /// names are normalized to identity ids) — callers must build their
    /// layout lists with that id so gate and rows agree.
    LayoutIncludeFlags resolvePerScreenLayoutInclude(const QString& screenId, QString* resolvedIdOut = nullptr) const;
    // overlayOverride is resolved once per screen by the caller (screen-invariant
    // across zones) and threaded in, rather than re-resolved per zone.
    QVariantMap zoneToVariantMap(PhosphorZones::Zone* zone, const QString& screenId, QScreen* physScreen,
                                 const QRect& overlayGeometry, PhosphorZones::Layout* layout,
                                 const PhosphorZones::ContextOverlayOverride& overlayOverride) const;

    /**
     * @brief Resolve the layout for a given screen with fallback chain
     *
     * Tries: per-screen assignment → activeLayout → m_layout
     */
    PhosphorZones::Layout* resolveScreenLayout(QScreen* screen) const;
    PhosphorZones::Layout* resolveScreenLayout(const QString& screenId) const;

    /// The id the layout picker / zone selector highlights as active on @p
    /// screenId. In autotile mode this is the resolved "autotile:<algorithm>"
    /// assignment id (matching the autotile cards); in snapping mode it is the
    /// resolved Layout's UUID (matching the manual cards). Snapping resolves
    /// through resolveScreenLayout() so its fallback chain is preserved, while
    /// autotile uses the assignment id directly because no Layout object backs
    /// an algorithm.
    QString activeLayoutIdForScreen(const QString& screenId) const;

    /// True when the snapping overlay must NOT show on @p screenId for the
    /// current desktop/activity. THREE conditions, any one of which is enough:
    /// the context is on a disable list; its default layout assignment is
    /// suppressed (the global "don't assign by default" setting, or a
    /// per-context rule) with nothing explicitly assigned; or the context
    /// resolves to an ENGINE mode. The third catches a bare or suppressed
    /// autotile context and a context-disabled scrolling one, neither of
    /// which is in the excluded-screens set, so without it the snap overlay
    /// surfaced on a screen the user had just switched away from snapping.
    /// Consumed by the OVERLAY activation sites; the zone SELECTOR is
    /// deliberately disabled-list-only (isSnappingContextDisabled) — a
    /// suppressed-default context still allows an explicit drag to pick a
    /// zone, so the selector must keep showing there.
    bool isSnappingContextInactive(const QString& screenId) const;
    bool isSnappingContextDisabled(const QString& screenId) const;

    // PhosphorLayer infrastructure - owns the wlr-layer-shell binding, screen
    // enumeration, and Surface factory for all overlay-style windows. Members
    // ordered so factory is destroyed before provider/transport (factory keeps
    // raw pointers to the other two).
    std::unique_ptr<PhosphorLayer::IScreenProvider> m_screenProvider;
    std::unique_ptr<PhosphorLayer::ILayerShellTransport> m_transport;
    /// Raw pointer to Daemon-owned registry. m_animationShaderRegistry is
    /// declared AFTER m_overlayService in daemon.h; the pointer stays valid
    /// because Daemon::stop() nulls this borrow (setAnimationShaderRegistry
    /// (nullptr)) before resetting the registry — explicit teardown, not
    /// declaration order, prevents dangling access during shutdown.
    PhosphorAnimationShaders::AnimationShaderRegistry* m_animShaderRegistry = nullptr;

    /// Borrowed Daemon-owned surface-shader registry (Stage d). Resolves the
    /// "osd" decoration pack's fragment shader + translated params for the OSD
    /// slot. Same teardown contract as m_animShaderRegistry: Daemon::stop()
    /// nulls this borrow before the registry is reset.
    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_surfaceShaderRegistry = nullptr;

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
    // dtor's PreDestroyCallback re-fire (for entries the drain missed)
    // runs while m_screenStates and friends are still alive. The decl
    // order (m_screenStates before m_shellHost) keeps reverse-destruction
    // safe even if a future change removes the explicit reset.
    QHash<QString, PerScreenOverlayState> m_screenStates;
    std::unique_ptr<PhosphorOverlay::ShellHost> m_shellHost;
    /// Twin host for the scrolling tab indicators' own per-screen surface,
    /// keyed the same way and torn down at the same call sites. They are not
    /// slots on the passive shell because the compositor SLIDES their surface
    /// with the strip, and a surface translates as a whole — see
    /// PhosphorRoles::ScrollTabShell. Declared next to m_shellHost so the
    /// destruction-order rationale above covers both.
    std::unique_ptr<PhosphorOverlay::ShellHost> m_tabShellHost;

    // Scroll tab-strip bookkeeping. Below m_shellHost deliberately: these
    // are plain per-screen maps with no teardown dependency on either
    // neighbour, so they must not sit between m_screenStates and m_shellHost
    // and break up the declaration-order rationale above.
    /// Per-screen generation guard for the scroll tab-strip animated hide:
    /// bumped per updateScrollTabStrips call so a hide completion that lost
    /// the race to a newer non-empty update no-ops instead of tearing down
    /// a repopulated slot. Retained after teardown (monotonic).
    QHash<QString, quint64> m_scrollTabsHideGuard;
    /// Screens with a hide in flight; the show path treats these as not
    /// visible so a mid-hide repopulation re-runs beginShow.
    QSet<QString> m_scrollTabsHidePending;
    /// Last non-empty strip model per screen, cached regardless of the
    /// enable toggle so re-enabling the indicator can replay it (the
    /// engine's tabStripsChanged is change-latched and stays silent).
    QHash<QString, QVariantList> m_lastScrollTabStrips;
    /// Per-screen drop-indicator paint overrides (see
    /// setScrollDropIndicatorOverrides).
    QHash<QString, QVariantMap> m_scrollDropIndicatorOverrides;
    /// Per-DRAG drop-indicator colour overrides from the dragged window's
    /// rules (see setScrollDropIndicatorWindowOverrides). Not per screen: one
    /// window is dragged at a time, and the entry lives only for that drag.
    QVariantMap m_scrollDropIndicatorWindowOverrides;
    /// Per-screen tab-indicator paint overrides (see
    /// setScrollTabIndicatorOverrides). Screens with no context rule carry no
    /// entry, so the common case costs one empty-hash lookup.
    QHash<QString, QVariantMap> m_scrollTabIndicatorOverrides;
    /// Window-local rects of the live tab indicators per screen, handed to the
    /// tab shell as its input region so clicks land on a tab but fall through
    /// everywhere else. Rebuilt from each strip update; cleared with the
    /// screen's strips.
    QHash<QString, QRegion> m_scrollTabInputRegions;
    /// Last announced wl_surface object id per screen's tab-indicator surface.
    /// The change gate for scrollTabSurfaceChanged, and the record of what the
    /// compositor currently believes, so teardown knows whether it owes a
    /// retraction.
    QHash<QString, quint32> m_scrollTabSurfaceIds;

    /// Per-screen generation guard for the drop indicator's animated hide,
    /// same contract as m_scrollTabsHideGuard: a hide completion that lost the
    /// race to a newer rect must no-op rather than tear down a repopulated
    /// slot. A drag pushes rects at pointer rate, so this race is the common
    /// case here, not the exotic one. Retained after teardown (monotonic) —
    /// must never restart, which is why unwirePassiveShellSlots erases the two
    /// maps below but deliberately not this one.
    QHash<QString, quint64> m_scrollDropIndicatorHideGuard;
    /// Screens with a drop-indicator hide in flight; the show path treats
    /// these as not visible so a mid-hide repopulation re-runs beginShow.
    QSet<QString> m_scrollDropIndicatorHidePending;
    /// Last rect pushed per screen, in SHELL-LOCAL px (already shifted by the
    /// screen origin) — the space that is actually painted, so a screen move
    /// invalidates the entry instead of comparing equal. Change-gate only: a
    /// drag re-pushes the same target on every tick, and without this each tick
    /// would re-write the QML properties, re-assert the shell's click-through
    /// flag and re-run a surface sync. An entry is written only once the update
    /// has passed every bail, so it records what is genuinely on screen.
    QHash<QString, QRect> m_lastScrollDropIndicatorRect;

    QPointer<PhosphorZones::Layout> m_layout;
    QPointer<ISettings> m_settings;
    ScrollZonesProvider m_scrollZonesProvider;
    LayoutsProvidedResolver m_layoutsProvidedResolver;
    /// Borrowed from Daemon. stop() detaches this even when init never reached start().
    PhosphorContext::IContextResolver* m_contextResolver = nullptr;
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

    // Shortcut cheatsheet — same singleton-slot shape as the picker.
    QString m_cheatsheetScreenId;
    bool m_cheatsheetVisible = false;

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

    /// Lazily create the per-screen ScrollTabShell + return the state entry
    /// (or nullptr if creation failed). The twin of @ref ensurePassiveShellFor
    /// for the tab indicators' exclusive surface; called only from the strip
    /// update path, so a screen that never shows a tabbed column never
    /// materialises one.
    PerScreenOverlayState* ensureScrollTabShellFor(const QString& effectiveId, QScreen* physScreen);

    /// PostCreate / PreDestroy hooks registered with @c m_tabShellHost, the
    /// twins of @ref wirePassiveShellSlots and @ref unwirePassiveShellSlots.
    /// The tab shell hosts exactly one slot and forwards exactly one signal,
    /// so both are correspondingly short.
    void wireScrollTabShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState);
    void unwireScrollTabShellSlots(const QString& screenId);

    /// Reconcile the tab shell's mapped state and input region for @p
    /// effectiveId. Simpler than its passive-shell counterpart because the
    /// surface hosts one display-only slot: it is mapped exactly while that
    /// slot is visible, never grabs input wholesale, and takes clicks only on
    /// the indicator rects.
    void syncScrollTabShellSurfaceState(const QString& effectiveId);

    /// Publish (or retract, with @p window null) the wl_surface object id of
    /// @p screenId's tab-indicator surface. Change-gated, so the ordinary
    /// re-assert on every strip update costs one hash compare.
    void announceScrollTabSurface(const QString& screenId, QQuickWindow* window);

    /// Drop @p screenId's now-dead ShellState entry on BOTH hosts. Destroying a
    /// shell only zeroes its fields; the entry survives, so hot-plug cycles
    /// would accumulate dead keys without this.
    void removeShellStates(const QString& screenId);

    /// Clear sticky creation-failure sentinels on both hosts for every screen
    /// id rooted on @p physicalScreenId (the bare id and its `/vs:N` children),
    /// so a replug of the same monitor can create its shells again.
    void clearShellFailuresForPhysicalScreen(const QString& physicalScreenId);

    /// Slot-hide animation completion - flips the OSD slot Item's
    /// `visible` to false once the SurfaceAnimator's hide leg settles,
    /// so a subsequent show with no content state writes doesn't paint a
    /// stale prior-frame opaque card before the next show's beginShow
    /// reasserts opacity = 0 → 1. Called from the lambda passed to
    /// `beginHide`. Per-screen-id keying tolerates surface destruction
    /// during the hide leg.
    void onOsdSlotHideCompleted(const QString& effectiveId);

    /// Shared property-push parameters for layout-OSD content. Defined in
    /// overlayservice_types.h; aliased here so existing nested-name
    /// references keep working.
    using LayoutOsdContentParams = PlasmaZones::LayoutOsdContentParams;
    void pushLayoutOsdContent(QObject* osdSlot, const LayoutOsdContentParams& params);

    /// Resolve a surface-decoration pack from the settings' DecorationProfileTree
    /// (@p surfacePath, e.g. "osd" / "popup.snapAssist" / "popup.zoneSelector" /
    /// "popup.layoutPicker") and push it onto @p slot's decoration properties
    /// (Stage d). Shared by every OSD show path (all modes: layout / locked /
    /// disabled / navigation) and the three transient popup show paths. Clears
    /// the slot's decorationChain (and decorationOuterPadding) when no pack
    /// resolves so a stale decoration never renders.
    void applyDecoration(QObject* slot, const QString& surfacePath);

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

    /// Animator-driven slot-hide completion for the cheatsheet. Mirrors
    /// onLayoutPickerSlotHideCompleted.
    void onCheatsheetSlotHideCompleted(const QString& effectiveId);

    /// Reset the modal singleton state (snap assist / layout picker /
    /// cheatsheet) and emit the dismissed signals when the screen that
    /// owns them is destroyed. Called from every runtime shell-teardown site
    /// (not the service destructor, where resetting members and emitting
    /// dismissed signals is moot) — the definition in
    /// overlayservice/screens.cpp keeps the current list.
    void resetModalSingletonsForDestroyedId(const QString& id);

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
    /// visibility. The shell's keepMappedOnHide is effects-gated
    /// (createWarmedOsdSurface): the window is kept mapped on hide only
    /// while shaders or animations are enabled. Surface::show()/hide()
    /// flip Qt::WindowTransparentForInput. The flip only happens
    /// through Surface's state machine, not through slot-level
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
     * @param outEffectiveScreenId Output: resolved screen id (caller's
     *        @p screenId, or the target physical screen's identity when the
     *        caller passed an empty id) — feed this to per-screen context
     *        lookups such as overlayOverrideForScreen
     * @param screenId Target screen (empty = primary)
     * @return true if window is ready, false on failure
     */
    bool prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface, QQuickItem*& outOsdSlot,
                                QScreen*& outPhysScreen, QRect& screenGeom, qreal& aspectRatio,
                                QString& outEffectiveScreenId, const QString& screenId = QString());

    /// Parameters for @ref createLayerSurface. Defined in
    /// overlayservice_types.h; aliased here so existing nested-name
    /// references keep working.
    using LayerSurfaceParams = PlasmaZones::LayerSurfaceParams;

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
     * (2) this helper calls createLayerSurface with keepMappedOnHide
     * gated on effects (kept mapped only while shaders or animations
     * are enabled; with both off the next syncSurfaceState unmaps the
     * wl_surface). Dismiss wiring is NOT done here: per-content
     * auto-dismiss flows through the shell window's per-slot signals
     * (`osdDismissRequested` et al.), which wirePassiveShellSlots
     * connects to the matching OverlayService slot (e.g.
     * onOsdDismissRequested) for an animator-driven ShellHost::hideSlot.
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
