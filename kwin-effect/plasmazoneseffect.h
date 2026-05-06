// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <compositor_bridge.h>
#include <PhosphorProtocol/WireTypes.h>
#include <trigger_parser.h>

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/offscreeneffect.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <effect/globals.h> // For ElectricBorder enum
#include <scene/borderradius.h>
#include <QJsonArray>
#include <QObject>
#include <QVector>
#include <QSet>
#include <QSize>
#include <QThreadPool>
#include <QTimer>
#include <QDBusPendingCall>
#include <QHash>
#include <QPointer>
#include <QRect>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

#include <PhosphorIdentity/VirtualScreenId.h>

#include "plasmazoneseffect/types.h"

namespace KWin {
class OutlinedBorderItem;
class SurfaceItem;
class LogicalOutput;
}

namespace PhosphorAnimation {
class IMotionClock;
}

namespace PlasmaZones {

// Mirror of core/enums.h AutotileDragBehavior. The effect can't include daemon
// headers (KWin plugin ABI constraints), so the values are duplicated here.
// MUST stay in sync with core/enums.h — bump both in the same commit. The
// static_asserts below pin the integer encoding so a drift on either side
// becomes a compile-time failure rather than a silent runtime mismatch.
enum class EffectAutotileDragBehavior : int {
    Float = 0, ///< Drag-to-float (PlasmaZones default)
    Reorder = 1, ///< Drag-to-reorder (Krohnkite-style)
};
static_assert(static_cast<int>(EffectAutotileDragBehavior::Float) == 0,
              "EffectAutotileDragBehavior::Float must encode as 0 to match core/enums.h AutotileDragBehavior::Float");
static_assert(
    static_cast<int>(EffectAutotileDragBehavior::Reorder) == 1,
    "EffectAutotileDragBehavior::Reorder must encode as 1 to match core/enums.h AutotileDragBehavior::Reorder");

// Forward declarations for helper classes
class AutotileHandler;
class KWinCompositorBridge;
class NavigationHandler;
class ScreenChangeHandler;
class SnapAssistHandler;
class CompositorClock;
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
class PlasmaZonesEffect : public KWin::OffscreenEffect
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
    void slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy);

    // Daemon-driven batch operations (rotate, resnap)
    void slotApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);
    void slotRaiseWindowsRequested(const QStringList& windowIds);

    // Snap-all (effect collects candidates, daemon computes assignments)
    void slotSnapAllWindowsRequested(const QString& screenId);
    void slotPendingRestoresAvailable();
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    void slotRunningWindowsRequested();
    void slotRestoreSizeDuringDrag(const QString& windowId, int width, int height);
    void slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                             const PhosphorProtocol::EmptyZoneList& emptyZones);
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
     * @brief Compose the window's composite runtime identifier.
     *
     * Returns the "appId|instanceId" composite that every daemon-side
     * service uses as its primary key. `appId` is the live app class read
     * from KWin at first observation; `instanceId` is KWin's internalId()
     * UUID string and is stable for the window's lifetime.
     *
     * The composite is cached per EffectWindow* in m_windowIdCache and
     * returned unchanged for the rest of the window's lifetime — even if
     * KWin subsequently emits windowClassChanged for an Electron/CEF app
     * that swaps its class. The stable key semantic is load-bearing:
     * daemon maps keyed by windowId must not shift under mid-session class
     * mutations. Live class lookups happen separately via getWindowAppId()
     * on the effect side, and via WindowRegistry::appIdFor() on the daemon
     * side after pushWindowMetadata() updates the registry.
     */
    QString getWindowId(KWin::EffectWindow* w) const;

    /**
     * @brief Extract the compositor-supplied stable instance token.
     *
     * Returns only KWin's internalId() UUID string, without the appId
     * prefix. Use when a caller specifically needs the raw instance token
     * (e.g. pushWindowMetadata feeds it to the daemon's WindowRegistry as
     * the primary key), not the composite key used by the daemon services.
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
     * Mirrors the daemon's Phosphor::Screens::ScreenIdentity::identifierFor() exactly: tries
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
    //
    // profilePath drives the shader-transition resolve (see ShaderProfileTree). This used to be
    // hardcoded to "zone.snapIn" inside applySnapGeometry, which fired the same shader for every
    // motion that flowed through this chokepoint — snap-in, snap-out, resnap, resize, restore, etc.
    // Callers now pass the logical event path so the shader tree can route each one independently.
    // Default is ZoneSnapIn for source compatibility with the legacy hardcoded path.
    void applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag = false,
                           bool skipAnimation = false,
                           const QString& profilePath = PhosphorAnimation::ProfilePaths::ZoneSnapIn);
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

    QHash<QString, WindowBorder> m_windowBorders; // windowId → border

    // Policy returned from the daemon's beginDrag for the currently-active
    // drag. Async-populated a few ms after the
    // drag starts; until then, conservative defaults apply (snap-path
    // with streaming) so the worst-case UX is a brief zone-overlay flash
    // rather than a dead drag. Cleared at drag end.
    PhosphorProtocol::DragPolicy m_currentDragPolicy;

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
    // Per-output motion clocks. One `CompositorClock` per `LogicalOutput`
    // so mixed refresh-rate displays (e.g., 60 Hz + 144 Hz) phase-lock
    // independently — see IMotionClock docs. Populated on construction
    // from `KWin::effects->screens()` and maintained via the
    // screenAdded/screenRemoved signals. A fallback unbound clock is
    // always present for the degenerate no-output / migrated-window
    // cases. Every clock outlives `m_windowAnimator` — animator holds
    // non-owning pointers into these via captured MotionSpecs —
    // guaranteed by destruction order (animator declared after).
    std::unique_ptr<CompositorClock> m_motionClockFallback;
    std::unordered_map<KWin::LogicalOutput*, std::unique_ptr<CompositorClock>> m_motionClocksByOutput;
    PhosphorAnimation::IMotionClock* clockForOutput(KWin::LogicalOutput* output) const;
    void onScreenAdded(KWin::LogicalOutput* output);
    void onScreenRemoved(KWin::LogicalOutput* output);
    /// Per-effect curve registry. Replaces the prior per-process
    /// CurveRegistry::instance() singleton — composition roots own
    /// their own. Declared BEFORE m_windowAnimator so a future
    /// curve-driving member that captures a CurveRegistry reference
    /// (today: only animationEasingCurve loadSettingAsync at construction
    /// time) outlives the animator on shutdown.
    PhosphorAnimation::CurveRegistry m_curveRegistry;
    std::unique_ptr<WindowAnimator> m_windowAnimator;

    // Phase 6: per-window shader transitions via OffscreenEffect.
    // The registry is populated from the same search paths as the daemon's
    // via loadShaderRegistryFromDbus(). Until then, effect lookups return
    // invalid and shader transitions gracefully no-op.
    PhosphorAnimationShaders::AnimationShaderRegistry m_animationShaderRegistry;
    PhosphorAnimationShaders::ShaderProfileTree m_shaderProfileTree;

    /// User-texture cache, keyed by absolute path. Multiple shader effects
    /// (and multiple legs of the same effect) that reference the same
    /// texture file share one upload — saves both GPU memory and the
    /// per-leg `KWin::GLTexture::upload` cost. Cleared on
    /// `effectsChanged` alongside `m_shaderCache` so a hot-reload that
    /// drops a texture file frees the GPU memory rather than holding it
    /// for the rest of the session. The registry's per-effect watch
    /// list (see `effectWatchPaths` in animationshaderregistry.cpp)
    /// includes declared texture file paths, so a bitmap content
    /// change with no metadata change still re-fires `effectsChanged`
    /// and invalidates this cache uniformly — no separate watcher
    /// needed here.
    ///
    /// **Growth policy.** Between consecutive `effectsChanged` events
    /// the cache grows monotonically with the count of UNIQUE texture
    /// paths surfaced over the session. Bounded in practice by the
    /// total number of distinct textures across all installed packs
    /// (kMaxUserTextureSlots × pack count), which is small. No LRU /
    /// reference-count eviction is implemented because (a) every
    /// metadata edit / live-reload triggers a full clear, and (b) the
    /// per-texture footprint is tens of MiB at most for atlas-style
    /// uploads. Revisit if a third-party pack channel pushes the
    /// long-tail bitmap count higher.
    ///
    /// **Declaration ORDER MATTERS.** C++ destroys members in reverse
    /// declaration order, so `m_textureCache` declared FIRST means it
    /// destructs LAST — outlives `m_shaderCache` and `m_shaderTransitions`.
    /// `ShaderTransition::userTextures[]` holds raw non-owning pointers
    /// into this cache; a reverse declaration would let the cache
    /// destruct first and leave the transitions with dangling pointers.
    /// `m_shaderCache` similarly needs to outlive its dependents (the
    /// shader-program pointers in transitions) — declared second so it
    /// destructs after `m_shaderTransitions`.
    std::map<QString, CachedTexture> m_textureCache;
    /// Async texture pre-warm pipeline.
    ///
    /// Texture loading (PNG decode + format conversion + GLTexture upload —
    /// or, worse, SVG rasterise via QSvgRenderer at 1024px max-axis +
    /// upload) takes multiple milliseconds per file on a cold cache, all
    /// of which would otherwise run synchronously on the compositor
    /// thread inside `beginShaderTransition`'s per-leg load loop. At
    /// 144 Hz the per-frame budget is ~6.9 ms, so a single uncached
    /// SVG slot can blow the budget on its own and produce a visible
    /// stutter on the very first frame of a freshly-assigned shader.
    ///
    /// Pattern: kick `warmUserTextureAsync` to off-load `loadUserTextureImage`
    /// (CPU-only — QImage decode and QSvgRenderer rasterise) onto a worker
    /// thread, then dispatch the resulting `QImage` back to the compositor
    /// thread via `QMetaObject::invokeMethod(...,
    /// Qt::QueuedConnection)` to perform `KWin::GLTexture::upload`
    /// (which MUST run on the GL-context thread). Subsequent
    /// transitions hit the warm cache and pay zero load cost.
    ///
    /// Pool size is fixed at 1 to keep semantics simple — multi-threaded
    /// loads of the same file would race on `m_textureLoadsInFlight`
    /// dedupe and produce duplicate GPU uploads. A serialised single-
    /// worker pool delivers near-all of the latency win (the bottleneck
    /// is the rasterise/decode cost, not parallelism) without that
    /// complexity.
    QThreadPool m_textureLoaderPool;
    /// Set of absolute texture paths currently in-flight on
    /// `m_textureLoaderPool`. Prevents `warmUserTextureAsync` from
    /// queuing duplicate loads when several shader transitions
    /// reference the same file in quick succession (e.g. multiple
    /// windows opening at once with the same effect assigned).
    /// Entries are removed when the GL-thread upload completes (or
    /// the worker observes a load failure).
    /// All access happens on the compositor thread; the worker reads only
    /// the captured path string and never touches m_textureLoadsInFlight.
    /// The queued upload lambda removes the entry on the compositor thread
    /// before any cache mutation.
    QSet<QString> m_textureLoadsInFlight;
    /// Generation counter for the texture cache. Bumped on every
    /// `effectsChanged` hot-reload so workers in flight (which captured
    /// the generation at submission time) can discard their queued GL
    /// upload if the cache has been cleared underneath them. Cheaper than
    /// draining the loader pool synchronously on the GL thread, which
    /// could block the compositor for tens of ms on a worker mid-rasterise
    /// of a 1024x1024 SVG. Distinct from the destructor's
    /// `m_textureLoaderPool.waitForDone()` — full teardown still waits to
    /// flush pending invokeMethod posts against `this` while members are
    /// still intact.
    quint64 m_textureCacheGeneration = 0;
    /// Monotonic counter bumped on every cache lookup / insert. Stamped
    /// onto `CachedTexture::lastAccessTick` so the LRU sweep below has
    /// a stable order. Distinct from `m_textureCacheGeneration` (which
    /// flips on hot-reload to invalidate in-flight workers).
    quint64 m_textureCacheAccessTick = 0;
    /// Soft upper bound on `m_textureCache` size. The cache is path-keyed
    /// and entries can be referenced by raw pointers held in
    /// `ShaderTransition::userTextures[]`, so eviction MUST skip any
    /// entry currently in-flight. With kMaxUserTextureSlots * (typical
    /// pack count of 5..20) the cache normally settles at <50 entries
    /// even on third-party-pack-heavy installations; the cap is a
    /// long-soak / hot-reload-without-effects-changed safety net rather
    /// than a routine pressure relief.
    static constexpr std::size_t kTextureCacheSoftBound = 32;
    /// LRU sweep: when `m_textureCache.size() > kTextureCacheSoftBound`,
    /// evict the entry with the smallest `lastAccessTick` that is NOT
    /// referenced by any `m_shaderTransitions[*].userTextures[*]`. If
    /// every entry is in flight (pathological), no-op — the cache
    /// transiently exceeds the bound rather than tearing a live
    /// transition's pointer. Called from the two cache-insert sites
    /// (sync fallback in `beginShaderTransition` and async upload in
    /// `warmUserTextureAsync`).
    void evictLruTextureIfOverBound();
    /// Off-load a texture load onto the loader pool, then upload to
    /// the GL cache on the compositor thread when the worker
    /// finishes. Returns immediately if the path is already cached
    /// or already in-flight. SVGs are rasterised at the same
    /// 1024-px max-axis as `loadUserTextureImage`'s default — the
    /// path-keyed cache assumes a single canonical size per asset,
    /// so per-call size overrides would silently collide on the
    /// cache key with the first-loader-wins.
    void warmUserTextureAsync(const QString& absolutePath);
    // Invariant: all ShaderTransition.cached pointers must be ended
    // (via endShaderTransition) before any cache erasure.
    std::map<QString, CachedShader> m_shaderCache;
    std::unordered_map<KWin::EffectWindow*, ShaderTransition> m_shaderTransitions;
    /// Windows for which a paintWindow expiry-fall-through has already
    /// queued a deferred @ref endShaderTransition via
    /// @c QMetaObject::invokeMethod. Guards against the next paint
    /// frame (before the queued end has actually run) re-queuing a
    /// duplicate end and double-tearing-down the same transition.
    /// Cleared when the queued end actually runs (success path) or
    /// when the transition is otherwise erased / superseded.
    QSet<KWin::EffectWindow*> m_pendingShaderExpiryEnd;
    /// 1Hz cache for the `iDate` uniform. Recomputing iDate from
    /// `QDateTime::currentDateTime()` on every paintWindow tick adds
    /// microseconds per call, multiplied by every active transition,
    /// for every output, every frame. The daemon's
    /// `shadernoderhiuniforms.cpp` already caches at 1Hz for the same
    /// reason — sub-second iDate variation is invisible (the .w
    /// channel carries floating-point seconds anyway, but seeded once
    /// per second is plenty of precision for the typical iDate-driven
    /// shader effect). Mirror that policy here so kwin-side parity
    /// matches the daemon side and the cost stays bounded.
    qint64 m_lastIDateRefreshMs = 0;
    QVector4D m_cachedIDate{};
    /// Cursor position cached once per `prePaintScreen` rather than
    /// re-read in paintWindow per active transition. `KWin::effects->cursorPos()`
    /// is itself cheap, but multiple transitions paying the call per
    /// frame multiplies up unnecessarily — and a cache also guarantees
    /// every transition this frame sees an identical iMouse, eliminating
    /// any sub-frame jitter from the cursor moving between paint calls.
    QPointF m_cachedCursorGlobal;
    /// Monotonic counter feeding @ref ShaderTransition::generation. Bumped
    /// inside @ref beginShaderTransition every time a transition installs;
    /// the timer-driven teardown in @ref tryBeginShaderForEvent compares the
    /// generation it captured at schedule time against the live transition's
    /// generation before tearing down. Mismatch = a successor replaced us;
    /// don't kill the successor.
    quint64 m_shaderTransitionGenerationCounter = 0;
    /// Per-window edge-detection for windowMaximizedStateChanged. KWin can
    /// fire that signal twice for a single user-driven maximize transition
    /// (axis-by-axis), and we only want the WindowMaximize / WindowUnmaximize
    /// shader to fire on the actual edge, not on the intermediate
    /// vertical-only / horizontal-only state. Cleared on windowDeleted.
    QHash<KWin::EffectWindow*, bool> m_lastFullyMaximized;
    /// Last window we fired a window.focus shader on. KWin emits
    /// `windowActivated` on virtual-desktop / activity / re-stack churn even
    /// when the focused window didn't actually change; gating on this avoids
    /// shader spam. QPointer auto-nulls on window destroy so a fresh window
    /// reusing the address can't false-match. Cleared explicitly on
    /// windowDeleted (defence in depth) and on window destroy via QPointer.
    QPointer<KWin::EffectWindow> m_lastFocusShaderWindow;
    /// @p durationMs interpretation:
    ///   • > 0: time-based transition. paintWindow reads progress as
    ///     (now - startTimeMs) / durationMs, linear ramp; tryBeginShaderForEvent's
    ///     timer fires `endShaderTransition` after this many ms.
    ///   • 0 (default) / negative: animator-driven transition. paintWindow
    ///     reads progress from `m_windowAnimator->animationFor(w)` (curved
    ///     by the geometry animation's profile); the animator's completion
    ///     callback drives `endShaderTransition`. Used by `applySnapGeometry`
    ///     for zone.* events that already have an animator-tracked motion.
    /// `tryBeginShaderForEvent` rejects the negative case before calling here;
    /// internal callers (`applySnapGeometry`) pass the default 0.
    ///
    /// @p reverse flips paintWindow's progress to `1 - progress`, so the
    /// shader plays its timeline backwards — used by "going away" events
    /// (window.close, going-to-minimized, going-to-unmaximized) to share a
    /// single user-assigned shader with the matching forward event.
    void beginShaderTransition(KWin::EffectWindow* window, const PhosphorAnimationShaders::ShaderProfile& profile,
                               int durationMs = 0, bool reverse = false, bool holdCloseGrab = false);
    void endShaderTransition(KWin::EffectWindow* window);
    void loadShaderProfileFromDbus();
    void loadShaderRegistryFromDbus();

    /// Resolve @p profilePath against the shader profile tree and, if a non-empty
    /// effect id resolves, kick off a @ref beginShaderTransition on @p window
    /// with a timer-driven @ref endShaderTransition after @p durationMs.
    ///
    /// Used for window-lifecycle events (open, close, focus, minimize, maximize,
    /// move, resize) where there is no @c m_windowAnimator animation to end the
    /// transition for us — those animations are owned by KWin's own effects or
    /// happen instantaneously, so we drive the shader leg on its own timer.
    /// Events that already drive an animator-tracked geometry change (zone.*)
    /// go through @ref applySnapGeometry's chokepoint instead and let the
    /// animator's completion callback tear down the shader.
    ///
    /// No-op when the profile resolves to empty effectId (user hasn't assigned
    /// a shader to this path), the registry doesn't have the effect yet, or
    /// the window pointer is null. Same null-tolerance contract as
    /// @ref beginShaderTransition.
    ///
    /// @p reverse forwards to @ref beginShaderTransition's reverse flag — see
    /// that doc for semantics. Defaults to false (forward 0→1 timeline).
    ///
    /// @p holdCloseGrab is true only for the @c slotWindowClosed call site;
    /// it claims the window via @c KWin::WindowClosedGrabRole so KWin's
    /// teardown blocks final deletion until our close shader has had its
    /// frames. The grab is set BEFORE @ref beginShaderTransition's redirect
    /// (so it lands while the window is still in the closing-but-not-yet-
    /// deleted window of validity) and stored on the resulting transition's
    /// @c closeGrabHeld field so @ref endShaderTransition can release it
    /// when the timer-driven teardown runs. Pre-resolved here (rather than
    /// inside the lambda) because we need to skip the grab when no shader
    /// will install — otherwise a holdCloseGrab=true caller with no user-
    /// assigned close shader would strand the window in closing state
    /// forever.
    void tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                bool reverse = false, bool holdCloseGrab = false);

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
    // the compositor thread. See ClientHelpers::asyncCall() and ClientHelpers::fireAndForget().

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
    // Pinned to the canonical Limits constant rather than an inline magic
    // number so a future bump in the suite-wide default propagates here
    // automatically and a malformed daemon reply (zero/negative) clamped
    // through Limits at the assignment site stays structurally safe even
    // before the first reply arrives.
    int m_cachedAnimationDuration =
        PhosphorAnimation::Limits::DefaultAnimationDurationMs; // ms, fallback until loaded from daemon
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
    /// True between sending registerBridge and receiving its reply. Prevents
    /// the Introspect probe + daemonReady signal racing into two concurrent
    /// registrations before the first reply sets m_daemonServiceRegistered.
    /// Reset on every reply path (success / error / rejection / version-
    /// mismatch) so a future retry can re-arm. ALSO reset in the
    /// serviceUnregistered handler so a daemon restart with an in-flight
    /// stale call doesn't leave the gate stuck and silently swallow the
    /// new daemon's daemonReady signal.
    bool m_bridgeRegistrationInFlight = false;
    bool m_daemonReadyRestoresDone = false; ///< set after slotDaemonReady snap restores dispatched

    /// Pre-computed snap restore target for a pending app (appId → geometry + saved screen).
    /// Fetched once from daemon on ready; consumed in slotWindowAdded for instant
    /// teleport (no D-Bus round-trip visible flash). The screenId lets the effect
    /// tell "cached saved zone is on snap-mode screen X" from "current KWin
    /// placement is autotile screen Y" — we trust the saved screen, not the
    /// placement, so cross-VS / cross-monitor restores work.
    QHash<QString, CachedSnapRestore> m_snapRestoreCache;
    bool m_virtualScreensReady = false; ///< set after all fetchVirtualScreenConfig replies arrive
    /// True while a daemon-driven geometry apply (slotApplyGeometriesBatch / slotWindowsTileRequested)
    /// is moving a window. Suppresses the windowFrameGeometryChanged crossing-detection paths so a
    /// VS swap/rotate does not produce spurious "window moved between monitors" events. The daemon
    /// emits virtualScreensChanged and the geometry batch in the same handler chain, but on the
    /// effect side those D-Bus messages can race: the geometry change fires while m_virtualScreenDefs
    /// still holds the pre-rotation regions, so the crossing comparison computes newScreenId from
    /// stale config + new position and falsely concludes the window crossed VSes. The daemon is the
    /// authoritative source of the window's intended VS during these applies, so the crossing check
    /// is unsafe and must be skipped.
    bool m_inDaemonGeometryApply = false;
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
