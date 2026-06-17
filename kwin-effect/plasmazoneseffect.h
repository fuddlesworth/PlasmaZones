// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorCompositor/DecorationManager.h>
#include <PhosphorCompositor/ICompositorBridge.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorCompositor/TriggerParser.h>

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/offscreeneffect.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <effect/globals.h> // For ElectricBorder enum
#include <scene/borderradius.h>
#include <QObject>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QHash>
#include <QPointer>
#include <QRect>

#include <functional>
#include <memory>
#include <unordered_map>

#include "shadertransitionmanager.h"

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

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
// (Re-declaring the same alias/using in a sibling header is well-formed.)
using PhosphorCompositor::BorderState;
using PhosphorCompositor::DecorationManager;
using PhosphorCompositor::ICompositorBridge;
using PhosphorCompositor::ParsedTrigger;
namespace AutotileStateHelpers = PhosphorCompositor::AutotileStateHelpers;
namespace DecorationDefaults = PhosphorCompositor::DecorationDefaults;
namespace TriggerParser = PhosphorCompositor::TriggerParser;

// Mirror of PhosphorTiles::AutotileDragBehavior (re-exported via core/enums.h).
// The effect can't include daemon headers (KWin plugin ABI constraints), so the
// values are duplicated here. MUST stay in sync with the canonical enum — the
// static_asserts below pin the integer encoding so a drift on either side
// becomes a compile-time failure rather than a silent runtime mismatch.
enum class EffectAutotileDragBehavior : int {
    Float = 0, ///< Drag-to-float (PlasmaZones default)
    Reorder = 1, ///< Drag-to-reorder (Krohnkite-style)
};
static_assert(static_cast<int>(EffectAutotileDragBehavior::Float) == 0,
              "EffectAutotileDragBehavior::Float must encode as 0 to match PhosphorTiles::AutotileDragBehavior::Float");
static_assert(
    static_cast<int>(EffectAutotileDragBehavior::Reorder) == 1,
    "EffectAutotileDragBehavior::Reorder must encode as 1 to match PhosphorTiles::AutotileDragBehavior::Reorder");

// Forward declarations for helper classes
class AutotileHandler;
class SnapHandler;
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

    void clearDaemonCompositorState();

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

protected:
    // OffscreenEffect hook: deform the redirected window's quad list.
    // For surface-extent shader transitions (metadata `fboExtent:
    // "surface"`) this replaces the window quad with one spanning the
    // window's output, so the shader can paint past the window bounds.
    // Every other redirected window is left untouched (drawn 1:1 over
    // its own geometry).
    void apply(KWin::EffectWindow* window, int mask, KWin::WindowPaintData& data, KWin::WindowQuadList& quads) override;

    // Capture the redirected window's current (pre-moveResize) content into a
    // GLTexture for the geometry-morph cross-fade. Replicates KWin's
    // OffscreenData::maybeRender: render the window into our own FBO via
    // effects->drawWindow (our morph shader temporarily bypassed so the copy
    // is the raw old content), then store the texture in
    // `transition.oldSnapshot` (bound as uOldWindow). Called once on the first
    // morph paint, while the content is still old (the moveResize configure
    // hasn't round-tripped).
    void captureOldWindowSnapshot(ShaderTransition& transition, KWin::EffectWindow* window);

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
    void slotWindowDesktopMoveRequested(const QString& windowId, int desktop);
    void slotWindowOutputMoveExpected(const QString& windowId, const QString& targetScreenId);

    // Float toggle is entirely daemon-local — no effect-side slot needed.

    // Daemon tells the effect the drag routing has flipped mid-drag (cursor
    // crossed a virtual-screen boundary that changes autotile↔snap mode).
    // Effect applies the transition: entering/exiting autotile bypass,
    // canceling snap overlay, etc.
    void slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy);

    // Daemon-driven batch operations (rotate, resnap, vs_reconfigure arrive
    // over the wire; the effect-local snap_all path calls this slot directly)
    void slotApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);
    void slotRaiseWindowsRequested(const QStringList& windowIds);

    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    void slotWindowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& state);
    void slotRunningWindowsRequested();
    void slotRestoreSizeDuringDrag(const QString& windowId, int width, int height);

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
     *
     * @param includeExtended When false, the extended-property snapshot (the
     * trailing a{sv}: state flags, geometry, accessory flags, captionNormal) is
     * NOT rebuilt or sent — the daemon preserves whatever it already has. Used by
     * the captionChanged handler: terminals/browsers rewrite their title every
     * frame, and the rule-relevant extended fields don't change on a title tick,
     * so rebuilding/marshalling a ~20-entry map per frame is pure waste. The
     * extended snapshot is captured at window-open and refreshed on identity
     * changes (class/desktop/activity), which is when it matters for the daemon's
     * open-path Float / RestorePosition resolvers.
     */
    void pushWindowMetadata(KWin::EffectWindow* w, bool includeExtended = true);

    /**
     * @brief Snapping/zone-management window filter.
     *
     * @param w            window to classify.
     * @param rejectReason when non-null, set to a human-readable description
     *                     of the first failing clause on a false return, and
     *                     cleared on a true return. Default nullptr — hot-loop
     *                     callers pay nothing. Used by logWindowDiagnostics()
     *                     so the rejection reason has a single source of truth
     *                     (this function) and cannot drift from the filter.
     */
    bool shouldHandleWindow(KWin::EffectWindow* w, QString* rejectReason = nullptr) const;

    /**
     * @brief Autotile-tree eligibility filter. @see shouldHandleWindow for the
     *        @p rejectReason out-parameter contract.
     */
    bool isTileableWindow(KWin::EffectWindow* w, QString* rejectReason = nullptr) const;

    /**
     * @brief Shared window-TYPE rejection predicate.
     *
     * Returns true when @p w is a structurally unmanageable window kind
     * (special/desktop/dock/fullscreen/skipSwitcher, or the transient/dialog/
     * menu/popup/tooltip family). Single source of truth behind both
     * shouldHandleWindow()'s structural clause and notifyWindowActivated()'s
     * focus-tracking filter, so the two can never drift (discussion #461
     * item 11).
     *
     * @param w            window to classify; must be non-null.
     * @param rejectReason when non-null, set to a human-readable reason on a
     *                     true return. @see shouldHandleWindow.
     */
    bool isStructurallyUnmanageableWindowType(KWin::EffectWindow* w, QString* rejectReason = nullptr) const;

    /// Classify a window's structural kind for the snap-restore consume gate.
    PhosphorEngine::WindowKind classifyWindowKind(KWin::EffectWindow* w) const;

    /**
     * @brief Emit a full dump of a window's KWin properties plus the snap and
     *        autotile filter verdicts.
     *
     * One call per window-open (slotWindowAdded) and per class/metadata change.
     * Logged under the opt-in `plasmazones.effect.diag` category at debug
     * level, so it is silent by default and never floods the journal; enable
     * it on demand with QT_LOGGING_RULES="plasmazones.effect.diag.debug=true".
     * Exists to diagnose apps whose windows KWin mis-classifies: Steam and
     * other CEF/Electron clients report inconsistent window-type flags and
     * reparent surfaces mid-session, so the only reliable way to fix their
     * tiling behaviour is to see every flag the filters consult. The dump
     * lists each flag and the exact clause that rejected the window.
     */
    void logWindowDiagnostics(KWin::EffectWindow* w, const char* context) const;

    /**
     * @brief Animation-side window filter.
     *
     * Returns true when @p w should animate, false when the user's
     * animation Window Filtering settings exclude it. Mirrors the
     * snapping/tiling Exclusions but pulls from the separate
     * `Animations.WindowFiltering` cache so the two filter sets can
     * diverge.
     *
     * A WindowRule carrying any OverrideAnimation* or SetOpacity
     * action whose match expression resolves for the window OVERRIDES
     * the filter — the existence of even one targeted rule signals
     * deliberate user intent to animate this app, regardless of
     * which event the cascade is firing for. The match expression
     * walks the full per-window query (AppId / WindowClass / Title /
     * WindowRole / DesktopFile / WindowType / Pid / state flags) so
     * a rule pinned to any of those axes triggers the override; a
     * window with no rule-matchable attributes at all falls through
     * to the filter.
     */
    bool shouldAnimateWindow(KWin::EffectWindow* w) const;

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

    /**
     * @brief Recognise the daemon's own overlay surface AND the editor window
     *        by window class.
     *
     * The shouldHandleWindow filter rejects both as "own overlay/editor window
     * class" so the snap/tile pipeline never targets them — that is the right
     * scope for tiling exclusion: neither the daemon overlay nor the editor may
     * ever be tiled.
     *
     * Do NOT use this for the focus-follows-mouse look-through — the editor is
     * an interactive window that must keep its focus. Use
     * isOwnPassthroughOverlayClass() there instead.
     */
    static bool isOwnOverlayClass(const QString& windowClass);

    /**
     * @brief Recognise only the daemon's non-interactive passthrough overlay
     *        surface ("plasmazonesd") by window class.
     *
     * The focus-follows-mouse stacking walks look THROUGH this surface to the
     * real user window beneath, because it is full-screen, permanently topmost,
     * and never holds keyboard focus (PR #517 / discussion #461 #3). The
     * interactive editor is intentionally excluded so FFM treats it as a real
     * occluder and leaves focus on it.
     */
    static bool isOwnPassthroughOverlayClass(const QString& windowClass);

    /**
     * @brief Reject XDG desktop portal surfaces by window class.
     *
     * File dialogs / color pickers / screenshot pickers brokered by
     * `xdg-desktop-portal-*` services arrive with classes like
     * "xdg-desktop-portal-kde" / "xdg-desktop-portal-gtk". Snapping or
     * tracking them as user-focus targets pollutes the daemon's
     * last-active-window state. Shared by `shouldHandleWindow` and
     * `notifyWindowActivated` so the two filter chains stay in lockstep.
     */
    static bool isXdgDesktopPortalSurface(const QString& windowClass);

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
     * @brief Build a map of full window IDs to EffectWindow pointers
     *
     * Keys are full window IDs (appId|uuid) from getWindowId(),
     * so two windows of the same app get separate entries. Callers that
     * receive daemon data keyed by appId should do a linear scan fallback
     * when the exact full ID is not found.
     *
     * Always filters to handleable windows (passes shouldHandleWindow()) —
     * the prior `filterHandleable=false` overload had zero callers and was
     * removed as dead surface. Add it back as an explicit parameter if a
     * future caller actually needs the unfiltered map.
     *
     * @return Hash map of fullWindowId -> EffectWindow*
     */
    QHash<QString, KWin::EffectWindow*> buildWindowMap() const;

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
    /// True iff @p windowId is snapped into a zone (snap mode; delegates to the
    /// NavigationHandler zone cache). Autotile tiles carry no zone and are not
    /// snapped under this definition.
    bool isWindowSnapped(const QString& windowId) const;
    /// The snap-zone UUID @p windowId occupies, or empty when it occupies none.
    QString zoneForWindow(const QString& windowId) const;
    /// Build a window-rule match query for @p w with the effect's runtime
    /// placement state (floating / snapped / zone) threaded into the free
    /// `windowRuleQueryFor` builder. Use this at EVERY rule-evaluation site so
    /// IsFloating / IsSnapped / Zone resolve uniformly; the free builder stays
    /// KWin-only and can't reach the effect's caches.
    PhosphorWindowRule::WindowQuery windowRuleQuery(KWin::EffectWindow* w) const;

    /// Resolve the animation rule-action verdict for @p w, skipping the per-frame
    /// `windowRuleQuery(w)` build (≈30 KWin accessor reads) when the evaluator
    /// already has a cached verdict for @p windowId. Peek-then-build: a cache hit
    /// returns the memoised actions directly; a miss builds the query and resolves
    /// (caching the result). An empty windowId or a windowless query yields empty
    /// actions (no slots) WITHOUT caching, matching the resolvers' old
    /// short-circuit (avoids churning the cache for sub-surfaces / proxies). The
    /// per-frame opacity / border resolvers consume the returned ResolvedActions.
    PhosphorWindowRule::ResolvedActions resolveWindowRuleActions(KWin::EffectWindow* w, const QString& windowId) const;

    /**
     * @brief True if the window is currently snap-managed (tiled into a snap zone).
     * Its frame geometry is the zone rect, NOT a free-floating position — callers
     * that capture "pre-tile / float-back" geometry must skip such windows even on
     * fast paths, or the snap zone poisons the autotile float-back (per-mode float
     * independence). Backed by the shared snap BorderState tiled set.
     */
    bool isWindowMarkedSnapped(const QString& windowId) const;

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
     * Mirrors the daemon's PhosphorScreens::ScreenIdentity::identifierFor() exactly: tries
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
    SnapHandler* snapHandler() const
    {
        return m_snapHandler.get();
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

    // Move a window to a target geometry, running the configured placement
    // transition (snap / tile / move). Shared chokepoint for snap zones,
    // autotile tiles, and float restores — not snap-specific despite history.
    // When allowDuringDrag is true, applies immediately even if window is in user move state (snap-on-hover).
    // When false and the window is being dragged, defers via windowFinishUserMovedResized signal.
    //
    // profilePath drives the shader-transition resolve (see ShaderProfileTree). This used to be
    // hardcoded to "window.snapIn" inside applyWindowGeometry, which fired the same shader for every
    // motion that flowed through this chokepoint — snap-in, snap-out, resnap, resize, restore, etc.
    // Callers now pass the logical event path so the shader tree can route each one independently.
    // Default is WindowSnapIn (the kwin-effect's default snap-into-zone window animation).
    void applyWindowGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag = false,
                             bool skipAnimation = false,
                             const QString& profilePath = PhosphorAnimation::ProfilePaths::WindowSnapIn);
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
    // Helper class access methods — consumed across the handler split
    // (AutotileHandler/SnapHandler via decorationManager(), ScreenChangeHandler
    // via applyStaggeredOrImmediate, KWinCompositorBridge via clearScreenIdCache)
    // ═══════════════════════════════════════════════════════════════════════════════
public:
    /// Access the compositor bridge (for shared code that needs compositor-agnostic window ops)
    ICompositorBridge* compositorBridge() const
    {
        return m_compositorBridge.get();
    }

    /// The single owner of server-side decoration (title-bar) state. Every
    /// hide/restore goes through its owner model — handlers and the rule
    /// layer must never call KWin::Window::setNoBorder directly.
    DecorationManager* decorationManager() const
    {
        return m_decorationManager.get();
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
    friend class SnapHandler;
    friend class NavigationHandler;
    friend class ScreenChangeHandler;
    friend class SnapAssistHandler;
    friend class WindowAnimator;
    friend class DragTracker;
    friend class KWinCompositorBridge;
    friend class ShaderTransitionManager;
    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class instances
    // ═══════════════════════════════════════════════════════════════════════════════
    std::unique_ptr<AutotileHandler> m_autotileHandler;
    std::unique_ptr<SnapHandler> m_snapHandler;

    QHash<QString, WindowBorder> m_windowBorders; // windowId → border

    // The window most recently passed to slotWindowActivated — i.e. the
    // "previously active" window on the next focus change. Used to repaint the
    // window that just lost focus so a focus-scoped (IsFocused) SetOpacity rule
    // re-resolves on it; the window gaining focus is repainted via the slot's
    // own argument. QPointer auto-nulls on window destruction.
    QPointer<KWin::EffectWindow> m_lastActivatedWindow;

    // The window currently in an interactive RESIZE (set at
    // windowStartUserMovedResized when isUserResize(), cleared at finish).
    // windowFinishUserMovedResized does not reliably report isUserResize() at
    // teardown, so the resize-vs-move discriminator is latched at start. Used to
    // persist a floating window's new free size the instant the resize ends —
    // distinct from a move, which the drag→snap pipeline owns (a move can end in
    // a snap, so it must not be captured as a free geometry here). QPointer
    // auto-nulls on window destruction.
    QPointer<KWin::EffectWindow> m_resizingWindow;

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

    /// Debounce timer for `WindowRules.rulesChanged`. Single-shot, 50ms;
    /// timeout fires `loadWindowRuleAnimationsFromDbus`. Re-armed on every
    /// `slotWindowRulesChanged` invocation so a burst of per-rule mutations
    /// (a 50-rule batch edit emits 50 signals) collapses into a single
    /// `getAllRules` fetch at the trailing edge.
    QTimer m_animationRulesRefreshDebounce;

    /// Wire the DecorationManager into the effect: the deferred-restore veto
    /// plus the windowDecorationRestored / drainFinished connections.
    /// Defined in borders.cpp with the rest of the decoration code; called
    /// once from the constructor.
    void setupDecorationManager();
    void updateWindowBorder(const QString& windowId, KWin::EffectWindow* w);
    void removeWindowBorder(const QString& windowId);
    void updateAllBorders();
    void clearAllBorders();

    /// Drop the per-window rule match cache and refresh @p windowId's border /
    /// opacity after its placement state (snapped / floating / zone) changed.
    /// Those are rule MATCH inputs now, so without this a window stays resolved
    /// at its prior state (e.g. a `WHEN isSnapped` border never reverting on
    /// unsnap). Mirrors slotWindowActivated's focus invalidation; no-op when
    /// there are no animation rules.
    void invalidateRuleCacheForStateChange(const QString& windowId);

    /// Bulk analog of invalidateRuleCacheForStateChange for placement changes that
    /// affect EVERY window at once — daemon loss (the zone / floating caches are
    /// cleared) and the daemon-ready re-seed (they are repopulated). The match
    /// cache is keyed (windowId, ruleSet revision); neither moves on a bulk
    /// placement change, so a placement-scoped opacity verdict would otherwise
    /// stay cached (e.g. a `WHEN isSnapped` SetOpacity window staying dimmed after
    /// the cache that made it "snapped" was cleared). Drops the whole match cache
    /// and forces a full repaint so opacity rules re-resolve against the current
    /// IsSnapped / IsFloating / Zone state. Borders recover via their own
    /// restore / rebuild path. No-op when there are no animation rules.
    void invalidateAllRuleCaches();

    /// Flush coalesced per-window rule-cache invalidations queued by
    /// invalidateRuleCacheForStateChange within one event-loop turn: drops the
    /// match cache once and re-resolves the border / opacity of each affected
    /// window. Posted via a queued single-shot so a float toggle (which emits
    /// both windowFloatingChanged AND windowStateChanged) clears the cache once
    /// instead of twice.
    void flushPendingRuleInvalidations();

    /// Resolve which mode's BorderState manages @p windowId — autotile first,
    /// then snap — or nullptr if neither draws a border for it.
    const PhosphorCompositor::BorderState* resolveBorderStateFor(const QString& windowId) const;

    /// Resolve the per-window-rule SetHideTitleBar override for @p windowId
    /// and forward it to the DecorationManager as a tri-state rule override
    /// (unset = mode decides, true = rule hides, false = force-show veto).
    void reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w);

    /// Clear every DecorationManager rule override (Rule owners + force-show
    /// vetoes). Called when the rule set empties (updateAllBorders) so a
    /// rule-hidden title bar is never left hidden after the authoritative
    /// rule state is gone. Daemon loss / effect teardown do NOT route here —
    /// they call DecorationManager::restoreAll(), which clears rule
    /// overrides along with all other tracking.
    void restoreAllRuleHiddenTitleBars();

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
    // Shader/texture cache, LRU eviction, warm-up pipeline, profile tree,
    // and transition lifecycle are managed by ShaderTransitionManager.
    ShaderTransitionManager m_shaderManager;

    // Shader transition methods — implementations in shader_transitions.cpp,
    // operating on m_shaderManager state.
    /// Returns true when a fresh leg was installed (or the prior leg was
    /// replaced); false otherwise. Two distinct failure modes share the
    /// `false` return:
    ///
    ///   (a) Same-effect short-circuit — a transition with the same
    ///       effectId, direction, and timing mode is already in flight
    ///       on this window. The prior leg is untouched; its own
    ///       teardown timer (or animator-completion callback) owns the
    ///       teardown. Callers MUST NOT schedule a fresh per-leg timer
    ///       in this case — a new timer would carry the prior leg's
    ///       generation and fire on the new (likely shorter) duration,
    ///       cutting the still-running animation short.
    ///
    ///   (b) Pre-commit short-circuit — install short-circuited before
    ///       any state was committed: empty effectId / null window,
    ///       global animations toggle off, collapsed/minimised surface,
    ///       registry miss, shader file open / read / include-expansion
    ///       failure, or shader compile failure. Nothing was installed,
    ///       so there is nothing to schedule a teardown for either.
    ///
    /// Both cases are correctly handled by `tryBeginShaderForEvent`'s
    /// "skip the timer" branch. A future caller writing a manual install
    /// path that needs to distinguish the two should snapshot
    /// `m_shaderManager.findTransition(window)` (and its generation)
    /// pre-call and compare against the post-call snapshot to detect
    /// case (a).
    bool beginShaderTransition(KWin::EffectWindow* window, const PhosphorAnimationShaders::ShaderProfile& profile,
                               int durationMs = 0, bool reverse = false, bool holdCloseGrab = false,
                               bool holdAddedGrab = false);
    void endShaderTransition(KWin::EffectWindow* window);

    // First-frame open suppression — implementations in window_lifecycle.cpp.
    // beginRestoreSuppression withholds a window from compositing the moment
    // it opens; endRestoreSuppression releases it once it has settled into
    // its zone / tile (or on the hard deadline). See RestoreSuppression.
    void beginRestoreSuppression(KWin::EffectWindow* window);
    void endRestoreSuppression(KWin::EffectWindow* window);

    void loadShaderProfileFromDbus();
    void loadMotionProfileTreeFromDbus();
    void loadShaderRegistryFromDbus();
    void tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                bool reverse = false, bool holdCloseGrab = false, bool holdAddedGrab = false);
    void evictLruTextureIfOverBound();
    void warmUserTextureAsync(const QString& absolutePath);

    std::unique_ptr<DragTracker> m_dragTracker;
    std::unique_ptr<ICompositorBridge> m_compositorBridge;
    std::unique_ptr<DecorationManager> m_decorationManager;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;
    Qt::MouseButtons m_currentMouseButtons = Qt::NoButton;
    bool m_keyboardGrabbed = false;
    // Re-entrancy guard: true while captureOldWindowSnapshot's drawWindow walks
    // the chain, so paint/apply hooks behave plainly during the raw capture
    // pass (no morph quad deform / re-capture).
    bool m_capturingSnapshot = false;

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
     * Used by loadCachedSettings() to eliminate per-setting watcher boilerplate.
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

    // Drag-gate exclusion rule set — the Exclude-shaped slice of the
    // unified WindowRule store the effect mirrors over D-Bus. Filled by
    // loadWindowRuleAnimationsFromDbus's parse step (which already
    // deserialises the full rule set for the animation override path),
    // via `PhosphorWindowRule::ExclusionRules::excludeRulesFrom`. The
    // bound RuleEvaluator drives shouldHandleWindow()'s exclusion gate.
    // Declaration ORDER MATTERS — the rule set must precede (and outlive)
    // the evaluator that binds a reference to it.
    PhosphorWindowRule::WindowRuleSet m_snappingExclusionRuleSet;
    PhosphorWindowRule::RuleEvaluator m_snappingExclusionEvaluator{m_snappingExclusionRuleSet};

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

    // Animation window filtering — separate cache from the snapping/tiling
    // exclusions because the user can opt for divergent filter sets. The
    // filter gates the animation cascade BEFORE rule resolution, but a
    // rule whose match expression resolves for the window overrides the
    // filter (so a user can disable animations broadly via an app exclusion
    // AND still keep one app animated through a targeted rule). The match
    // expression sees the full per-window query (AppId / WindowClass /
    // Title / WindowRole / DesktopFile / WindowType / Pid / state flags).
    // Defaults are permissive (no filter) until D-Bus populates them;
    // matches the per-key defaults in ConfigDefaults.
    bool m_animationExcludeTransientWindows = false;
    // Notification / OSD surfaces — excluded by default (see
    // ConfigDefaults::animationExcludeNotificationsAndOsd()). Initialised
    // to the exclude default rather than the permissive value above so
    // a pre-D-Bus window event doesn't flash a shader on a notification.
    bool m_animationExcludeNotificationsAndOsd = true;
    int m_animationMinWindowWidth = 0;
    int m_animationMinWindowHeight = 0;

    // Animation exclusion rule set — the `ExcludeAnimations`-action slice
    // of the unified WindowRule store the effect mirrors over D-Bus.
    // Filled by loadWindowRuleAnimationsFromDbus's parse step (which
    // already deserialises the full rule set for the animation override
    // path), via
    // `PhosphorWindowRule::ExclusionRules::excludeAnimationsRulesFrom`.
    // The bound RuleEvaluator drives shouldAnimateWindow()'s exclusion
    // gate. Declaration ORDER MATTERS — the rule set must precede (and
    // outlive) the evaluator that binds a reference to it.
    PhosphorWindowRule::WindowRuleSet m_animationExclusionRuleSet;
    PhosphorWindowRule::RuleEvaluator m_animationExclusionEvaluator{m_animationExclusionRuleSet};

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
    bool m_cachedZoneSpanToggleMode = false;
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

    /// Monotonic per-drag generation. Bumped on every drag start. The async
    /// beginDrag reply lambda captures the generation at dispatch time and
    /// checks against the live value at reply time — if the drag has ended
    /// (or a new one started) before the reply arrives, the captured policy
    /// would otherwise be written into m_currentDragPolicy and bleed into
    /// the next drag's state. Generation-mismatched replies are discarded.
    quint64 m_dragGeneration = 0;

    // Windows floated by drag on autotile screens. The daemon emits
    // applyGeometryRequested to restore pre-autotile geometry on float,
    // but drag-to-float should keep the window where the user dropped it.
    // Entries are consumed (removed) when slotApplyGeometryRequested skips
    // the geometry restore for a drag-floated window.
    QSet<QString> m_dragFloatedWindowIds;

    // Per-window rule-cache invalidations accumulated within one event-loop turn,
    // flushed once by flushPendingRuleInvalidations(). Coalesces the double
    // invalidation a float toggle triggers (windowFloatingChanged + windowStateChanged).
    QSet<QString> m_pendingRuleInvalidations;

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
    /// Per-physId fetchVirtualScreenConfig sequence. Every fetch bumps its
    /// physId's entry; the async reply applies to m_virtualScreenDefs only if
    /// it is still the latest. Without this, two live changes in quick
    /// succession (e.g. remove-then-readd a VS) race: replies can land
    /// out-of-order and a stale payload clobbers the fresh one, leaving
    /// resolveEffectiveScreenId tagging windows with dead "physId/vs:N" ids.
    QHash<QString, uint64_t> m_vsFetchSeqPerPhysId;
    bool m_daemonReadyWindowStateProcessed = false; ///< re-entrancy guard for processDaemonReadyWindowState
    /// One-shot guard for the WindowRules rulesChanged D-Bus subscription.
    /// QDBusConnection::connect silently accepts duplicate subscriptions, so without
    /// this flag the subscription set would grow unbounded across every
    /// slotSettingsChanged broadcast (which re-runs loadCachedSettings()). Set true
    /// after the first successful connect from continueDaemonReadySetup().
    bool m_windowRulesSubscribed = false;

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

    // Windows withheld from compositing between windowAdded and the frame
    // their snap-restore / autotile reposition lands — see RestoreSuppression.
    // paintWindow draws nothing for a window present here. Entries are
    // erased on settle, on a negative resolve, on the deadline, and on
    // window close/delete.
    QHash<KWin::EffectWindow*, RestoreSuppression> m_restoreSuppress;

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

    /// Handle daemon signal when the per-event motion-profile tree
    /// changes (a per-event animation duration was edited). Re-fetches
    /// `motionProfileTree` so per-event durations apply without a
    /// logout/login. Dedicated signal (not settingsChanged) so the
    /// Settings app's change detection is unaffected.
    void slotMotionProfileTreeChanged();

    /// Fetch the unified WindowRule store via `org.plasmazones.WindowRules.
    /// getAllRules`, filter to rules carrying an OverrideAnimation* action,
    /// and forward them to the shader manager — the sole source of per-window
    /// animation overrides. Called once at bringup; the bringup also
    /// subscribes to the interface's `rulesChanged` signal (via a debounce
    /// timer — see m_animationRulesRefreshDebounce) so a settings-UI edit
    /// takes effect without restarting the effect.
    void loadWindowRuleAnimationsFromDbus();

    /// D-Bus signal handler for `WindowRules.rulesChanged`. Re-arms the
    /// debounce timer rather than refetching the full ruleset on every
    /// signal — the daemon emits one signal per per-rule mutation, so a
    /// 50-rule batch edit would otherwise drive 50 full-ruleset fetches
    /// and parses. A 50ms single-shot debounce coalesces the burst into a
    /// single fetch at the trailing edge.
    void slotWindowRulesChanged();
};

} // namespace PlasmaZones
