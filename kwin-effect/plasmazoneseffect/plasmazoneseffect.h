// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorCompositor/DecorationManager.h>
#include <PhosphorCompositor/ICompositorBridge.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorCompositor/TriggerParser.h>

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/Curve.h> // beginShaderTransition's progressCurve param
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h> // resolveEventMotionProfile's return type
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleSet.h>
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
#include <QScopeGuard>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QHash>
#include <QPointer>
#include <QRect>

#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "transitions/shadertransitionmanager.h"
#include "transitions/desktoptransitionmanager.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include "shader_resolve.h"
#include "types.h"

namespace KWin {
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

// Plasmashell notification stacking makes KWin emit spurious
// minimizedChanged(true) events on tiled windows, with the matching
// unminimize ~1-2 ms later. Two suppressions key off this window and MUST
// agree on its width: the autotile minimize→float debounce
// (autotilehandler/signals.cpp) and the minimize shader-event
// spurious-pair cancel (plasmazoneseffect/daemon_apply.cpp,
// slotWindowMinimizedChanged). Shared here so the two can never desync.
inline constexpr int kSpuriousMinimizePairMs = 75;

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

    // KWin 6.7 dropped the explicit presentTime parameter from the prePaint
    // hooks; effects now self-source time (our CompositorClock samples
    // std::chrono::steady_clock, matching KWin's own AnimationEffect clock).
    void prePaintScreen(KWin::ScreenPrePaintData& data) override;
    void postPaintScreen() override;
    void prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data) override;
    // Per-window borders are rendered by routing the redirected window through
    // the offscreen border MapTexture shader (see the drawWindow override below +
    // decorations.cpp), NOT here. paintScreen is overridden solely for the
    // full-screen desktop transitions (the virtual-desktop switch and the
    // show-desktop peek, which share the same path): while one is live,
    // m_desktopTransition.paintOutput draws the two-texture blend for that output
    // and we skip the normal scene; otherwise this chains straight through.
    void paintScreen(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, int mask,
                     const KWin::Region& deviceRegion, KWin::LogicalOutput* screen) override;
    void paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                     KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                     KWin::WindowPaintData& data) override;
    // Border render path (implemented in decorations.cpp). A static bordered window
    // is rendered through the offscreen border shader PASSIVELY here: we bind the
    // border shader + push its uniforms, then let OffscreenEffect::drawWindow
    // re-blit the redirected FBO through it on EVERY composite (idle included),
    // with no FBO re-render and no forced per-frame repaints — the
    // KDE-Rounded-Corners model. paintWindow no longer touches the border.
    void drawWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, KWin::EffectWindow* w,
                    int mask, const KWin::Region& deviceRegion, KWin::WindowPaintData& data) override;
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

    // Minimize shader event (both directions, spurious-pair suppression)
    // plus the snap-mode minimize/unminimize float tracking tail
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
     * menu/popup/tooltip family). Single source of truth behind
     * shouldHandleWindow()'s structural clause, notifyWindowActivated()'s
     * focus-tracking filter and classifyWindowKind(), so they can never drift
     * (discussion #461 item 11).
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
     * A Rule carrying any effect-consumed (Tag::Effect)
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
     * @brief Per-window gate for the border / decoration pass.
     *
     * Modeled on shouldAnimateWindow rather than shouldHandleWindow so the
     * transient family is a real toggle (m_decorationExcludeTransientWindows):
     * with it off the effect draws borders onto dialogs / popups. Rejects the
     * always-wrong surfaces (own overlay / editor, xdg-portal, plasma-shell,
     * special / desktop / dock / fullscreen / skipSwitcher, notification / OSD),
     * honours the same user Exclude rule slice shouldHandleWindow uses (so an
     * excluded app stays undecorated), then applies the transient toggle and the
     * min-size threshold. Defaults preserve the prior behavior (transient on,
     * size off), so a default config decorates exactly what it did before.
     */
    bool shouldDecorateWindow(KWin::EffectWindow* w) const;

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
     * @brief Whether KWin is currently in the show-desktop / peek state.
     *
     * Workspace::activateWindow() cancels show-desktop the moment any hidden
     * window is activated, so every activation path the EFFECT ITSELF drives
     * must bail while this is true or a peek collapses on the first cursor move
     * or engine relayout. Both origins are covered: effect-local paths that
     * never touch the bus (focus-follows-mouse in snaphandler and
     * autotilehandler) and daemon-relayed ones (retile reactivation, unfloat
     * refocus, the snap engine's activate requests, the autotile engine's
     * post-relayout focus flush, and the compositor bridge's activateWindow).
     * For the relayed ones the effect cannot tell a user-initiated daemon
     * request (a keyboard navigation shortcut) from an engine-initiated one —
     * both arrive on the same D-Bus signals — so all of them are gated; only
     * KWin-native activation (clicking a surface) ends a peek.
     */
    static bool isShowingDesktop();

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
     * @brief Free-float geometry to CAPTURE for @p w, correcting for maximize/fullscreen.
     *
     * A maximized or fullscreen window's frameGeometry() is the full-monitor rect.
     * Capturing THAT as a window's pre-tile / pre-snap / float-back geometry makes it
     * restore to a maximized size when it later floats. Returns @p fallback unless @p w
     * is maximized/fullscreen, in which case it returns the pre-maximize / pre-fullscreen
     * RESTORE rect (a sane free size), falling back to @p fallback again if that restore
     * rect is empty. Shared by the snap and autotile capture paths, which write the SAME
     * daemon free-geometry store.
     */
    static QRectF freeGeometryForCapture(KWin::EffectWindow* w, const QRectF& fallback);

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
    /// Drop @p windowId's zone entry from the NavigationHandler zone cache —
    /// the source of the IsSnapped / Zone rule-match fields — re-resolving the
    /// window's rules when an entry was actually removed. Unsnap paths call
    /// this (via SnapHandler::clearWindowSnapped) so placement-scoped rules
    /// see the new state immediately instead of waiting for a daemon
    /// broadcast that some paths (drag-out unsnap) never send.
    void clearWindowZone(const QString& windowId);
    /// Build a window-rule match query for @p w with the effect's runtime
    /// placement state (floating / snapped / zone) threaded into the free
    /// `ruleQueryFor` builder. Use this at EVERY rule-evaluation site so
    /// IsFloating / IsSnapped / Zone resolve uniformly; the free builder stays
    /// KWin-only and can't reach the effect's caches.
    PhosphorRules::WindowQuery ruleQuery(KWin::EffectWindow* w) const;

    /// Resolve the animation rule-action verdict for @p w, skipping the per-frame
    /// `ruleQuery(w)` build (≈30 KWin accessor reads) when the evaluator
    /// already has a cached verdict for @p windowId. Peek-then-build: a cache hit
    /// returns the memoised actions directly; a miss builds the query and resolves
    /// (caching the result). An empty windowId or a windowless query yields empty
    /// actions (no slots) WITHOUT caching, matching the resolvers' old
    /// short-circuit (avoids churning the cache for sub-surfaces / proxies). The
    /// per-frame opacity / border resolvers consume the returned ResolvedActions.
    PhosphorRules::ResolvedActions resolveRuleActions(KWin::EffectWindow* w, const QString& windowId) const;

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

    /// The O(1) reverse-cache half of findWindowById, WITHOUT the fuzzy appId fallback.
    ///
    /// findWindowById's fallback walks the whole stacking order building a composite id per
    /// window, which is the right thing when a cross-session restore has changed the UUID but
    /// kept the appId. It is the wrong thing on a hot path: the per-frame repaint drivers and
    /// the per-pointer-motion hover driver all re-check `getWindowId(sw) == key` immediately
    /// and discard a fuzzy match anyway, so the walk is an O(N) string-building nothing on
    /// every frame. Worse, a close-grabbed window (deleted, but held in the stacking order by
    /// the close shader) is rejected by the exact check AND skipped by the fuzzy walk, so the
    /// walk cannot even produce a result for the one case that reaches it. Those callers want
    /// exactness; this gives it in one hash lookup.
    KWin::EffectWindow* findWindowByIdExact(const QString& windowId) const;

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
    /// Report a screen's current virtual desktop to the daemon (Plasma 6.7
    /// per-output virtual desktops). Deduplicates against m_lastScreenDesktop and
    /// only fires when the daemon service is registered.
    void reportScreenDesktop(const QString& screenId, int desktop);
    QString getWindowScreenId(KWin::EffectWindow* w) const;
    /// Resolve the KWin output a window sits on by POSITION (the output whose
    /// geometry contains the window centre), falling back to w->screen() only
    /// when no output contains the centre. Never trust w->screen() first: KWin
    /// can assign a window the wrong one of two identical-model outputs
    /// (Discussion #724). Shared by getWindowScreenId and the activation-time
    /// desktop report in notifyWindowActivated.
    KWin::LogicalOutput* windowOutput(KWin::EffectWindow* w) const;
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
    friend class DesktopTransitionManager;
    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class instances
    // ═══════════════════════════════════════════════════════════════════════════════
    std::unique_ptr<AutotileHandler> m_autotileHandler;
    std::unique_ptr<SnapHandler> m_snapHandler;

    QHash<QString, WindowDecoration> m_windowDecorations; // windowId → border

    // Smoothed focus value per window, so uSurfaceFocused RAMPS between 0 and
    // 1 on a focus change instead of snapping — every focus-tracking pack
    // (glow dim, border dim, focus-fade) then transitions softly. Kept in its
    // OWN map, NOT on WindowDecoration, because slotWindowActivated rebuilds
    // every WindowDecoration via updateAllDecorations on each focus change,
    // which would reset an in-flight ramp. `value < 0` is the uninitialised
    // sentinel (first decorate snaps to the current state, no fade on
    // appearance); `lastMs` dedupes the per-frame advance across the chain's
    // packs. windowSurfaceAnimates keeps the window repainting while a value
    // is inside its near-0/near-1 thresholds, so the ramp runs to completion.
    struct FocusFadeState
    {
        float value = -1.0f;
        qint64 lastMs = -1;
    };
    QHash<QString, FocusFadeState> m_focusFade;
    // Live focus cross-fade duration (ms) for the uSurfaceFocused ramp (border
    // colour mix + the focus-fade content pack). A STANDALONE decoration
    // setting ("focusFadeDuration", loaded in loadCachedSettings), deliberately
    // independent of the window animation system: the fade is a decoration
    // cross-fade, not a window animation, so disabling animations or retuning
    // the window.focus event no longer snaps or retimes it. 0 = instant.
    // Seeded to the shared default until the async settings load lands.
    int m_focusFadeDurationMs = PhosphorCompositor::DecorationDefaults::FocusFadeMs;
    // Resolve the fully-cascaded motion Profile (curve + duration) for
    // @p profilePath: global animator profile → category "All" → per-node
    // motion-tree overrides → per-window Rule override. This is the single SSOT
    // for the per-event timing cascade, shared by all four per-event timing
    // consumers — the animator-driven geometry path (applyWindowGeometry), the
    // time-driven shader path (tryBeginShaderForEvent), the desktop switch (the
    // desktopChanged handler) and the show-desktop peek (the
    // showingDesktopChanged handler) — so each honours the same global → All →
    // node resolution. Pass a windowless @p query (hasWindow() false) + empty
    // @p windowId for events with no per-window rule scope (both desktop legs);
    // the Rule layer is then skipped and only the tree cascade applies.
    //
    // The returned DURATION is clamped into [Limits::MinAnimationDurationMs,
    // Limits::MaxAnimationDurationMs] — callers do not need to re-clamp, and
    // applyWindowGeometry depends on it (WindowAnimator's own clampProfile uses a
    // looser envelope). A null returned CURVE means linear iTime, but in practice
    // it is unreachable after settings load: daemon_bringup builds the animator's
    // global curve with CurveRegistry::create(), which never returns null.
    //
    // The duration does NOT bound a stateful (spring) curve, which derives its
    // lifetime from settleTime() instead — see AnimationLimits.h.
    PhosphorAnimation::Profile resolveEventMotionProfile(const QString& profilePath,
                                                         const PhosphorRules::WindowQuery& query,
                                                         const QString& windowId) const;
    // Cap on the per-frame ramp delta. A window at rest (value pinned at 0 or 1)
    // stops being force-repainted by windowSurfaceAnimates, so its FocusFadeState
    // `lastMs` goes stale; without this cap the first frame after a focus change
    // would see a multi-second `now - lastMs` and jump the whole ramp in one step
    // (an instant snap instead of a fade). A live window's real frame delta is
    // always well under this, so the cap only tames the resume-after-idle case.
    static constexpr qint64 kFocusFadeMaxStepMs = 50;

    // Live system colours that a `BorderColorToken::Accent` sentinel in a
    // border-colour rule resolves to. The sentinel tracks the system colour
    // scheme per focus state: the focused (active) slot adopts the accent /
    // highlight colour, the unfocused (inactive) slot adopts the inactive
    // colour. Both are pushed from the daemon, which tracks the Plasma colour
    // scheme; invalid until the first push (sentinel then yields no colour).
    // See resolveWindowAppearance.
    QColor m_borderAccentColor;
    QColor m_borderInactiveColor;

    // Config-backed window-decoration appearance default. Window appearance
    // resolves as: this default (each slot gated by its scope token) filling the
    // slots the user's per-window rules left unset — rules still win per slot.
    // Pushed from the daemon over the settings D-Bus wire in loadCachedSettings,
    // re-fetched on every settingsChanged. The two colour strings carry a hex
    // "#AARRGGBB" OR the "accent" sentinel (resolved to m_borderAccentColor /
    // m_borderInactiveColor at merge time, mirroring the rule colour path).
    // Scope tokens live in PhosphorCompositor::WindowAppearanceScope: "tiled"
    // (snapped OR autotile-managed), "normal" (Normal type AND not transient),
    // "all" (every window). Defaults match ConfigDefaults::windowBorderScope().
    struct WindowAppearanceDefault
    {
        bool showBorder = false;
        QString borderScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
        int borderWidth = 0;
        int borderRadius = 0;
        QString activeColor;
        QString inactiveColor;
        bool hideTitleBar = false;
        QString titleBarScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
        // Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*),
        // rendered by the built-in "opacity-tint" pack in easy mode. The tint
        // colour carries hex or the accent sentinel like the border colours.
        bool showOpacityTint = false;
        QString opacityTintScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
        double opacity = 1.0;
        double tintStrength = 0.0;
        QString tintColor;
    };
    WindowAppearanceDefault m_windowAppearanceDefault;

    /// True when a config-default border, hidden title bar, or opacity+tint
    /// layer could apply to some window. Placement-change reconciliation
    /// (invalidateRuleCacheForStateChange / flushPendingRuleInvalidations) must
    /// run whenever this is true even with an empty rule set, because a config
    /// default is scope-gated on placement state (isSnapped / isTiled /
    /// normal), so a snap/unsnap changes whether it applies.
    bool hasWindowAppearanceDefault() const
    {
        return m_windowAppearanceDefault.showBorder || m_windowAppearanceDefault.hideTitleBar
            || m_windowAppearanceDefault.showOpacityTint;
    }

    /// True when the decoration profile tree could decorate some window (a
    /// baseline chain or any per-path override exists). Placement-change
    /// reconciliation must also run when THIS is true: surface paths are
    /// placement-derived (window.snapped / window.floating / ...), so a
    /// snap/unsnap changes which chain resolves even with no rules and no
    /// config-default border — the gate being blind to tree packs left
    /// chain-decorated windows undecorated from a drag-start unsnap until an
    /// unrelated push rebuilt them.
    bool hasDecorationTreeContent() const
    {
        // NOTE: DecorationProfileTree is PhosphorSurfaceShaders', not
        // PhosphorAnimation::ProfileTree — it has no hasAnyOverride(). Not a hot
        // path (this runs on reconcile, not per frame), so the QStringList copy
        // is fine here.
        //
        // enabledChain(), the SAME accessor updateWindowDecoration renders from, not
        // effectiveChain(): the latter keeps packs the user toggled off, so a tree whose
        // baseline chain exists but has every pack disabled reported "content" and made
        // every snap / float / zone change do full per-window reconcile work for a
        // configuration that can decorate nothing.
        return !m_decorationTree.resolve(QString()).enabledChain().isEmpty()
            || !m_decorationTree.overriddenPaths().isEmpty();
    }

    /// Evaluate a config-default appearance scope token against a live window.
    /// "tiled" → the window is snapped or autotile-managed; "normal" → its
    /// window type is Normal and it is not transient; "all" → always true;
    /// any other token → false (the default contributes nothing).
    bool windowMatchesAppearanceScope(const QString& scope, KWin::EffectWindow* w, const QString& windowId) const;

    /// Resolve @p windowId's effective window-decoration appearance: the user's
    /// per-window rule appearance (when any rules exist) with every slot it left
    /// unset filled from the config default in m_windowAppearanceDefault, each
    /// default slot gated by its scope token. Rules win per slot. Used by both
    /// the border draw path and the title-bar reconcile so config-backed
    /// defaults apply even with an empty rule set.
    ResolvedWindowAppearance resolveEffectiveWindowAppearance(KWin::EffectWindow* w, const QString& windowId) const;

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
    //
    // The window pointer rides along so the debounced flush can run the
    // shouldHandleWindow exclusion gate ONCE per flush instead of on every
    // geometry tick — during animated geometry (retiles, morphs, interactive
    // resize) the per-tick gate was an uncached rule resolve plus a full
    // ruleQuery build, hundreds of times per second (discussion #816). The
    // decoration resync deliberately stays PER TICK in the stash lambda (see
    // window_lifecycle.cpp): it is cheap, and deferring it let a re-decorated
    // title bar flash for the throttle window. QPointer auto-nulls if the
    // window dies before the flush; the flush skips those entries.
    struct PendingFrameGeometry
    {
        QRect geometry;
        QPointer<KWin::EffectWindow> window;
    };
    QHash<QString, PendingFrameGeometry> m_pendingFrameGeometry;
    QTimer* m_frameGeometryFlushTimer = nullptr;
    void flushPendingFrameGeometry();

    /// Debounce timer for `Rules.rulesChanged`. Single-shot, 50ms;
    /// timeout fires `loadRuleAnimationsFromDbus`. Re-armed on every
    /// `slotRulesChanged` invocation so a burst of per-rule mutations
    /// (a 50-rule batch edit emits 50 signals) collapses into a single
    /// `getAllRules` fetch at the trailing edge.
    QTimer m_animationRulesRefreshDebounce;

    /// Wire the DecorationManager into the effect: the windowDecorationRestored
    /// connection. Defined in decorations.cpp with the rest of the decoration code;
    /// called once from the constructor.
    void setupDecorationManager();

    // Interactive-resize latch. windowStartUserMovedResized fires once with
    // isUserResize() true when an edge drag begins; we capture the pre-resize
    // frame so windowFinishUserMovedResized can report the before/after geometry
    // to the daemon for neighbour reflow (GitHub #652). The resize-vs-move
    // identity is the existing m_resizingWindow latch; this carries only the
    // baseline geometry it lacks. The daemon's frame shadow can't serve as the
    // baseline — it updates mid-drag via the debounced setFrameGeometry push.
    QRect m_resizeStartGeometry;
    void notifyWindowResized(KWin::EffectWindow* w, const QRect& oldGeometry);

    void updateWindowDecoration(const QString& windowId, KWin::EffectWindow* w);

    /// Poll-defer a decorated, minimized window's teardown while an animation
    /// still paints it. A minimized window is only isVisible() while some
    /// effect holds an EffectWindowVisibleRef on it — KWin's magic lamp /
    /// squash minimize animations, or our own minimize transition. Tearing
    /// the decoration down at that moment (updateWindowDecoration's
    /// isMinimized() gate) yanks the OffscreenEffect redirect and its GL
    /// working set out from under the in-flight animation: the mid-lamp
    /// freeze and unbound-sampler black smears of discussion #816. The poll
    /// re-enters updateWindowDecoration; once the animation drops its ref the
    /// window stops being visible and the normal undecorate proceeds (or, if
    /// the window unminimized meanwhile, the normal refresh path re-resolves).
    /// The deferral's lifetime is bounded by WHOEVER holds a visible ref, not
    /// only minimize animations: a thumbnail / overview effect keeping a
    /// minimized window visible extends the poll (and the kept decoration)
    /// for the ref's whole lifetime, which is the correct trade — the window
    /// is being painted, so its decoration staying live is consistent, and
    /// each poll tick is a lookup plus an early return. Keyed set prevents
    /// timer pileup when decoration sweeps re-enter while a poll is already
    /// armed; stale entries self-drain (the timer removes its own entry, and
    /// a re-entry against a since-cleared decoration map is a no-op that does
    /// not re-arm), so bulk teardown paths need no explicit clear. Defined in
    /// decorations.cpp.
    void deferDecorationTeardownWhileAnimated(const QString& windowId);
    QSet<QString> m_animatedDecoTeardownPending;

    /// windowHint: the EffectWindow when the caller still holds it and the
    /// window is already deleted (close / delete paths) — findWindowById
    /// cannot resolve a deleted id, and without the pointer the GL release
    /// (setShader(nullptr) + unredirect) is skipped, leaving the corpse
    /// redirected with a shader whose samplers reference textures this very
    /// function destroys (unbound sampler = opaque black flash on close).
    /// @param keepSurfaceState when true, the window's SurfaceMultipassState (its
    ///        capture, static-prefix, composite and buffer textures + framebuffers)
    ///        SURVIVES the removal. Set by updateWindowDecoration's remove-first
    ///        step: a decoration REFRESH re-resolves the same window's chain, and
    ///        the GL targets are keyed on (size, chain) — which the fold re-checks
    ///        itself — so tearing them down and immediately reallocating them is
    ///        pure churn. updateAllDecorations runs on every focus change, so
    ///        without this every focus change would free and reallocate every
    ///        decorated window's whole GL working set and cold-start both caches.
    ///        Genuine teardown (close, delete, undecorate) leaves it false.
    void removeWindowDecoration(const QString& windowId, KWin::EffectWindow* windowHint = nullptr,
                                bool keepSurfaceState = false);

    /// Free a window's composite / capture / prefix / buffer GL targets — unless a
    /// shader transition is mid-flight on it, which still samples them. Every
    /// decoration TEARDOWN must route through here, or it will destroy the composite a
    /// live animation is drawing from (the compositeTexId-0 class of bug). @p target
    /// must be the EXACT window, never a fuzzy same-app sibling.
    ///
    /// Three other sites erase m_surfaceMultipass directly, and each is deliberate:
    ///   - lifecycle.cpp's surface-pack hot-reload clears the WHOLE map, because every
    ///     compiled pack is about to be recompiled and no composite survives it;
    ///   - lifecycle.cpp's windowDeleted backstop, which runs after the window is gone
    ///     and there is nothing left to animate;
    ///   - surface_capture.cpp's ensureSurfaceTargets, which on an allocation failure
    ///     erases the half-built state it just failed to allocate and returns false;
    ///     its caller abandons the fold immediately, so a transition loses its layer
    ///     for one frame rather than sampling a freed texture.
    /// Nothing else may.
    void releaseSurfaceState(const QString& windowId, KWin::EffectWindow* target);

    /// The EXACT window a decoration belongs to: an exact-id live match, else the frozen
    /// reverse mapping (which resolves a DELETED window and survives a close
    /// transition), else @p hint. Shared by every teardown path, because a fuzzy
    /// same-app sibling handed to the GL release tears down the wrong window.
    KWin::EffectWindow* resolveDecorationTarget(const QString& windowId, KWin::EffectWindow* hint);

    /// Hand the window's OffscreenEffect redirect and shader slot back to KWin and
    /// damage what the decoration covered. Skipped by a decoration REFRESH (which is
    /// about to re-assert the same redirect, so tearing it down only makes KWin free
    /// and reallocate its OffscreenData); run by the paths where a refresh discovers
    /// the window is no longer decoratable, or it would be left redirected and shaded
    /// with no decoration behind it. No-op while a transition owns the slot.
    void releaseDecorationGl(KWin::EffectWindow* w, int outerPadding);
    /// SHARED placement-flip funnel: re-resolve a window's decoration
    /// update-or-remove in the SAME turn after its snapped / tiled /
    /// floating state flipped. Both engines route through this (snap's
    /// clearWindowSnapped, autotile's applyFloatCleanup) so neither can
    /// regress into the teardown-now-rebuild-later shape that blanked
    /// every pack at drag start. Callers flip their engine facts first.
    void reconcileDecorationOnPlacementFlip(const QString& windowId);
    void updateAllDecorations();
    void clearAllDecorations();

    // ── Offscreen border shader (flush rounded corners + per-window outline) ──
    //
    // A bordered window is rendered THROUGH a MapTexture fragment shader that
    // evaluates one rounded-rect SDF over the frame to clip the corners AND draw
    // the `width` outline band, using KWin's own MVP so it is flush over the
    // server-side decoration (the prior scene-graph OutlinedBorderItem composited
    // UNDER the decoration and looked inset). Same path for decorated + borderless
    // windows; it clips the COMPOSITED texture, never the client surface, so the
    // window's own BorderRadius is left untouched (setting it inset the corner and
    // clipped the inner surface). Coordinated with the per-window animation
    // transition on the SAME OffscreenEffect setShader() slot — see decorations.cpp.

    /// Compile-on-first-use + cache the surface shader pack @p packId (window
    /// border / rounded corners / glow / …) from data/surface via the
    /// SurfaceShaderRegistry, keyed by pack id in m_compiledPacks. Returns the
    /// cached CompiledSurfacePack (whose `shader` is nullptr when the pack is missing
    /// or its compile failed — decoration then no-ops for that pack), or nullptr only
    /// when there is no GL context yet.
    /// The whole cache is cleared on a SurfaceShaderRegistry hot-reload
    /// (effectsChanged) and on teardown.
    ///
    /// @p profile supplies the pack's parameter overrides (parameters[packId])
    /// merged over the pack's declared defaults; baked into the compiled pack's
    /// customParams/customColors VALUES at first compile (the cache is pack-keyed,
    /// not pack+params-keyed — see CompiledSurfacePack).
    CompiledSurfacePack* compiledPack(const QString& packId, const PhosphorSurfaceShaders::DecorationProfile& profile);

    /// Populate the surface-shader registry's search paths (the bundled
    /// ${XDG_DATA_DIRS}/plasmazones/surface dirs + the user override) on first
    /// use. One-shot: the registry's live-reload watcher then tracks pack edits.
    void ensureSurfaceRegistryPaths();

    /// Decide and apply the desired offscreen shader for @p windowId / @p w:
    ///   • a transition is active (animation owns the slot) → leave it alone;
    ///   • else the window has a border in m_windowDecorations → redirect + set the
    ///     border shader, marking the WindowDecoration `shaderApplied`;
    /// There is deliberately NO teardown branch here: a window that should no longer be
    /// decorated has its redirect released by the teardown paths (releaseDecorationGl),
    /// not by this reconcile, which only ever ADDS. Idempotent and safe to call from
    /// updateWindowDecoration / removeWindowDecoration / transition end. Never
    /// unredirects a window the animation system owns.
    void reconcileDecorationShader(const QString& windowId, KWin::EffectWindow* w);

    /// Per-frame uniform push for a bordered window painted through @p pack's
    /// surface shader. Sets the geometry uniforms (uSurfaceSize, uSurfaceFrameTopLeft,
    /// uSurfaceFrameSize) from the window's frame/expanded geometry × @p scale, the
    /// logical-to-device @p scale itself (uSurfaceScale), the focus flag
    /// (uSurfaceFocused), plus @p packId's customParams/customColors — seeded from
    /// THIS window's resolved values (WindowDecoration::packParamValues) with the
    /// compiled pack's baked baseline as fallback. @p wb is the window's border
    /// entry. The built-in "border" base pack needs no special-casing here:
    /// updateWindowDecoration routes the window's resolved border appearance
    /// into packParamValues by param id, the same path every pack's overrides
    /// take.
    /// Writes onto the ALREADY-BOUND pack shader: every caller (drawWindow's idle
    /// blit, renderSurfaceChain's transition capture, renderSurfaceChainComposite's
    /// per-pack fold) owns the KWin::ShaderBinder, has already resolved @p pack
    /// and the border entry, and has ruled out a transition owning the slot, so
    /// this neither binds/unbinds nor re-validates the window.
    /// @p texturePaddingLogical: outer margin (logical px) baked into the
    /// TARGET texture's canvas — non-zero only on the padded composite path,
    /// where the geometry uniforms must describe the inflated space.
    /// @p timeSec: the clock the FOLD decided on, in seconds. Threaded in rather than
    /// sampled here because a chain that Decorations.Performance has paused is handed a
    /// frozen clock — see SurfaceMultipassState::pausedAtMs / timeOffsetMs. Sampling live
    /// here would let a paused window's packs disagree with the frozen composite they are
    /// folding into.
    /// @p foldCursor is the cursor the FOLD resolved (SurfaceFoldPlan::foldCursor) — a
    /// global point, or kCursorOutside when the pointer is elsewhere or the chain is
    /// paused. Handed in rather than re-derived, because the fold keys its cache on this
    /// exact value and the shader must be given the same one.
    void pushBorderUniforms(KWin::EffectWindow* w, const WindowDecoration& wb, const QString& packId,
                            const CompiledSurfacePack& pack, qreal scale, float timeSec, const QPointF& foldCursor,
                            qreal texturePaddingLogical = 0.0, const QString& windowId = {});

    /// Advance the per-window smoothed focus value (m_focusFade) toward the
    /// hard 0/1 target and return it, so focus changes cross-fade instead of
    /// snapping. Called by pushBorderUniforms only for a pack that reads focus.
    /// Uses the pinned per-frame clock, so repeated same-frame calls (a chain
    /// with several focus-reading packs) are exact no-ops — the ramp advances
    /// at most once per frame.
    float advanceFocusFade(const QString& windowId, bool focused);

    /// Resolves a pack id to its compiled program, compiling on a cache miss. The fold
    /// memoises the decoration-profile lookup behind this, so the input side takes it
    /// as a callable rather than resolving the tree a second time.
    ///
    /// A NON-OWNING reference to the caller's lambda, not a std::function. The fold's
    /// resolver captures three pointers (24 bytes), which is past libstdc++'s 16-byte
    /// small-object buffer — so every conversion to std::function HEAP-ALLOCATED, and the
    /// fold converts twice. At eight decorated windows across two outputs at 60Hz that is
    /// some four thousand malloc/free pairs a second, added by a refactor that shipped
    /// inside a performance PR. The callee only ever invokes it during the call, so a
    /// borrowed reference is all it ever needed.
    class CompiledPackResolver
    {
    public:
        template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, CompiledPackResolver>>>
        CompiledPackResolver(F&& fn) // NOLINT(google-explicit-constructor): behaves as a callable
            : m_ctx(static_cast<void*>(std::addressof(fn)))
            , m_invoke([](void* ctx, const QString& packId) -> CompiledSurfacePack* {
                return (*static_cast<std::remove_reference_t<F>*>(ctx))(packId);
            })
        {
        }
        CompiledSurfacePack* operator()(const QString& packId) const
        {
            return m_invoke(m_ctx, packId);
        }

    private:
        void* m_ctx;
        CompiledSurfacePack* (*m_invoke)(void*, const QString&);
    };

    /// (Re)allocate a window's composite / capture / per-pack buffer targets for the
    /// current size, scale and chain, dropping every cache an allocation makes stale.
    /// False means an allocation FAILED and the window's surface state has been erased
    /// — @p state is dangling and the caller must abandon the fold. Defined in
    /// surface_capture.cpp.
    bool ensureSurfaceTargets(const QString& windowId, SurfaceMultipassState& state, const QStringList& chain,
                              const QSize& textureSize, qreal captureScale,
                              const CompiledPackResolver& compiledPackLazy);

    /// Decide what a fold can reuse — the animation gate, the clock, the cacheable head
    /// of the chain, and the state keys — before it does any work. Defined in
    /// surface_capture.cpp, beside the rest of the fold's input side.
    SurfaceFoldPlan planSurfaceFold(KWin::EffectWindow* w, const QString& windowId, const WindowDecoration& deco,
                                    const QStringList& chain, SurfaceMultipassState& state,
                                    const CompiledPackResolver& compiledPackLazy, bool inTransition);

    /// Capture the raw window surface for the fold to read as uTexture0. The single
    /// most expensive step of the fold — it re-enters KWin's whole draw chain — and the
    /// reason SurfaceMultipassState::captureValid exists. Defined in surface_capture.cpp.
    void captureWindowSurface(KWin::EffectWindow* w, SurfaceMultipassState& state, const QRectF& logicalGeometry,
                              qreal captureScale, bool intoCaptureTex, bool captureCacheable, qreal captureOpacity);

    /// Render the window's active surface-layer stack into the window's
    /// per-window ping-pong FBO chain (`m_surfaceMultipass`, shared with the
    /// idle path) and return the texture holding the final composited surface,
    /// or nullptr when the window has no active surface layers (the caller then
    /// animates the bare `uTexture0`). @p transition supplies only its
    /// `cached->shader` as the capture-restore shader. Called once per animated frame
    /// from paintWindow's transition branch BEFORE the animation draw: the
    /// returned texture is bound as `uSurfaceLayer` so the animation composites
    /// over the layered surface (border / rounded corners, future tint/glow) and
    /// the border stays visible through the whole transition.
    ///
    /// Layer 0 is the border: the raw window is rendered through the border
    /// shader into the chain via OffscreenData (mirrors captureOldWindowSnapshot,
    /// reusing the existing border shader + its MVP vertex path), so it shares
    /// uTexture0's layout. Additional layers chain as passthrough-quad FBO→FBO
    /// blits (ping-pong). Implemented in surfacelayers.cpp.
    KWin::GLTexture* renderSurfaceChain(ShaderTransition& transition, KWin::EffectWindow* w, qreal scale);

    /// Blit the scene BEHIND @p w (everything painted below it this frame)
    /// from the live render target into the window's backdropTex, over the
    /// SAME padded canvas renderSurfaceChainComposite uses — texel-aligned
    /// so packs sample composite and backdrop with one uv. Called from
    /// paintWindow for needsBackdrop chains, live windows only (the close
    /// path reuses the frozen composite and must never re-capture).
    /// animatedFrame: where the animation is DRAWING the window this frame
    /// (WindowAnimator's current rect, or a morph transition's interpolated
    /// rect), in logical frame-rect terms. When valid, the blit SOURCE
    /// follows it (scaled into the rest-rect-sized canvas) so a frost/glass
    /// pane shows the scene behind the moving quad instead of behind the
    /// resting rect. Invalid = capture at the live geometry.
    void captureWindowBackdrop(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                               KWin::EffectWindow* w, const WindowDecoration& wb,
                               const QRectF& animatedFrame = QRectF());

    /// Fold @p w's decoration chain into a per-window ping-pong composite, and return the
    /// texture holding the result (null on no decoration / allocation failure). drawWindow
    /// presents it through surfacePresentShader().
    ///
    /// Captures the raw window surface, then folds each pack over the running composite: the
    /// pack's buffer passes run first (sampling the composite), then its main pass runs as a
    /// fullscreen FBO draw — composite on unit 0, its own buffers as iChannels — into the
    /// other slot. MUST be driven from paintWindow: the capture re-enters
    /// effects->drawWindow.
    ///
    /// Three caches decide how much of that actually runs on a given frame, and
    /// planSurfaceFold (surface_capture.cpp) owns all three: the window capture is
    /// damage-gated, the leading run of packs that do not vary per frame is folded once and
    /// reused, and a chain where NOTHING varies per frame returns its previous composite
    /// untouched.
    ///
    /// @p captureRestoreShader: the shader to hand the OffscreenEffect slot back to after
    /// the raw capture (null = surfacePresentShader(), the rest path's redirect; transitions
    /// pass their animation shader).
    KWin::GLTexture* renderSurfaceChainComposite(KWin::EffectWindow* w, qreal scale,
                                                 KWin::GLShader* captureRestoreShader = nullptr);

    /// Lazily-compiled passthrough shader that samples a bound texture (uFinal)
    /// at vTexCoord and writes it verbatim. Used as the redirect shader for a
    /// multi-pack window so OffscreenData::paint presents the pre-composited
    /// final FBO at window geometry. nullptr if the one-shot compile failed.
    KWin::GLShader* surfacePresentShader();
    std::unique_ptr<KWin::GLShader> m_surfacePresentShader; ///< compiled passthrough present shader
    int m_surfacePresentFinalLoc = -1; ///< uFinal sampler location on the present shader
    int m_surfacePresentOpacityLoc = -1; ///< uOpacity (final modulation) location on the present shader
    bool m_surfacePresentFailed = false; ///< latch a failed present-shader compile
    /// One-shot latch for the capture-time opacity fallback warning (the
    /// opacity-tint pack failed to compile). The condition is pack-level and
    /// the fold runs per window per frame, so an unlatched warning would spam
    /// the journal at vsync rate. Reset alongside the compile cache on a
    /// registry hot-reload (effectsChanged) so a fixed pack that breaks again
    /// warns again.
    bool m_opacityTintFallbackWarned = false;

    /// The shared clock behind the surface contract's `iTime`, in integer milliseconds,
    /// relative to an epoch captured at first use. Monotonic (steady_clock). Every
    /// per-window clock is derived from this one by subtracting the time that window spent
    /// not animating — see SurfaceMultipassState. The seconds-valued sibling this used to
    /// have is gone: it was left behind by the integer rewrite with no callers at all.
    qint64 surfaceShaderTimeMs();
    qint64 m_surfaceTimeEpochMs = -1; ///< steady-clock ms captured on the first iTime push

    /// Should this window be driven to repaint THIS frame?
    ///
    /// NOT a pure query, and not only about iTime: it also reports an in-flight focus ramp, a
    /// live audio spectrum, and a fold cursor that has drifted from the folded one — and in
    /// that last case it ARMS hoverRepaintPending, which is why it is non-const. A reader who
    /// took the old "true when any pack references iTime" wording at face value is exactly how
    /// the second hover driver came to spin at vsync.
    ///
    /// True when ANY pack in @p windowId's resolved chain references iTime (main
    /// or a buffer pass). Such a window is driven to repaint every frame by
    /// postPaintScreen so its animation advances even with no content damage; a
    /// purely static decoration (e.g. border-only) returns false and costs nothing.
    bool windowSurfaceAnimates(const QString& windowId);

    /// Is this window's decoration chain allowed to animate right now? False when
    /// the session is idle (and PauseWhenIdle is on), or when AnimateFocusedOnly is
    /// on and this is not the active window. A window it refuses keeps its last
    /// composite and still LOOKS decorated — it just stops moving. Defined in
    /// surface_gating.cpp.
    bool decorationMayAnimate(KWin::EffectWindow* w) const;

    /// Mark every repaint issued inside this scope as OURS, so the damage handler does
    /// not read it as the window's content going stale and drop the capture cache.
    /// See m_selfRepainting for why that distinction exists at all.
    ///
    /// RESTORES the previous value rather than clearing, so a scope nested inside
    /// another cannot hand the outer one back an un-flagged window — the failure that
    /// would produce (silent, per-frame capture invalidation) is exactly the one this
    /// flag exists to prevent, and it would not be visible in any test.
    [[nodiscard]] auto selfRepaintScope()
    {
        const bool previous = m_selfRepainting;
        m_selfRepainting = true;
        return qScopeGuard([this, previous] {
            m_selfRepainting = previous;
        });
    }

    /// Is the window's focus cross-fade still moving? Shared by the postPaintScreen
    /// repaint driver and windowSurfaceAnimates, which must agree — one decides
    /// whether to drive the window, the other whether the chain has anything to show
    /// for being driven.
    bool focusRampInFlight(const QString& windowId) const;

    /// Make the compositor's GL context current, best-effort.
    ///
    /// Every path that DESTROYS a GL object (a shader, a texture, a framebuffer, or a
    /// window's offscreen redirect) has to be able to run off the paint cycle — a file
    /// watcher, a D-Bus reply, a QTimer, a window closing — and glDelete* against no
    /// current context is undefined. This was asserted in six places and quietly ignored
    /// in nine, including the hottest one (every time-driven animation's teardown). It is
    /// one call here instead, idempotent and a no-op when the context is already current.
    ///
    /// False only during compositor teardown, where GL is going away and the driver
    /// reclaims everything regardless — so callers clear their state either way rather
    /// than leaking it to avoid a call that cannot matter.
    bool ensureGlContextCurrent() const
    {
        return KWin::effects && KWin::effects->makeOpenGLContextCurrent();
    }

    /// Wake every decorated window with one repaint each. Needed whenever a gate above
    /// OPENS (a settings flip, the session resuming, the daemon dying while we were idle):
    /// a paused chain emits no damage of its own, so it would otherwise stay frozen on its
    /// last composite until something unrelated damaged it. Defined in surface_gating.cpp.
    void repaintAllDecorations();

    /// Repaint every decorated window whose chain reads the cursor. The ONLY thing that
    /// restarts a hover pack's repaint loop after it settles — see the note on it.
    void repaintHoverDecorations(const QPointF& cursor);

    /// The cursor value a fold bakes in for this window. The fold keys its cache on this
    /// and the repaint driver decides whether to drive on it, so it is one expression:
    /// two spellings that must agree exactly are two spellings that eventually will not.
    /// @p cursor is passed explicitly rather than read from the frame cache, because the
    /// pointer-motion path runs BEFORE the next frame refreshes that cache and would
    /// otherwise compare against a stale position.
    QPointF foldCursorFor(KWin::EffectWindow* w, const QRectF& canvasGeo, bool mayAnimate, const QPointF& cursor) const;

    /// Surface-shader pack registry (the "surface" category: window border /
    /// rounded corners / glow / …). Discovers data/surface packs; the effect
    /// compiles each pack a resolved decoration chain references. Search paths
    /// populated lazily via ensureSurfaceRegistryPaths.
    PhosphorSurfaceShaders::SurfaceShaderRegistry m_surfaceShaderRegistry;
    bool m_surfaceRegistryPathsAdded = false; ///< one-shot guard for the search-path population

    /// Per-surface decoration profile tree, delivered by the daemon as
    /// `decorationProfileTreeJson` (Settings::decorationProfileTree). resolve()
    /// over a window's surface path (window.tiled / window.snapped /
    /// window.floating) yields the DecorationProfile that drives the window's
    /// surface-pack chain and the per-pack parameters that style it (border
    /// width / radius / colours are the pack's own params, not host fields).
    /// Seeded in the constructor with a baseline matching
    /// today's per-field defaults so decoration renders correctly before the
    /// async fetch lands; replaced wholesale when the setting arrives.
    PhosphorSurfaceShaders::DecorationProfileTree m_decorationTree;

    /// Compiled surface-shader packs keyed by pack id (CompiledSurfacePack holds
    /// the main MapTexture shader, contract uniform locations, pack-declared
    /// param values, the main-pass iChannel locations, and the multipass buffer
    /// passes for that one pack). Populated on first use by compiledPack();
    /// cleared wholesale on a SurfaceShaderRegistry hot-reload (effectsChanged)
    /// and on teardown. A window's render path looks up its resolved base pack id
    /// (WindowDecoration::basePackId) here.
    std::unordered_map<QString, CompiledSurfacePack> m_compiledPacks;

    /// Has ANY compiled pack ever declared iMouse in the current compile generation?
    ///
    /// A cheap necessary condition for the hover driver, which fires on every pointer-motion
    /// event. When false, no decoration can possibly react to the cursor, so the whole
    /// per-window chain scan is skipped — which is the entire common case (border-only chains,
    /// no hover pack anywhere). Set true when a cursor-reading pack compiles; never cleared
    /// except with m_compiledPacks itself, so a hot-reload that drops the last hover pack
    /// re-derives it from the next round of compiles. It can lag TRUE for a pack no window
    /// currently uses, which only costs the fuller scan the driver already did — it never
    /// lags false, so the driver can never miss a real hover pack.
    bool m_anyCompiledPackReadsCursor = false;

    /// Per-window multipass FBO targets (surfaceTex + bufferTex chain). Keyed by
    /// getWindowId(w). Allocated lazily by the composite fold, reallocated
    /// when the window's expanded size × scale changes, and erased on window
    /// close / border removal (removeWindowDecoration) to free GPU memory.
    std::unordered_map<QString, SurfaceMultipassState> m_surfaceMultipass;

    // ── Audio-reactive surface decorations (CAVA) ────────────────────────────
    // The compositor has no daemon-style audio path, so the effect runs its OWN
    // CavaSpectrumProvider (Qt-Core-only) and uploads the spectrum to a session-
    // global `bars×1` texture bound as `uAudioSpectrum` (surface_audio.glsl) on
    // every audio-reactive decoration pass, mirroring the daemon's RGBA8
    // R-channel layout. A pack opts in purely by including surface_audio.glsl and
    // reading getBass/audioBar (its compiled iAudioSpectrumSizeLoc then resolves
    // >= 0). Animation packs share the same texture through their own opt-in
    // module (data/animations/shared/audio.glsl) plus an `audio` metadata flag.
    // Cava is gated by syncEffectAudioState on `enableAudioVisualizer` AND at
    // least one audio consumer being present — a decorated window carrying an
    // audio pack, or an audio animation pack assigned where transitions can
    // resolve it — so capture only spins up when something can actually react.

    /// The effect's own CAVA spectrum source. Constructed lazily on first need
    /// (syncEffectAudioState) so a session that never uses an audio decoration
    /// or animation pack pays nothing. Owns a `cava` child process while
    /// running.
    std::unique_ptr<PhosphorAudio::IAudioSpectrumProvider> m_audioProvider;

    /// Session-global spectrum texture (`bars×1`, R = bar value 0..1). Uploaded
    /// lazily during the composite fold when a new spectrum has arrived
    /// (m_audioSpectrumDirty), reused across every audio-reactive window that
    /// frame. Lives on the GL thread (allocated/uploaded only inside paint).
    std::unique_ptr<KWin::GLTexture> m_audioSpectrumTex;

    /// Shared 1x1 transparent texture bound in place of a referenced but
    /// unsupplied user-texture sampler (surface fold + animation paths), so
    /// the contract's "reads transparent black" holds instead of the sampler
    /// defaulting to unit 0 (live window content). Lazily created by
    /// transparentFallbackTexture() on a paint path; freed with the effect.
    std::unique_ptr<KWin::GLTexture> m_transparentFallbackTex;

    /// Lazily create + return the shared transparent fallback texture; null
    /// only when GL allocation fails (callers then skip the bind).
    KWin::GLTexture* transparentFallbackTexture();

    /// Latest spectrum delivered by the provider signal (values 0..1). Copied on
    /// the compositor thread; consumed (uploaded) during the next paint.
    QVector<float> m_audioSpectrum;
    bool m_audioSpectrumDirty = false; ///< a new spectrum awaits GL upload
    int m_audioSpectrumSize = 0; ///< bar count == iAudioSpectrumSize; 0 when audio is off
    /// steady-clock ms of the last spectrum that actually CHANGED. Drives the
    /// idle gate (audioReactiveDriving): sustained silence settles to repeated
    /// frames, so after a quiet window the per-vsync recomposite stops instead of
    /// folding every audio border forever. -1 until the first spectrum arrives.
    qint64 m_audioSpectrumLastChangeMs = -1;

    /// The daemon's audio-viz master toggle + the full CAVA parameter set,
    /// pulled via getSetting in loadCachedSettings exactly like
    /// snapAssistEnabled. The effect's cava run gate ANDs the toggle with an
    /// audio decoration or an audio animation pack being present; the options
    /// are applied wholesale in syncEffectAudioState (the provider no-ops on
    /// an unchanged set and restarts capture at most once per change).
    bool m_enableAudioVisualizer = false;
    PhosphorAudio::SpectrumOptions m_audioOptions;

    /// KWin stock effects syncStockEffectSuppression unloaded because one of
    /// OUR packs owns the event they animate: windowaperture/eyeonscreen for
    /// a `desktop.peek` pack, magiclamp/squash for a window.minimize pack,
    /// maximize for a window.maximize pack. Only names WE
    /// unloaded are recorded, so clearing the pack (or unloading this effect)
    /// loads back exactly what the user had — never an effect KWin left
    /// disabled in kwinrc. Accepted edge: disabling a builtin in the Desktop
    /// Effects KCM WHILE the suppression holds it unloaded leaves its name
    /// recorded (the KCM apply is a no-op on the already-unloaded effect), so
    /// the eventual restore re-loads it for the rest of the session; the next
    /// session honours kwinrc, which the suppression never writes. Querying
    /// kwinrc from the effect to close this would add a config dependency the
    /// plugin doesn't otherwise need.
    QStringList m_suppressedStockEffects;
    /// Set by the aboutToQuit latch (constructor): distinguishes a runtime
    /// unload of this effect from compositor shutdown in the destructor's
    /// suppressed-effect restore. See ~PlasmaZonesEffect.
    bool m_compositorShuttingDown = false;
    /// Coalescing latch for scheduleEffectAudioSync: many decoration/settings
    /// callbacks can fire in one event-loop turn (a focus change removes then
    /// re-adds a decoration); collapsing them to one syncEffectAudioState keeps
    /// the blocking cava stop()/start() off the synchronous path and avoids a
    /// kill+respawn when a decoration is immediately re-added.
    bool m_audioSyncScheduled = false;
    /// Warn once, not every sync, when an audio pack wants CAVA but `cava` is not
    /// installed. Reset when audio is torn down so a later install can re-warn.
    bool m_audioUnavailableWarned = false;

    /// Deliver a fresh spectrum from m_audioProvider: store it, stamp the
    /// change time, mark the texture dirty, and prime a repaint so audio-reactive
    /// borders pick it up.
    void onEffectAudioSpectrum(const QVector<float>& spectrum);

    /// Start/stop/reconfigure the effect's cava instance to match the run gate
    /// (m_enableAudioVisualizer && (hasAudioReactiveDecoration() ||
    /// hasAudioReactiveAnimation())). Lazily creates m_audioProvider on first
    /// run. Prefer scheduleEffectAudioSync from high-frequency callers
    /// (decoration refresh, settings replies).
    void syncEffectAudioState();

    /// Coalesced, deferred syncEffectAudioState: sets a pending latch and posts a
    /// single queued evaluation, so a remove-then-readd (focus change) or the two
    /// async settings replies settle to ONE net decision at event-loop return and
    /// the compositor thread never blocks on cava stop()+respawn mid-refresh.
    void scheduleEffectAudioSync();

    /// Unload the KWin stock effects whose event one of OUR packs owns, and
    /// load back exactly the ones WE unloaded when that stops holding. Three
    /// groups share one predicate shape (pack assigned in the tree, installed,
    /// event-contract match, animations enabled):
    ///   desktop.peek       → windowaperture / eyeonscreen
    ///   window.minimize    → magiclamp / squash
    ///   window.maximize    → maximize
    /// Unloading is the only suppression that works for all three: the
    /// show-desktop scripts never consult activeFullScreenEffect() (and the
    /// peek deliberately takes no fullscreen claim anyway, see
    /// DesktopTransitionManager), and the minimize/maximize stock effects
    /// honor no per-window grab role the way the open/close builtins honor
    /// WindowAddedGrabRole / WindowClosedGrabRole — left loaded they animate
    /// the same surface concurrently with our shader (discussion #816).
    /// Idempotent; re-asserted from every path that can change the predicate
    /// or the loaded-effects list: the shader-profile-tree load, the animation
    /// registry commit (bringup + pack install/uninstall), the animationsEnabled
    /// setting, and reconfigure() (a Desktop Effects KCM apply re-loads the
    /// scripts from kwinrc). The destructor restores them on a runtime unload
    /// but skips during compositor shutdown (m_compositorShuttingDown).
    void syncStockEffectSuppression();

    /// True when any decorated window's resolved chain carries an audio-reactive
    /// pack (SurfaceShaderEffect::audio). Read from pack METADATA (no compile
    /// needed) so the run gate resolves before first paint.
    bool hasAudioReactiveDecoration() const;

    /// True when an audio-reactive ANIMATION pack (AnimationShaderEffect::
    /// useAudio) is assigned anywhere transitions can resolve it from: the
    /// shader profile tree's baseline or overrides, or an animation rule's
    /// effectId payload. Keeps cava warm while such a pack is assigned so a
    /// transition's FIRST frame already has a spectrum — a lazy start at
    /// transition begin would eat the whole leg in cava spawn latency. Like
    /// its decoration sibling, reads pack metadata only.
    bool hasAudioReactiveAnimation() const;

    /// True when audio is live: the toggle is on, the provider is running, and a
    /// non-empty spectrum has arrived. Gates pushing iAudioSpectrumSize > 0 and
    /// binding the spectrum texture. NOT the repaint gate — use
    /// audioReactiveDriving for that so silence lets the paint loop idle.
    bool audioActive() const;

    /// True when audio is live AND the spectrum changed recently (within the idle
    /// window). The repaint gate for windowSurfaceAnimates: sustained silence
    /// (repeated frames stop refreshing m_audioSpectrumLastChangeMs) lets the
    /// per-vsync recomposite stand down until audio resumes.
    bool audioReactiveDriving() const;

    /// Upload m_audioSpectrum into m_audioSpectrumTex if dirty (reallocating on a
    /// bar-count change), returning true when the texture is bindable this frame.
    /// Runs inside the composite fold (GL context current). Self-heals: a failed
    /// (re)allocation leaves the dirty flag set to retry next frame.
    bool ensureAudioSpectrumTexture();

    /// Bind the session-global CAVA spectrum to kSurfaceAudioUnit and push
    /// iAudioSpectrumSize onto the currently-bound @p shader. Pushes size 0 (and binds
    /// nothing) when audio is not live or the pack declares no audio locations, so
    /// getBass*() reads 0 and the pack renders static. Returns true when it bound the
    /// texture, and the caller then unbinds the unit after drawing.
    ///
    /// @p animating is false for a chain that Decorations.Performance has paused, which is
    /// treated exactly like silence — a paused chain is CACHED, and a cached composite must
    /// not be fed a live spectrum. See the note in surface_audio.cpp.
    bool bindSurfaceAudio(KWin::GLShader* shader, int iAudioSpectrumSizeLoc, int uAudioSpectrumLoc, bool animating);

    /// Resolve the DECORATION SURFACE PATH for @p windowId based on MEMBERSHIP
    /// alone:
    ///   • autotile member (AutotileStateHelpers::isTiledWindow) → "window.tiled"
    ///   • else snap member (SnapHandler::isTiledWindow)         → "window.snapped"
    ///   • else                                                  → "window.floating"
    /// Autotile-first precedence. The resolved profile's effectiveChain() (an
    /// empty chain = no decoration) is the sole render gate (see
    /// updateWindowDecoration); there is no separate show-border gate.
    QString resolveSurfacePathFor(const QString& windowId) const;

    /// Seed m_decorationTree with the empty/neutral default (mirroring the
    /// daemon's ConfigDefaults::decorationProfileTree()) so the pre-fetch state
    /// is well-defined. Called once from the constructor. The tree carries only
    /// the user-applied surface-shader pack stack; border and title-bar
    /// appearance are resolved host-side (config-default appearance + rules)
    /// and render correctly before the fetch, so no placeholder is built. The
    /// daemon's async `decorationProfileTreeJson` fetch overwrites the whole
    /// tree on arrival.
    void seedDecorationTreeBaseline();

    /// Coalesce a full border sweep to the end of the event-loop turn. The
    /// config-default appearance loaders (and the accent / inactive colour
    /// loaders) each land as a separate async settings reply; several arriving in
    /// one turn would otherwise each run a full updateAllDecorations(). Collapsing them
    /// to a single deferred sweep keeps the last-value result while doing the work
    /// once. The sweep still lands before the next paint.
    void scheduleBorderSweep();

    /// Drop the per-rule match cache and refresh @p windowId's border /
    /// opacity after its placement state (snapped / floating / zone) changed.
    /// Those are rule MATCH inputs now, so without this a window stays resolved
    /// at its prior state (e.g. a `WHEN isSnapped` border never reverting on
    /// unsnap). Mirrors slotWindowActivated's focus invalidation. A no-op only when the
    /// window has nothing that could re-resolve: no rules AND no config-default window
    /// appearance AND no decoration tree content. The last two matter — a config-default
    /// border scoped to tiled windows must still reconcile on a snap flip with an empty
    /// rule set.
    void invalidateRuleCacheForStateChange(const QString& windowId);

    /// Bulk analog of invalidateRuleCacheForStateChange for placement changes that
    /// affect EVERY window at once — daemon loss (the zone / floating caches are
    /// cleared) and the daemon-ready re-seed (they are repopulated). The match
    /// cache is keyed (windowId, ruleSet revision); neither moves on a bulk
    /// placement change, so a placement-scoped opacity verdict would otherwise
    /// stay cached (e.g. a `WHEN isSnapped` SetOpacity window staying dimmed after
    /// the cache that made it "snapped" was cleared). Drops the whole match cache,
    /// then re-reconciles every window's rule layer — keepAbove/keepBelow is
    /// event-driven, so the cache clear alone would leave it stale on both the
    /// loss and re-seed edges. Appearance slots (opacity, tint, border colour)
    /// bake into the decoration at updateWindowDecoration time, so each caller
    /// pairs this with its own decoration path: daemon loss tears the
    /// decorations down (clearAllDecorations), the daemon-ready re-seeds
    /// schedule a border sweep to re-fold against the fresh placement. No-op
    /// when there are no animation rules and no rule-held layer snapshots.
    void invalidateAllRuleCaches();

    /// Flush coalesced per-rule-cache invalidations queued by
    /// invalidateRuleCacheForStateChange within one event-loop turn: drops the
    /// match cache once and re-resolves the border / opacity of each affected
    /// window. Posted via a queued single-shot so a float toggle (which emits
    /// both windowFloatingChanged AND windowStateChanged) clears the cache once
    /// instead of twice.
    void flushPendingRuleInvalidations();

    /// Resolve the per-window-rule SetHideTitleBar override for @p windowId
    /// and forward it to the DecorationManager as a tri-state rule override
    /// (unset = mode decides, true = rule hides, false = force-show veto).
    void reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w);

    /// Resolve the per-window-rule SetWindowLayer override for @p windowId and
    /// apply it to KWin's keepAbove/keepBelow pair. First application snapshots
    /// the window's pre-rule flags into m_ruleWindowLayerSnapshots; a resolve
    /// with no owning rule restores that snapshot once and forgets the window.
    /// Rides a superset of reconcileRuleHiddenTitleBar's triggers
    /// (placement-state flush, rule edits / focus via updateAllDecorations)
    /// plus an eager window-added apply — so a layer rule takes effect before
    /// the window's first reconcile-triggering event — the class-swap
    /// re-drive in the identity-change handler, and the bulk-placement
    /// sweep in invalidateAllRuleCaches (daemon loss and the daemon-ready
    /// re-seeds). Deliberately NOT triggered by keepAboveChanged /
    /// keepBelowChanged: an instant re-assert would fight the user's own
    /// toggle (the Krohnkite failure mode this feature exists to avoid), so
    /// a manual toggle under an active rule stands until the next natural
    /// reconcile.
    void reconcileRuleWindowLayer(const QString& windowId, KWin::EffectWindow* w);

    /// The window's OWN keep-above flag — the app/user-set state, with
    /// rule-written values substituted from the pre-rule snapshot while a
    /// SetWindowLayer rule owns the window's layer. Consulted by the
    /// keep-above overlay-tool gates (shouldHandleWindow / shouldDecorateWindow
    /// / isTileableWindow) and the engine-facing KWinCompositorBridge::windowInfo
    /// export; applyOwnLayerFlags is the query-side counterpart.
    bool windowOwnKeepAbove(KWin::EffectWindow* w) const;

    /// Substitute the pre-rule snapshot's keepAbove/keepBelow pair into
    /// @p query while a SetWindowLayer rule owns @p windowId's layer, so rule
    /// output never feeds back into rule input. Shared by ruleQuery (the
    /// effect's live evaluation path) and pushWindowMetadata (the daemon's
    /// KeepAbove/KeepBelow match inputs) — the one invariant lives in one
    /// place. No-op with no snapshots (the no-rules case pays one isEmpty).
    void applyOwnLayerFlags(PhosphorRules::WindowQuery& query, const QString& windowId) const;

    /// Restore every rule-applied window layer to its snapshotted pre-rule
    /// flags and clear the snapshot map. Teardown counterpart of
    /// reconcileRuleWindowLayer, called from the destructor next to
    /// DecorationManager::restoreAll so an effect unload doesn't strand
    /// rule-set keepAbove/keepBelow state on live windows.
    void restoreAllRuleWindowLayers();

    /// Pre-rule keepAbove/keepBelow pair captured the first time a
    /// SetWindowLayer rule is applied to a window. Deliberately NOT re-captured
    /// while a rule owns the layer, so the restore returns the window to the
    /// user's own state, not to an intermediate rule state.
    struct WindowLayerSnapshot
    {
        bool keepAbove = false;
        bool keepBelow = false;
    };
    QHash<QString, WindowLayerSnapshot> m_ruleWindowLayerSnapshots;

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

    // Full-screen desktop transitions: the virtual-desktop switch and the
    // show-desktop peek, which share the same path. By-value + `this` ctor,
    // same ownership shape as m_shaderManager; must be initialised AFTER it in
    // the ctor init list to match declaration order.
    DesktopTransitionManager m_desktopTransition;

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
    ///       global animations toggle off, collapsed surface, a
    ///       minimized surface without @p animateMinimized,
    ///       registry miss, a desktop-class pack refused on a window
    ///       event, the cached null-shader sentinel from a prior compile
    ///       failure, shader file open / read / include-expansion
    ///       failure, shader compile failure, or the transition-map
    ///       insert being rejected. Nothing was installed, so there is
    ///       nothing to schedule a teardown for either.
    ///
    /// Both cases are correctly handled by `tryBeginShaderForEvent`'s
    /// "skip the timer" branch. A future caller writing a manual install
    /// path that needs to distinguish the two should snapshot
    /// `m_shaderManager.findTransition(window)` (and its generation)
    /// pre-call and compare against the post-call snapshot to detect
    /// case (a).
    ///
    /// @p progressCurve is the event's resolved timing curve, and is honoured
    /// ONLY on the time-driven path (@p durationMs > 0), where paintWindow eases
    /// the linear progress through it. On the animator-driven path
    /// (@p durationMs == 0) it is dropped with a warning: that leg reads its
    /// progress from the WindowAnimator, whose own profile already carries the
    /// curve, so honouring it here would double-ease.
    ///
    /// @p animateMinimized opts a MINIMIZED window into the install. Only the
    /// going-to-minimized leg of window.appearance.minimize passes true: it is
    /// the one event whose semantic is "animate this window although it is
    /// minimized", and the install then holds an EffectWindowVisibleRef so the
    /// window has frames to paint. Every other event reaching a minimized
    /// window (a snap batch, focus, a racing geometry apply) is rejected as it
    /// always was — installing there would force-show a window the user
    /// believes is minimized.
    bool beginShaderTransition(KWin::EffectWindow* window, const PhosphorAnimationShaders::ShaderProfile& profile,
                               int durationMs = 0, bool reverse = false, bool holdCloseGrab = false,
                               bool holdAddedGrab = false, bool animateMinimized = false,
                               std::shared_ptr<const PhosphorAnimation::Curve> progressCurve = nullptr);
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
                                bool reverse = false, bool holdCloseGrab = false, bool holdAddedGrab = false,
                                bool animateMinimized = false);
    /// Arm the duration teardown for a time-driven transition, generation-guarded.
    ///
    /// Re-arms itself when the transition's own clock says the leg is not finished.
    /// The install-time delay is only a first estimate: restore suppression rebases
    /// `startTimeMs` every withheld frame, so a timer fixed at install fires while
    /// the animation still has up to 250 ms left to play.
    void scheduleShaderTransitionTeardown(KWin::EffectWindow* window, quint64 generation, int delayMs);
    /// Runtime mirror of the settings pickers' shader-class filter, routed
    /// through the canonical PhosphorAnimationShaders::
    /// shaderEffectAppliesToEventPath predicate so the two can never drift.
    /// Returns false only when @p effectId is KNOWN to the registry and
    /// provably cannot drive @p profilePath (e.g. a crossfade pack on the
    /// held-drag leg, a move-physics or desktop pack on a crossfade leg).
    /// An id the registry doesn't know returns true: the pack may still be
    /// scanning, and beginShaderTransition's registry-miss warning stays the
    /// single reporter for genuinely unknown ids. Gates every per-window
    /// resolution route (tryBeginShaderForEvent and the applyWindowGeometry
    /// snap chokepoint) against rule-layer and stale-config assignments the
    /// pickers cannot intercept.
    bool resolvedShaderAppliesToEvent(const QString& effectId, const QString& profilePath) const;
    // window.maximize / window.unmaximize shader install + geometry-morph
    // endpoint wiring. `departureFrame` is the frame rect the window is
    // leaving (the pre-maximize float rect when maximizing, the maximized
    // rect when restoring); the destination is read live. Both directions
    // play FORWARD — geometry packs encode direction in the rects, matching
    // the zone-snap convention (see the implementation comment). Called
    // either directly from the maximize state edge (geometry already landed)
    // or deferred to the size-delivering windowFrameGeometryChanged when the
    // state signal outran the client's commit. Implementation in
    // window_lifecycle.cpp beside its two call sites.
    void beginMaximizeShaderMorph(KWin::EffectWindow* window, const QRectF& departureFrame);
    /// Evict least-recently-used cached textures back under the soft bound, never
    /// touching one a live transition still points at. @p pending is the transition
    /// currently being BUILT, which is not in shaderTransitions() yet and would
    /// otherwise have the slots it has already filled evicted out from under it.
    /// No default argument on purpose. A build site that forgets @p pending compiles
    /// straight into the unguarded path with no diagnostic, and the entry it just
    /// inserted is precisely the one the sweep would take.
    void evictLruTextureIfOverBound(const ShaderTransition* pending);
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

    /// True while DesktopTransitionManager::compositeWindowsInto drives
    /// paintWindow DIRECTLY (outside KWin's chain walk). That is the shared tail
    /// of BOTH desktop captures — captureDesktop (the switch legs) and
    /// capturePeekWindowsScene (the peek's windows layer). paintWindow's tail then terminates
    /// with effects->drawWindow instead of continuing the paintWindow chain:
    /// the chain iterator sits at begin() in that context, so chaining would
    /// re-enter our own paintWindow (double fold, animator transform applied
    /// twice to the capture) and drive later effects' paintWindow hooks without
    /// the prePaintWindow they key off — the capture deliberately runs windows
    /// that were never in this frame's scene walk. Unlike m_capturingSnapshot
    /// this must NOT suppress the fold: the capture exists to bake the
    /// decorated composite into the transition texture.
    bool m_directPaintCapture = false;

    /// True while WE schedule a repaint on a window (postPaintScreen's per-frame
    /// repaint that keeps an animated decoration chain ticking). KWin's
    /// `windowDamaged` fires on repaint SCHEDULING, not just on client content
    /// damage, so without this guard our own addRepaintFull would invalidate the
    /// very capture cache it exists to let us reuse — every frame, so the cache
    /// could never hit. The damage handler ignores signals raised inside this
    /// window; genuine client damage lands outside it and still invalidates.
    ///
    /// Never set this directly — take a selfRepaintScope().
    bool m_selfRepainting = false;

    // ── Decorations.Performance ─────────────────────────────────────────────
    // An animated decoration pack repaints every window carrying it on EVERY
    // vsync. That is what keeps the GPU pinned in its top performance state
    // (measured: ~110 W and +12 C over an idle desktop with the effect unloaded,
    // on a card only ~45% busy) — the cost is not the work per frame, it is that
    // there is work every frame. No amount of shrinking the per-frame work
    // recovers the idle clocks; only not drawing does. These two gate that.

    /// Animate only the focused window's chain; unfocused windows hold their last
    /// composite. Divides the continuous redraw by the decorated-window count.
    bool m_animateFocusedOnly = false;

    /// Stop animating once the session goes idle, resume on the first input.
    bool m_pauseAnimationWhenIdle = true;

    /// Whether the session is currently idle. Pushed by the daemon, which owns the
    /// idle detection: idleness is a WAYLAND CLIENT concern (ext-idle-notify-v1)
    /// and this effect lives inside the compositor, where that protocol is served
    /// rather than consumed. The effect sees only the resolved boolean.
    bool m_sessionIdle = false;

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
    // unified Rule store the effect mirrors over D-Bus. Filled by
    // loadRuleAnimationsFromDbus's parse step (which already
    // deserialises the full rule set for the animation override path),
    // via `PhosphorRules::ExclusionRules::excludeRulesFrom`. The
    // bound RuleEvaluator drives shouldHandleWindow()'s exclusion gate.
    // Declaration ORDER MATTERS — the rule set must precede (and outlive)
    // the evaluator that binds a reference to it.
    PhosphorRules::RuleSet m_snappingExclusionRuleSet;
    PhosphorRules::RuleEvaluator m_snappingExclusionEvaluator{m_snappingExclusionRuleSet};

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

    // Decoration window filtering — gates the border / decoration pass,
    // populated over D-Bus from the `Decorations.WindowFiltering` config group
    // (loadCachedSettings). Initialised to the config defaults so a pre-D-Bus
    // decoration pass matches the prior behavior: transients were already never
    // decorated (exclude-transient on), and no size threshold was ever applied
    // (min-size 0). The transient/min-size filters here reuse the snapping
    // exclusion rule set (m_snappingExclusionEvaluator) rather than a dedicated
    // decoration rule slice, so no new rule action is involved.
    bool m_decorationExcludeTransientWindows = true;
    int m_decorationMinWindowWidth = 0;
    int m_decorationMinWindowHeight = 0;

    // Animation exclusion rule set — the `ExcludeAnimations`-action slice
    // of the unified Rule store the effect mirrors over D-Bus.
    // Filled by loadRuleAnimationsFromDbus's parse step (which
    // already deserialises the full rule set for the animation override
    // path), via
    // `PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom`.
    // The bound RuleEvaluator drives shouldAnimateWindow()'s exclusion
    // gate. Declaration ORDER MATTERS — the rule set must precede (and
    // outlive) the evaluator that binds a reference to it.
    PhosphorRules::RuleSet m_animationExclusionRuleSet;
    PhosphorRules::RuleEvaluator m_animationExclusionEvaluator{m_animationExclusionRuleSet};

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

    // Whether the window being dragged was ALREADY floating when the drag
    // began. Written in the DragTracker::dragStarted handler before any float
    // transition runs, then snapshotted into a local at callEndDrag dispatch
    // (the async endDrag reply may land after the next dragStarted has already
    // overwritten this member, so the reply lambda must not read it directly).
    // The drag-stop ApplyFloat path consults that snapshot: a window that was
    // already floating is just being moved, so its current (user-chosen) size
    // must be preserved. Re-applying the stale pre-autotile size would clobber
    // any resize the user made while floating. Only the tiled→float transition
    // wants the pre-autotile size restore.
    bool m_dragStartedFloating = false;

    // Per-rule-cache invalidations accumulated within one event-loop turn,
    // flushed once by flushPendingRuleInvalidations(). Coalesces the double
    // invalidation a float toggle triggers (windowFloatingChanged + windowStateChanged).
    QSet<QString> m_pendingRuleInvalidations;

    // Set while a coalesced border sweep is queued for the end of the turn (see
    // scheduleBorderSweep); collapses a burst of appearance-setting replies into
    // one updateAllDecorations().
    bool m_borderSweepPending = false;

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
    /// Per-screen supersession epoch for slotApplyGeometriesBatch cascades.
    /// When cascade stagger is enabled, a daemon geometry batch spreads its
    /// per-window moves across QTimer::singleShot ticks. A rapid second batch
    /// (e.g. holding the rotate shortcut) starts its own cascade while the
    /// first one's ticks are still queued; the older batch's later-firing
    /// timers would then clobber the newer batch's positions, leaving windows
    /// in stale zones. Each batch bumps and captures the epoch for every screen
    /// it targets. A staggered apply drops itself when its screen's epoch has
    /// advanced, and the z-order restore drops only when every screen it
    /// targeted has advanced. Per-screen, not global, so a batch on
    /// one output never strands an in-flight cascade on another — mirrors the
    /// autotile cascade guard (m_autotileStaggerGenByScreen).
    QHash<QString, uint64_t> m_daemonBatchGenByScreen;
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
    /// One-shot guard for the Rules rulesChanged D-Bus subscription.
    /// QDBusConnection::connect silently accepts duplicate subscriptions, so without
    /// this flag the subscription set would grow unbounded across every
    /// slotSettingsChanged broadcast (which re-runs loadCachedSettings()). Set true
    /// after the first successful connect from continueDaemonReadySetup().
    bool m_rulesSubscribed = false;

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

    // Stamp of the last going-to-minimized shader install per window, used
    // by slotWindowMinimizedChanged to detect KWin's spurious
    // minimize→unminimize pairs (plasmashell notification stacking emits
    // them on tiled windows ~1-2 ms apart; the float side debounces the
    // same quirk with the shared kSpuriousMinimizePairMs — see
    // autotilehandler/signals.cpp). An unminimize landing inside the
    // window silently drops the reverse leg instead of replaying a full
    // un-minimize animation. `generation` pins the stamp to the exact
    // transition the minimize event installed (or kept running), so the
    // cancel can never hit an unrelated reverse leg (a superseding leg,
    // or any future reverse event). Entries are erased on consume and on
    // windowDeleted (raw-pointer-keyed, bounded like its siblings above).
    struct MinimizeShaderStamp
    {
        qint64 timeMs = 0;
        quint64 generation = 0;
    };
    QHash<KWin::EffectWindow*, MinimizeShaderStamp> m_minimizeShaderStamp;

    // Cursor output tracking (for daemon shortcut screen detection on Wayland)
    // Stores the connector name of the last output the cursor was on.
    // Used for deduplication only — the actual D-Bus call sends the EDID screen ID.
    QString m_lastCursorOutput;
    // Per-screen current virtual desktop last reported to the daemon (physical
    // screenId → 1-based desktop), for dedup of KWin's per-output desktopChanged.
    QHash<QString, int> m_lastScreenDesktop;

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

    /// Apply virtual-screen subdivisions for an already-resolved PHYSICAL screen id.
    /// This is the shared implementation; the output-taking overload above wraps it
    /// via outputScreenId(). getWindowScreenId resolves the output by POSITION
    /// (KWin::effects->screenAt) rather than trusting the window's own KWin output,
    /// then calls the output overload — so position-based resolution comes from the
    /// caller's screenAt, not from this overload.
    QString resolveEffectiveScreenId(const QPoint& pos, const QString& physId) const;

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

    /// The session went idle, or came back. Pauses / resumes decoration-chain
    /// animation when Decorations.Performance.PauseWhenIdle is on. Resuming has to
    /// repaint every decorated window: a paused chain emits no damage of its own,
    /// so it would otherwise stay frozen until something unrelated damaged it.
    void slotSessionIdleChanged(bool idle);

    /// Fetch the unified Rule store via `org.plasmazones.Rules.
    /// getAllRules`, filter to rules carrying any effect-consumed
    /// (Tag::Effect) action, and forward them to the shader manager — the
    /// sole source of per-window effect overrides. Called once at bringup; the bringup also
    /// subscribes to the interface's `rulesChanged` signal (via a debounce
    /// timer — see m_animationRulesRefreshDebounce) so a settings-UI edit
    /// takes effect without restarting the effect.
    void loadRuleAnimationsFromDbus();

    /// D-Bus signal handler for `Rules.rulesChanged`. Re-arms the
    /// debounce timer rather than refetching the full ruleset on every
    /// signal — the daemon emits one signal per per-rule mutation, so a
    /// 50-rule batch edit would otherwise drive 50 full-ruleset fetches
    /// and parses. A 50ms single-shot debounce coalesces the burst into a
    /// single fetch at the trailing edge.
    void slotRulesChanged();
};

} // namespace PlasmaZones
