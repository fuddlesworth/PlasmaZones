// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): PlasmaZonesEffect is the KWin plugin's
// single entry class — the Effect interface overrides plus every handler
// back-pointer surface the split-out implementation files
// (plasmazoneseffect/*.cpp, handlers, autotilehandler) call back through.
// The implementation is already partitioned; the class declaration is the
// one place KWin's plugin contract requires to be whole.

#pragma once

#include <PhosphorCompositor/TilingState.h>
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

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleSet.h>

// Project-local headers. The three transition managers are BY-VALUE members,
// so their declarations must precede the class body; the rest carry the POD
// state types the members below are declared with. Each is self-contained, so
// this block sits with the other project includes rather than after the Qt /
// KDE ones (own header → project → KDE → Qt).
#include "effect_state.h"
#include "shader_resolve.h"
#include "types.h"

#include "transitions/desktoptransitionmanager.h"
#include "transitions/shadertransitionmanager.h"
#include "transitions/striptransitionmanager.h"

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
#include <cstdint> // fetchVirtualScreenConfig's generation parameter
#include <functional>
#include <memory>
#include <type_traits>
#include <optional>
#include <unordered_map>
#include <vector>

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
namespace TilingStateHelpers = PhosphorCompositor::TilingStateHelpers;
namespace TriggerParser = PhosphorCompositor::TriggerParser;

// Per-call state carried into paintShaderTransitionWindow; defined in
// paint_internal.h (included only by the paint TUs). Forward-declared here so
// the method signature below can take it by const reference.
struct PaintWindowContext;

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
// unminimize ~1-2 ms later. THREE suppressions key off this window and MUST
// agree on its width: the autotile minimize→float debounce
// (tilinghandler/minimizefloat.cpp), the SNAP-mode minimize→float debounce
// (handlers/snaphandler.cpp) and the minimize shader-event spurious-pair
// cancel (plasmazoneseffect/daemon_apply.cpp, slotWindowMinimizedChanged).
// Shared here so the three can never desync — which is the whole point of
// enumerating them, so keep the list complete when a fourth appears.
inline constexpr int kSpuriousMinimizePairMs = 75;

// Forward declarations for helper classes
class TilingHandler;
class SnapHandler;
class ScrollOverhangInputFilter;
class KWinCompositorBridge;
class NavigationHandler;
class ScreenChangeHandler;
class SnapAssistHandler;
class CompositorClock;
class WindowAnimator;
class StripViewAnimator;
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
    bool blocksDirectScanout() const override;
    void postPaintScreen() override;
    void prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data) override;
    // Per-window borders are rendered by routing the redirected window through
    // the offscreen border MapTexture shader (see the drawWindow override below +
    // decorations.cpp), NOT here. paintScreen is overridden for the TWO
    // scene-replacement paths, tried in a load-bearing order: first the
    // full-screen desktop transitions (the virtual-desktop switch and the
    // show-desktop peek, which share one path) — while one is live,
    // m_desktopTransition.paintOutput draws the two-texture blend for that
    // output and we skip the normal scene — and second the strip shader pass
    // (m_stripTransition.paintOutput), which captures the scrolling scene and
    // draws the pack's decoration of it. Desktop deliberately outranks strip:
    // a desktop blend replaces the scene wholesale, so a strip pass under it
    // would decorate a frame nobody sees. Otherwise this chains straight
    // through.
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

    // Capture the OLD content into a GLTexture for a cross-fade, storing it in
    // `transition.oldSnapshot` (bound as uOldWindow). Replicates KWin's
    // OffscreenData::maybeRender: render into our own FBO via
    // effects->drawWindow, the capture-target's shader temporarily bypassed so
    // the copy is raw content. Two callers with different subjects:
    //  - the geometry morph, on the leg's first paint, capturing @p window's
    //    OWN still-old content (the moveResize configure hasn't
    //    round-tripped);
    //  - the tab swap's LAZY FALLBACK (transition.snapshotSource set),
    //    capturing the OUTGOING tab — a different window — wherever it is
    //    parked, at first paint, when seedTabSwapSnapshot found no usable
    //    composite at install time.
    void captureOldWindowSnapshot(ShaderTransition& transition, KWin::EffectWindow* window);
    /// Install-time snapshot for the scrolling tab swap: blits the outgoing
    /// tab's decorated composite while it is still the PRE-SWITCH fold (the
    /// lazy first-paint capture reads a park-poisoned re-fold instead — see
    /// the definition). Arms the lazy raw-capture fallback when no usable
    /// composite exists.
    void seedTabSwapSnapshot(ShaderTransition& transition, KWin::EffectWindow* src, KWin::EffectWindow* window);

    /// Outcome of the shader-transition branch extracted from paintWindow.
    /// Handled: the branch painted (or captured / suppressed / queued its own
    /// teardown) and paintWindow must return without touching the rest of the
    /// chain. Continue: an expired, non-minimized leg fell through, so paintWindow
    /// proceeds to the decoration fold and the normal paint-chain continuation —
    /// byte-equivalent to the branch's original fall-out of its `if` block.
    enum class ShaderBranchOutcome {
        Handled,
        Continue,
    };

    /// The shader-transition branch of paintWindow, extracted verbatim
    /// (paint_shader_window.cpp). Runs the snapshot capture-only frame, computes
    /// progress, binds every animation-shader uniform, draws the redirected
    /// window, and drives the deferred expiry teardown. @p st is the live
    /// transition (guaranteed non-null with a cached shader by the caller's
    /// guard); @p ctx carries paintWindow's per-call paint arguments and the
    /// per-frame-pinned clock. See ShaderBranchOutcome for the control-flow
    /// contract that keeps paintWindow's behaviour byte-equivalent.
    ShaderBranchOutcome paintShaderTransitionWindow(const PaintWindowContext& ctx, ShaderTransition* st);

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
    void slotWindowOutputMoveExpected(const QString& windowId, const QString& targetScreenId,
                                      const QString& sourceScreenId);

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
     * The composite is cached per EffectWindow* in m_idCaches.windowIdCache and
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
     * Called from slotWindowAdded for initial registration; from the
     * windowClassChanged / desktopFileNameChanged / desktops- and
     * activities-changed handlers for identity/context updates; from the
     * minimizedChanged handler (full snapshot — the daemon's mode-swap seed
     * and capture guards consult the registry's LIVE isMinimized); and from
     * the captionChanged handler (caption-only, includeExtended=false).
     *
     * @param includeExtended When false, the extended-property snapshot (the
     * trailing a{sv}: state flags, geometry, accessory flags) is NOT rebuilt
     * or sent — the daemon preserves whatever it already has. The one
     * exception is captionNormal, which derives from the caption and is sent
     * alone (the daemon treats a CaptionNormal-only map as a caption refresh,
     * not a snapshot replace). Used by
     * the captionChanged handler: terminals/browsers rewrite their title every
     * frame, and the other rule-relevant extended fields don't change on a title
     * tick, so rebuilding/marshalling a ~20-entry map per frame is pure waste. The
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
    /// exemptFullscreen waives ONLY the structural fullscreen/transientFor
    /// terms (threaded into isStructurallyUnmanageableWindowType) — every
    /// other rejection stays authoritative. Opt-in for callers that carry the
    /// scrolling windowed-fullscreen exemption (isEligibleForTilingNotify);
    /// the default keeps all other consumers treating a genuinely fullscreen
    /// window as unmanageable.
    bool shouldHandleWindow(KWin::EffectWindow* w, QString* rejectReason = nullptr,
                            bool exemptFullscreen = false) const;

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
     * The fullscreen term carves out windowed-fullscreen strip members (the
     * strip keeps tiling them through real KWin fullscreen), and callers can
     * additionally exempt it wholesale via @p exemptFullscreen — the
     * activation-reporting path does, for any fullscreen window on a
     * scrolling screen — WITHOUT bypassing the other terms: a fullscreen
     * transient/splash/popup stays rejected either way.
     *
     * @param w                window to classify; must be non-null.
     * @param rejectReason     when non-null, set to a human-readable reason on
     *                         a true return. @see shouldHandleWindow.
     * @param exemptFullscreen when true, the fullscreen term never fires and
     *                         the bare transientFor() term is waived too
     *                         (Wine/Proton toplevels carry transient_for on
     *                         the real game window); every explicit type
     *                         term stays authoritative.
     */
    bool isStructurallyUnmanageableWindowType(KWin::EffectWindow* w, QString* rejectReason = nullptr,
                                              bool exemptFullscreen = false) const;
    // Cached placement-exclusion verdict (Exclude ∪ ExcludePlacement slice)
    // consumed by shouldHandleWindow's drag gate. Fast-paths on an empty
    // exclusion slice; otherwise resolves through the exclusion evaluator's
    // per-window cache (same freshness contract as the animation verdicts —
    // see the implementation).
    /// @p sharedQuery (optional): caller-owned memoisation slot, as on
    /// shouldAnimateWindow. Engaged only when a cache miss forced the build.
    bool isExcludedBySnappingRule(KWin::EffectWindow* w,
                                  std::optional<PhosphorRules::WindowQuery>* sharedQuery = nullptr) const;
    // Cached decoration-exclusion verdict (Exclude ∪ ExcludeDecorations
    // slice) consumed by shouldDecorateWindow. Same empty-slice fast path
    // and per-window cache contract as isExcludedBySnappingRule.
    /// @p sharedQuery (optional): caller-owned memoisation slot, same contract
    /// as the snapping twin. Engaged only when a cache miss forced the build.
    bool isExcludedByDecorationRule(KWin::EffectWindow* w,
                                    std::optional<PhosphorRules::WindowQuery>* sharedQuery = nullptr) const;

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
    /// @p sharedQuery (optional): a caller-owned memoisation slot. When the
    /// gate has to build the per-window WindowQuery (~30 KWin accessors), it
    /// stores it there so the caller's own resolver pass can reuse it instead
    /// of building a second one — the drag chokepoint pays this per animated
    /// apply. The slot is left disengaged when no rules forced a build.
    bool shouldAnimateWindow(KWin::EffectWindow* w,
                             std::optional<PhosphorRules::WindowQuery>* sharedQuery = nullptr) const;

    /**
     * @brief Per-window gate for the border / decoration pass.
     *
     * Modeled on shouldAnimateWindow rather than shouldHandleWindow so the
     * transient family is a real toggle (m_decorationExcludeTransientWindows):
     * with it off the effect draws borders onto dialogs / popups. Rejects the
     * always-wrong surfaces (own overlay / editor, xdg-portal, plasma-shell,
     * special / desktop / dock / fullscreen / skipSwitcher, notification / OSD),
     * honours the dedicated decoration-exclusion slice (Exclude ∪
     * ExcludeDecorations via m_decorationExclusionEvaluator — blanket Exclude
     * still leaves an app undecorated, the scoped ExcludePlacement
     * deliberately does not), then applies the transient toggle and the
     * min-size threshold. Defaults preserve the prior behavior (transient on,
     * size off), so a default config decorates exactly what it did before.
     *
     * The one surface family that escapes the always-wrong block is the
     * plasma-shell kinds shellSurfaceKindFor() recognises: each is answered by
     * its own opt-in (m_decorationExcludeShellPanels for the panel) before the
     * app-window rejects run, because every one of those rejects is written
     * about application windows and would reject a shell surface outright.
     */
    /// @p sharedQuery (optional): caller-owned memoisation slot, forwarded to the
    /// Exclude-rule gate. updateWindowDecoration passes its own slot so the
    /// exclusion verdict and the rule-action resolve below it build the
    /// ~30-accessor WindowQuery once per window per sweep instead of twice.
    bool shouldDecorateWindow(KWin::EffectWindow* w,
                              std::optional<PhosphorRules::WindowQuery>* sharedQuery = nullptr) const;

    /**
     * @brief Which plasma-shell surface family @p w belongs to, if any.
     *
     * DECORATION-ONLY classification, deliberately NOT built on
     * isPlasmaShellSurface(). That predicate is a coarse "never track this"
     * class match written for autotile leakage (see its docs); it lumps the
     * panel, the desktop, tray popups, notifications, the OSD and krunner into
     * one verdict, which is exactly right for tracking and exactly wrong for
     * decoration, where each family wants its own surface path and its own
     * opt-in. The two must stay independent: relaxing this one must never
     * relax tracking, focus reporting, or rule shielding.
     *
     * Resolved from KWin's own window TYPE plus the owning class, not from a
     * substring guess. A Plasma panel is a real `NET::Dock` whose class is
     * plasmashell; that pair is unambiguous and needs no heuristics.
     */
    enum class ShellSurfaceKind {
        None, ///< not a plasma-shell surface (an ordinary app window)
        Panel, ///< plasmashell's panel(s) — NET::Dock, layer 3
        AppletPopup, ///< launcher / tray flyout / widget popup — NET::AppletPopup
    };
    static ShellSurfaceKind shellSurfaceKindFor(KWin::EffectWindow* w);

    /**
     * @brief May a matched rule write persistent window state onto @p w?
     *
     * Shared shield for the three reconcilers that mutate state a rule has no
     * business touching on a surface we do not own: the stacking layer, the
     * hidden title bar, and open-fullscreen. Deliberately SEPARATE from
     * shouldDecorateWindow — a user who opts into decorating the panel is
     * opting into our paint pass over it, not into letting a broad rule
     * rewrite plasmashell's window state. Callers resolve a shielded window as
     * rule-free rather than early-returning, so a window that mutated INTO a
     * shielded class still drains any snapshot it holds.
     */
    static bool isRuleShieldedSurface(KWin::EffectWindow* w);

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
     * @brief Recognise the daemon's dedicated scrolling tab-indicator layer
     *        surface, the one overlay that rides the strip's view offset.
     *
     * The indicators get a surface of their own so the paint path can translate
     * them with the columns they label without dragging the OSD (which fires on
     * the very action that scrolls) sideways with them.
     *
     * Matched by the wl_surface's protocol object id, which the daemon
     * announces over D-Bus. Nothing KWin exposes per window can tell the
     * daemon's overlays apart: they share a window class, carry no caption,
     * role or desktop file, sit on the same layer and cover the same rect, and
     * the layer-shell scope that WOULD name them is not reachable from an
     * exported API. The object id is the only thing the two sides can both
     * name for one surface.
     *
     * The id alone is NOT a handle, and the predicate is the id match AND the
     * owning client. A Wayland object id is unique only among ONE client's live
     * objects, and ids start low and grow, so an ordinary application's surface
     * collides with a daemon id routinely rather than exotically. The match is
     * therefore qualified by isOwnPassthroughOverlayClass, which is what names
     * the announcing client. See the implementation for what a false positive
     * would cost a real window (the strip's view offset every frame, forfeited
     * occlusion culling, and a permanent lower to the bottom of its layer).
     */
    bool isScrollTabIndicatorSurface(KWin::EffectWindow* w) const;

    /**
     * @brief Lower every known tab-indicator surface to the bottom of its layer.
     *
     * wlr-layer-shell cannot order two surfaces within one layer, so the
     * lazily-created indicator surface stacks above the daemon's passive
     * overlay shell and would paint across the modal cards that shell hosts.
     * A client has no say in this; the compositor does, and we are it.
     */
    void restackScrollTabSurfaces();

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
     * tilinghandler) and daemon-relayed ones (retile reactivation, unfloat
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
     * @brief Free-float geometry to CAPTURE for @p w, correcting for maximize/fullscreen.
     *
     * A maximized or fullscreen window's frameGeometry() is the full-monitor rect.
     * Capturing THAT as a window's pre-tile / pre-snap / float-back geometry makes it
     * restore to a maximized size when it later floats. Returns @p fallback unless @p w
     * is maximized/fullscreen, in which case it returns the pre-maximize / pre-fullscreen
     * RESTORE rect (a sane free size), falling back to @p fallback again if that restore
     * rect is empty. Every candidate — restore rects AND the fallback — passes the
     * off-screen poison guard, so the function can also return an INVALID rect (a frame
     * parked outside every screen by the scrolling engine is never a legitimate free
     * geometry). A windowed-fullscreen strip member returns INVALID unconditionally —
     * its live frame AND its fullscreen restore rect are both tile rects, so it has no
     * free geometry to offer (which is why this is a const member now, not a static:
     * the membership check needs the instance). Callers MUST check isValid() before
     * storing. Shared by the snap and autotile capture paths, which write the SAME
     * daemon free-geometry store.
     */
    QRectF freeGeometryForCapture(KWin::EffectWindow* w, const QRectF& fallback) const;

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
    /// @p sharedQuery (optional): caller-owned memoisation slot, as on
    /// shouldAnimateWindow. A miss reuses an already-built query from the slot and
    /// otherwise fills it, so a caller that also runs the Exclude gate pays one
    /// build for both.
    PhosphorRules::ResolvedActions
    resolveRuleActions(KWin::EffectWindow* w, const QString& windowId,
                       std::optional<PhosphorRules::WindowQuery>* sharedQuery = nullptr) const;

    /// The same peek-then-build resolve against the effect-VERDICT evaluator
    /// (`Tag::EffectVerdict`: OpenFullscreen / ScrollFactor). A separate
    /// evaluator, so a separate entry point: the verdict evaluator's terminal
    /// scope is `{Exclude}` only, which is what keeps an `ExcludeAnimations`
    /// rule from cancelling a scroll multiplier or an open-fullscreen
    /// decision. Its per-window match cache is independent of the animation
    /// one and is invalidated alongside it (rule_invalidation.cpp).
    PhosphorRules::ResolvedActions resolveRuleVerdictActions(KWin::EffectWindow* w, const QString& windowId) const;

    /**
     * @brief True if the window is currently snap-managed (tiled into a snap zone).
     * Its frame geometry is the zone rect, NOT a free-floating position — callers
     * that capture "pre-tile / float-back" geometry must skip such windows even on
     * fast paths, or the snap zone poisons the autotile float-back (per-mode float
     * independence). Backed by the shared snap BorderState tiled set.
     */
    bool isWindowMarkedSnapped(const QString& windowId) const;

    /// @p preTeardownScreenId: the window's screen resolved BEFORE
    /// onWindowClosed wiped the scroll tracking override — re-deriving here
    /// would fall back to position, and a parked scroll column's frame can
    /// sit on a NEIGHBOUR output.
    void notifyWindowClosed(KWin::EffectWindow* w, const QString& preTeardownScreenId);
    /// Returns false when the window is not a reportable activation target
    /// (null, own overlay, portal, plasmashell, structurally unmanageable) —
    /// the daemon-ready gate does NOT count as rejection. The bring-up
    /// re-seed uses this to fall back to the stacking walk when the raw
    /// active window is internally rejected; ordinary callers may ignore it.
    bool notifyWindowActivated(KWin::EffectWindow* w);
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
    /// getWindowScreenId for a caller that has already resolved the window id.
    /// The engine-authoritative scroll override is keyed on the window id, so
    /// the plain overload has to look it up; a caller holding one (ruleQuery)
    /// passes it here instead of paying the id-cache probe twice. Behaviour is
    /// otherwise identical — the id is the ONLY thing the overloads differ on.
    QString getWindowScreenId(KWin::EffectWindow* w, const QString& windowId) const;
    /// Resolve the KWin output a window sits on by POSITION (the output whose
    /// geometry contains the window centre), falling back to w->screen() only
    /// when no output contains the centre. Never trust w->screen() first: KWin
    /// can assign a window the wrong one of two identical-model outputs
    /// (Discussion #724). Shared by getWindowScreenId and the activation-time
    /// desktop report in notifyWindowActivated.
    KWin::LogicalOutput* windowOutput(KWin::EffectWindow* w) const;
    /// Resolve a (physical or virtual) screen id back to the KWin output that
    /// carries it. Nullptr when no connected output matches — a disconnected
    /// or not-yet-resolved id. The counterpart to outputScreenId, for the
    /// paths that hold an id and need the output's geometry.
    KWin::LogicalOutput* outputForScreenId(const QString& screenId) const;
    /// The output a scroll-strip window is managed by, or nullptr when the
    /// window is not a strip column (or is exempt: user move/resize, floating,
    /// deleted — a close-grabbed column counts as exempt for its whole close
    /// leg — or no screen is scrolling at all).
    /// The paint path compares this against the output currently being painted.
    /// Answers are memoised per output pass (see m_scrollManagedCache) so the
    /// prePaintWindow and paintWindow probes for one window cost one predicate
    /// walk between them.
    KWin::LogicalOutput* scrollManagedOutputFor(KWin::EffectWindow* w) const;
    /**
     * @brief The screen rect a scrolling-strip window's rendering AND input
     *        are confined to, or an invalid rect when no confinement applies.
     *
     * Valid only for a scroll-managed, non-floating window that is not in a
     * user move/resize: the managed output's geometry. paintWindow skips the
     * window in OUTPUT paint passes whose output is not the managed one
     * (snapshot captures are exempt via m_capturingSnapshot — the test would
     * blank a parked column's snapshot), and the overhang input filter treats
     * hits outside this rect as landing on the clipped-away (invisible)
     * overhang. One predicate, two consumers — keep them in lockstep.
     *
     * Answers an invalid rect immediately when no screen is scrolling, so the
     * common case costs one bool on the per-window-per-output-per-frame path.
     *
     * SCOPE: the confinement is the PHYSICAL output's geometry, so a strip on a
     * virtual sub-screen is not clipped at the sub-screen boundary. That is
     * intended — the point is to keep a column off a NEIGHBOURING MONITOR, and
     * both sub-screens render in the same output pass, so a same-monitor
     * overhang is drawn and remains interactive either way.
     */
    QRect scrollClipGeometryFor(KWin::EffectWindow* w) const;
    /**
     * @brief Is this strip column parked entirely off its output's viewport
     *        right now — drawn (if at all) where nobody can see it?
     *
     * True only for a scroll-managed window with a strip relocation entry
     * (m_scrollVisualDelta) whose VISUAL rect — the padded band moved by that
     * delta, plus the live view offset — intersects no part of its managed
     * output. The visual rect is where a column is drawn AT REST, which is the
     * one honest visibility test for a parked column; the committed rect is
     * always off every output (that is what parking IS) and answers nothing.
     *
     * "At rest" is the whole precision here: the WindowAnimator can be carrying
     * the same window through a per-window leg, and that term is deliberately
     * NOT folded in. For a parked column the batch path makes the leg
     * degenerate (origin == the constrained committed rect, tiling.cpp), so
     * there is no per-window motion to miss; for an unparked one the predicate
     * has already answered false on the delta probe. Do not "complete" this by
     * intersecting the animator's rect — the degenerate leg is what makes the
     * simpler test correct, and the cull would then depend on animator state
     * that changes under it mid-frame.
     *
     * FIVE consumers, and they must stay in lockstep or a column blinks or
     * burns: prePaintWindow withholds the TRANSFORMED flag (so KWin's own
     * culling is free to skip the window instead of being forced to paint
     * it), paintWindow skips the backdrop capture / decoration fold / draw,
     * the postPaintScreen repaint driver stops driving the window's
     * decoration (the ~30fps backdrop refold and the animated-pack pump),
     * prePaintScreen's tab-anchor election skips a parked column so an
     * anchor that will never paint cannot win, and StripTransitionManager's
     * above-strip election skips one when picking the stacking boundary its
     * capture composites around (falling back to the topmost PARKED member
     * when every column on the output is parked, rather than capturing the
     * whole scene). Note the anchor election runs BEFORE the strip view
     * animator advances for the frame, so its answer is one advance behind
     * the paint-path sites mid-leg — the failure is the benign,
     * already-documented one (indicators fall back to their layer slot for a
     * frame). A column that scrolls back toward the viewport starts
     * intersecting — the view offset is part of the rect, re-read every pass
     * — and the paint-path sites wake in the same frame.
     *
     * Snapshot captures must NOT consult this: a parked column's offscreen
     * capture (close snapshot, decoration capture) is legitimate work on an
     * invisible window. Both paint-path callers sit behind the
     * m_capturingSnapshot exemption already, matching the foreign-output
     * cull's treatment.
     */
    bool scrollParkedOffscreen(KWin::EffectWindow* w, const QString& windowId) const;

    /**
     * @brief Draw this pass's deferred tab-indicator surfaces into the scene
     *        walk, at the stacking position the layer-shell protocol denies
     *        them.
     *
     * The indicators live on a wlr-layer-shell surface, and a layer surface is
     * above every ordinary toplevel by protocol — restackScrollTabSurfaces can
     * order them within their layer but cannot push them below a window. So a
     * floating window raised over the strip had the indicator of the column
     * behind it painted across its content.
     *
     * The remedy is compositor-side re-slotting: prePaintScreen picks the
     * topmost scroll-managed window on the pass output as the paint anchor,
     * paintWindow calls this right after that anchor's draw completes, and the
     * indicator's own natural (layer-slot) paint is skipped once drawn here.
     * Paint order is stacking order, so the indicators composite above every
     * column but below whatever stacks over the strip — behaving like members
     * of the window layer even though the protocol has no such placement for
     * them.
     *
     * @param deviceRegion The triggering paintWindow call's device region. The
     * injected draw is clipped to it, never painted unclipped: paint order
     * only yields stacking order when every window above repaints the same
     * pixels afterwards, and KWin hands each of them only the damage region.
     * An unclipped injection put indicator pixels OUTSIDE that region, where
     * no occluder ever painted again — so the strip surfaced on top of
     * fullscreen windows, Spectacle's capture overlay, even the lock surface,
     * persisting until the next full-damage frame. The trigger's region is
     * also the CORRECT clip, not merely a safe one: for the anchor trigger it
     * is damage minus the opaque regions stacked above the strip, exactly
     * where content at the strip's slot may show; for the above-anchor
     * trigger the occluder's own paint follows immediately and resolves its
     * overlap the same way it would for a naturally-slotted window below it.
     */
    void injectScrollTabIndicators(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                   const KWin::Region& deviceRegion);

    TilingHandler* tilingHandler() const
    {
        return m_tilingHandler.get();
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
    //
    // originOverride replaces the window's CURRENT frame as the animation's
    // departure rect. Normally the two are the same — a window animates from
    // where it is. The scrolling strip is the exception: its off-viewport
    // columns are parked wherever is safe (never on a neighbouring output),
    // which is not necessarily the edge the user scrolled them off, so the
    // park rect is the wrong place to animate from. The engine sends the
    // intended edge as TileRequestEntry::scrollEdge and the caller turns it
    // into this rect. Invalid (the default) keeps the current-frame
    // behaviour.
    //
    // visualTargetOverride is the mirror, for motion that must LOOK like it
    // ends somewhere other than where the window is committed. The window
    // still moveResizes to `geometry`; only the animation (and its shader
    // morph) travels to this rect instead. The scrolling strip uses it for a
    // column leaving the viewport: it has to be seen sliding out by the edge
    // the user scrolled toward, while its committed rect is the park, which
    // is chosen for safety and may be on the far side. Both rects are
    // off-screen, so the jump between them at the end of the animation is
    // never visible. Do NOT use this to end an animation somewhere on screen —
    // the window would visibly snap at the end.
    void applyWindowGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag = false,
                             bool skipAnimation = false,
                             const QString& profilePath = PhosphorAnimation::ProfilePaths::WindowSnapIn,
                             const QRectF& originOverride = QRectF(), const QRectF& visualTargetOverride = QRectF());
    /// The rect applyWindowGeometry will REQUEST of KWin for a tile request:
    /// X11/XWayland frames are constrained to the client's WM_SIZE_HINTS and
    /// centred in the zone; everything else passes through unchanged. The
    /// scrolling batch path calls this to build animation origins and
    /// degenerate-leg comparisons against the predicted rect rather than the
    /// raw column rect (a fixed-size X11 game's column rect and committed
    /// rect differ by the centring offset).
    ///
    /// "Predict" is the honest word, not "commit" — the implementation
    /// (drag_snap.cpp) enumerates the two known divergences from what KWin
    /// finally commits, and why every consumer as written tolerates them. Do
    /// not add an equality comparand without reading that note.
    ///
    /// Idempotent, and that rests on two properties of the implementation:
    /// KWin's own constrainFrameSize is a fixed point, and the constrained
    /// size is rounded UP to the enclosing integer, never below the grid point
    /// it sits on, so a second pass floors back onto the grid within that same
    /// integer and the re-ceil returns the same size, taking no branch.
    /// Rounding to nearest instead would let a re-constrain land below
    /// the grid, floor into the previous bucket, and shift the rect again —
    /// the deferred user-move replay re-enters applyWindowGeometry with a rect
    /// this function already produced, so it depends on the round trip being a
    /// no-op.
    QRect constrainTileGeometry(KWin::EffectWindow* window, const QRect& geometry) const;
    void repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo);

    // Async D-Bus helper for 5-arg snap replies (x, y, w, h, shouldSnap).
    // Uses QDBusMessage::createMethodCall (no QDBusInterface) to avoid synchronous introspection.
    // onSnapSuccess: optional callback when snap is applied, receives (windowId, screenId)
    // onError: optional transport-error callback; valid no-snap replies still use fallback.
    void tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                          QPointer<KWin::EffectWindow> window, const QString& windowId, bool storePreSnap,
                          std::function<void()> fallback,
                          std::function<void(const QString&, const QString&)> onSnapSuccess = nullptr,
                          bool skipAnimation = false, std::function<void()> onComplete = nullptr,
                          std::function<void()> onError = nullptr);

    // The effect deliberately reserves NO screen edges. Reserving one turns on
    // KWin's electric-edge effect, whose glow and its own tile preview would
    // fight the zone overlay for the same gesture. Quick Tile is disabled
    // daemon-side via kwriteconfig6 instead, which leaves the edges free
    // without the effect having to hold them. borderActivated below still
    // exists to consume any edge activation that does reach us.

public Q_SLOTS:
    // Handle electric border activation - return true to consume the event
    // and prevent KWin Quick Tile from triggering
    bool borderActivated(KWin::ElectricBorder border) override;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class access methods — consumed across the handler split
    // (ScreenChangeHandler via applyStaggeredOrImmediate,
    // KWinCompositorBridge via clearScreenIdCache)
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
        m_idCaches.screenIdCache.clear();
        m_idCaches.connectedPhysicalIdsValid = false;
    }

    /// Connected physical screen ids, cached until the next screen
    /// add/remove/reconfigure (same invalidation points as screenIdCache).
    const QSet<QString>& connectedPhysicalIds() const;

    int animationDurationMs() const
    {
        return m_cachedAnimationDuration;
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
     * @param forceImmediate Ignore the user's sequence mode and apply every
     *                    item in one pass. For batches whose members must land
     *                    together because something else is already animating
     *                    them as a unit — a scrolling strip carried by the
     *                    per-output view spring is the case this exists for.
     *                    Staggering those would draw a column that has not
     *                    committed yet at its old rect PLUS the view offset,
     *                    i.e. one full delta the wrong way, until its own timer
     *                    fires. See slotWindowsTileRequested.
     */
    void applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                   const std::function<void()>& onComplete = nullptr, bool forceImmediate = false);

private:
    // Friend classes for helpers
    friend class TilingHandler;
    friend class SnapHandler;
    friend class ScrollOverhangInputFilter;
    friend class NavigationHandler;
    friend class ScreenChangeHandler;
    friend class SnapAssistHandler;
    friend class WindowAnimator;
    friend class DragTracker;
    friend class KWinCompositorBridge;
    friend class ShaderTransitionManager;
    friend class DesktopTransitionManager;
    friend class StripTransitionManager;
    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper class instances
    // ═══════════════════════════════════════════════════════════════════════════════
    std::unique_ptr<TilingHandler> m_tilingHandler;
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
    // FocusFadeState moved to effect_state.h.
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
    // CEILING on the per-frame ramp delta, not the cap itself. A window at rest
    // (value pinned at 0 or 1) stops being force-repainted by
    // windowSurfaceAnimates, so its FocusFadeState `lastMs` goes stale; without
    // a cap the first frame after a focus change would see a multi-second
    // `now - lastMs` and jump the whole ramp in one step (an instant snap
    // instead of a fade). advanceFocusFade (decoration_render.cpp) resolves the
    // live cap as qBound(1, focusFadeDurationMs / 2, this): the halving is what
    // keeps a SHORT duration spanning at least two frames instead of completing
    // inside one 50 ms resume step, and the floor of 1 keeps the step non-zero
    // for a 1 ms duration. A live window's real frame delta is well under the
    // ceiling, so at ordinary durations only the resume-after-idle case is
    // tamed at all.
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
    // re-fetched on every settingsChanged. The three colour strings carry a
    // hex "#AARRGGBB"; a current daemon resolves its empty follow-the-theme
    // sentinel before the value crosses D-Bus, so the "accent" token
    // (resolved to m_borderAccentColor / m_borderInactiveColor at merge
    // time, mirroring the rule colour path) only arrives from an older
    // daemon or through the rule vocabulary.
    // Scope tokens live in PhosphorCompositor::WindowAppearanceScope: "tiled"
    // (snapped OR autotile-managed), "normal" (Normal type AND not transient),
    // "all" (every window). Defaults match ConfigDefaults::windowBorderScope().
    // WindowAppearanceDefault moved to effect_state.h.
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
    ///
    /// Both halves ask the SAME question — does some surface path resolve to a
    /// non-empty enabledChain() — because that is the accessor
    /// updateWindowDecoration renders from. effectiveChain() keeps packs the
    /// user toggled off, and overriddenPaths() alone reports every registered
    /// path with no chain inspection at all, so either shortcut lets a tree
    /// that can decorate nothing make every snap / float / zone change do full
    /// per-window reconcile work. Not a hot path (reconcile, not per frame), so
    /// the resolve per overridden path is affordable. Defined in
    /// decorations.cpp with the rest of the chain-resolution code.
    bool hasDecorationTreeContent() const;

    /// True when a placement-state change could change SOME window's resolved
    /// rule outcome, so the per-window invalidation path has to run at all.
    ///
    /// The exclusion set is a separate term from the three appearance ones on
    /// purpose. It is not an animation rule, sets no appearance default and
    /// leaves no decoration-tree content, so an Exclude-only configuration makes
    /// all three false — yet isExcludedBySnappingRule caches its verdict per
    /// (windowId, rule-set revision), neither of which moves on a placement flip,
    /// and that verdict gates shouldHandleWindow / shouldDecorateWindow. Folding
    /// it in here is what stops `Exclude WHEN IsFloating` (and, since the
    /// ActiveLayout wire, `WHEN ActiveLayout = X`) freezing at its first consult.
    /// Callers still gate the expensive appearance work on the three predicates
    /// separately — this only decides whether the path is entered.
    bool hasPlacementSensitiveRuleWork() const
    {
        return !m_shaderManager.animationRuleSet().isEmpty() || hasWindowAppearanceDefault()
            || hasDecorationTreeContent() || !m_snappingExclusionRuleSet.isEmpty();
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
    // window_connections.cpp): it is cheap, and deferring it let a re-decorated
    // title bar flash for the throttle window. QPointer auto-nulls if the
    // window dies before the flush; the flush skips those entries.
    // PendingFrameGeometry moved to effect_state.h.
    QHash<QString, PendingFrameGeometry> m_pendingFrameGeometry;
    /// Last caption pushed via the caption-only metadata refresh, per window.
    /// Skips content-identical pushes (spurious captionChanged emissions).
    /// Raw-pointer-keyed; erased in the windowDeleted cleanup with its
    /// siblings.
    QHash<KWin::EffectWindow*, QString> m_lastPushedCaption;
    QTimer* m_frameGeometryFlushTimer = nullptr;
    void flushPendingFrameGeometry();

    /// One-shot wake for the parked-column GL reap (postPaintScreen). The
    /// park cull removes the repaint driver that used to keep the desktop
    /// compositing, so with nothing else damaging, the 10 s reap threshold
    /// would never be re-evaluated and the parked targets would be held
    /// indefinitely. Armed (and re-armed with the recomputed minimum) each
    /// pass while any parked window's reap is pending; fires one
    /// addRepaintFull so the next postPaintScreen runs the reap. Lazily
    /// created, parented to the effect.
    QTimer* m_parkReapTimer = nullptr;

    /// Debounce timer for `Rules.rulesChanged`. Single-shot, 50ms;
    /// timeout fires `loadRuleAnimationsFromDbus`. Re-armed on every
    /// `slotRulesChanged` invocation so a burst of per-rule mutations
    /// (a 50-rule batch edit emits 50 signals) collapses into a single
    /// `getAllRules` fetch at the trailing edge.
    QTimer m_animationRulesRefreshDebounce;

    /// Remaining bounded-retry attempts for a failed getAllRules fetch.
    /// Reset by every external loadRuleAnimationsFromDbus invocation,
    /// consumed only by fetchAllRulesOnce's failure-arm re-dispatches.
    int m_ruleFetchRetriesLeft = 0;

    /// Per-DISPATCH guard for the getAllRules fetch, the twin of
    /// TilingHandler's m_activeLayoutsQueryGeneration. Bumped by every
    /// fetchAllRulesOnce call and captured by its reply handler, so when the
    /// debounce, the bring-up load and the seed-edge re-drive put several
    /// round-trips in flight at once only the newest reply is applied. Without
    /// it an older reply landing last rewrites all five effect-bound rule sets
    /// and republishes m_activeLayoutRulesWithheld from a stale store snapshot.
    quint64 m_ruleFetchQueryGeneration = 0;

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
    ///        It ALSO suppresses the releaseDecorationGl call, so the window keeps
    ///        its OffscreenEffect redirect and its shader slot across the removal —
    ///        which is what the refresh wants (it re-asserts the same redirect a
    ///        step later) and is exactly why a genuine teardown must not pass it: a
    ///        window removed with it left true is stranded redirected, presenting
    ///        through a shader with no decoration behind it. Genuine teardown
    ///        (close, delete, undecorate) leaves it false. The three exits in
    ///        updateWindowDecoration where a refresh discovers the window is no
    ///        longer decoratable run the release themselves.
    void removeWindowDecoration(const QString& windowId, KWin::EffectWindow* windowHint = nullptr,
                                bool keepSurfaceState = false);

    /// Free a window's composite / capture / prefix / buffer GL targets — unless a
    /// shader transition is mid-flight on it, which still samples them. Every
    /// decoration TEARDOWN must route through here, or it will destroy the composite a
    /// live animation is drawing from (the compositeTexId-0 class of bug). @p target
    /// must be the EXACT window, never a fuzzy same-app sibling.
    ///
    /// Three other sites erase m_surfaceMultipass directly, and each is deliberate:
    ///   - lifecycle_wiring.cpp's surface-pack hot-reload clears the WHOLE map, because every
    ///     compiled pack is about to be recompiled and no composite survives it;
    ///   - lifecycle_wiring.cpp's windowDeleted backstop, which runs after the window is gone
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
    /// floating state flipped. Every engine routes through this (snap's
    /// clearWindowSnapped; the tiling handler's applyFloatCleanup, which
    /// serves autotile and scrolling alike) so none can regress into the
    /// teardown-now-rebuild-later shape that blanked every pack at drag
    /// start. Callers flip their engine facts first.
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
    /// @p canvasRect: the TARGET texture's canvas in logical coordinates —
    /// the device-aligned rect surfaceCanvasFor produced (the fold's
    /// logicalGeometry / SurfaceMultipassState::canvasGeo), which already
    /// carries any outer padding. Threaded in rather than re-derived from
    /// expandedGeometry + padding: the alignment shifts the canvas off the
    /// raw expanded rect by a sub-pixel band, and the geometry uniforms must
    /// describe the texture that is actually being drawn into, to the texel.
    /// Falls back to the window's expanded/frame rect when invalid.
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
                            const QRectF& canvasRect = {}, const QString& windowId = {});

    /// Advance the per-window smoothed focus value (m_focusFade) toward the
    /// hard 0/1 target and return it, so focus changes cross-fade instead of
    /// snapping. Called by pushBorderUniforms only for a pack that reads focus.
    /// Uses the pinned per-frame clock, so repeated same-frame calls (a chain
    /// with several focus-reading packs) are exact no-ops — the ramp advances
    /// at most once per frame.
    float advanceFocusFade(const QString& windowId, bool focused);

    // CompiledPackResolver moved to effect_state.h.

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
                              qreal captureScale, bool intoCaptureTex, qreal captureOpacity);

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
    /// paintedDeviceRegion: the paint region paintWindow received (device
    /// px). The blit is CLIPPED to it: outside this frame's damage the
    /// render target still holds the PREVIOUS presented frame — the full
    /// composite, including windows painted ABOVE @p w — and blitting it
    /// feeds the finished scene back into the backdrop (visible as a
    /// neighbour's padded-canvas edge re-blurred inside this window's
    /// frost).
    /// animatedFrame: where the animation is DRAWING the window this frame
    /// (WindowAnimator's current rect, or a morph transition's interpolated
    /// rect), in logical frame-rect terms. When valid, the blit SOURCE
    /// follows it (scaled into the rest-rect-sized canvas) so a frost/glass
    /// pane shows the scene behind the moving quad instead of behind the
    /// resting rect. Invalid = capture at the live geometry.
    /// backdropScale: the capture RESOLUTION relative to the composite
    /// canvas, from chainBackdropScale in paintWindow. 1.0 when some pack's
    /// main pass samples the backdrop sharp; the largest linked bufferScale
    /// when only buffer passes read it — a blur pyramid samples the capture
    /// at bufferScale resolution through normalized uvs, so texels past that
    /// density were captured, held (a full-canvas RGBA8 per window) and
    /// blitted every frame only to be skipped over by the sampler. The
    /// texture stays canvas-ALIGNED (same padded rect, same normalized
    /// backdropRect space) at reduced density; only the blit's destination
    /// arithmetic scales.
    void captureWindowBackdrop(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                               KWin::EffectWindow* w, const WindowDecoration& wb,
                               const KWin::Region& paintedDeviceRegion, const QRectF& animatedFrame = QRectF(),
                               qreal backdropScale = 1.0);

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
    /// Seeded in the constructor with a deliberately EMPTY, neutral baseline
    /// (seedDecorationTreeBaseline) — nothing is auto-inserted, because border
    /// and title-bar appearance resolve through resolveEffectiveWindowAppearance
    /// rather than through this tree; replaced wholesale when the setting
    /// arrives. Do not read this as "populated by default":
    /// hasDecorationTreeContent() is false until the user has actually applied
    /// surface packs, which is load-bearing for the invalidation gates that
    /// consult it.
    PhosphorSurfaceShaders::DecorationProfileTree m_decorationTree;

    /// Compiled surface-shader packs keyed by pack id (CompiledSurfacePack holds
    /// the main MapTexture shader, contract uniform locations, pack-declared
    /// param values, the main-pass iChannel locations, and the multipass buffer
    /// passes for that one pack). Populated on first use by compiledPack();
    /// cleared wholesale on a SurfaceShaderRegistry hot-reload (effectsChanged)
    /// and on teardown. A window's render path looks up its resolved base pack id
    /// (WindowDecoration::basePackId) here.
    std::unordered_map<QString, CompiledSurfacePack> m_compiledPacks;

    /// Per-pack CLAMPED bufferScale, cached off the registry's by-value
    /// SurfaceShaderEffect lookup for the per-frame backdrop-density resolve
    /// (chainBackdropScale in paint_pipeline.cpp). Metadata only — the
    /// linked-uniform verdicts are compile state and deliberately NOT cached
    /// here (see that lambda's comment for the two bugs a raw probe caused).
    /// Cleared wherever m_compiledPacks clears: a registry hot-reload can
    /// change the metadata too.
    std::unordered_map<QString, qreal> m_packBufferScaleCache;

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
    /// Bar count of the latest delivered spectrum; 0 when audio is off. NOT the
    /// value pushed as iAudioSpectrumSize — bindSurfaceAudio derives that from
    /// the BOUND texture's width instead, so a re-upload that failed during a
    /// bar-count change cannot let audioBar() index past the retained
    /// (still-old-size) texture for a frame. The dirty flag re-uploads at the
    /// new size on the next fold and the two converge again.
    int m_audioSpectrumSize = 0;
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
    ///   • autotile member (TilingStateHelpers::isTiledWindow) → "window.tiled"
    ///   • else snap member (SnapHandler::isTiledWindow)         → "window.snapped"
    ///   • else                                                  → "window.floating"
    /// Autotile-first precedence. The resolved profile's enabledChain() (an
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

    // Constructor wiring, decomposed from the ctor along its original comment
    // seams (definitions in lifecycle_wiring.cpp, except connectDaemonSubscriptions
    // which is in lifecycle_wiring_daemon.cpp). Each is called exactly once,
    // from the ctor, in this declared order. Not part of the public surface —
    // pure ctor decomposition, so their bodies keep the ordering guarantees the
    // inline sequence had (notably: connect the screen signals before iterating
    // the current screens() in initRenderingAndRegistries).
    void initRenderingAndRegistries();
    void initTimers();
    void connectDragTracker();
    void connectWindowAndScreenSignals();
    void connectDaemonSubscriptions();
    /// Wires the windows that already existed when the effect loaded, seeds the
    /// cursor output, and installs the overhang input filter.
    ///
    /// Split out of connectDaemonSubscriptions, which owned it only because the
    /// ctor's inline sequence had it there: none of this is daemon-facing, and
    /// keeping it under that name made the file's own "daemon subscriptions"
    /// heading false. It must still run AFTER connectDaemonSubscriptions, because
    /// the existing-window sweep can reach code that expects the subscriptions to
    /// be live.
    void initExistingWindowsAndInput();

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
    /// window has nothing that could re-resolve: no animation/appearance rules AND no
    /// config-default window appearance AND no decoration tree content AND both
    /// exclusion slices empty (their verdicts are cached the same way and can scope on
    /// placement fields). The appearance terms matter — a config-default border scoped
    /// to tiled windows must still reconcile on a snap flip with an empty rule set.
    void invalidateRuleCacheForStateChange(const QString& windowId);

    /// Targeted sibling of invalidateRuleCacheForStateChange for the
    /// frame-geometry edge: evicts ONLY @p windowId's entry from the three
    /// per-window verdict caches and re-drives that window's decoration,
    /// title-bar and layer reconciles directly. The coalesced helper above
    /// clears the GLOBAL animation match cache per flush — fine for discrete
    /// placement flips, but the geometry edge fires per 50 ms flush for the
    /// whole duration of a drag, and a global clear there cold-starts every
    /// other window's verdict twenty times a second (the same cost argument
    /// that keeps caption changes from clearing at all). The layer and
    /// title-bar reconciles are change-gated; updateWindowDecoration re-runs
    /// its chain resolve but keeps the cached prefix fold when the fold
    /// inputs have not moved, and the extra damage lands on a window that is
    /// already repainting every frame of its own motion.
    void invalidateRuleCachesForWindowGeometry(const QString& windowId, KWin::EffectWindow* w);

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
    /// schedule a border sweep to re-fold against the fresh placement. Always
    /// drops both CACHED exclusion evaluators' caches first — the placement and
    /// decoration slices; the animation slice resolves uncached and so has none
    /// to drop — and they are cleared even when the early return below fires;
    /// the rest is a no-op when there are no animation rules and no rule-held
    /// layer snapshots.
    void invalidateAllRuleCaches();

    /// Flush coalesced per-rule-cache invalidations queued by
    /// invalidateRuleCacheForStateChange within one event-loop turn: drops the
    /// match cache once and re-resolves the border / opacity of each affected
    /// window. Posted via a queued single-shot so a float toggle (which emits
    /// both windowFloatingChanged AND windowStateChanged) clears the cache once
    /// instead of twice.
    void flushPendingRuleInvalidations();

    /// Re-queue the cross-screen rule invalidations the outputChanged and
    /// virtual-screen-crossing handlers skipped because a drag was in flight.
    /// Called from every exit of the endDrag reply handling (outcome applied,
    /// D-Bus error, rejected payload, daemon timeout) so a drag that carried a
    /// window across screens always ends with its per-screen match inputs
    /// (ScreenId, ScreenOrientation, ActiveLayout) re-resolved. Empties the set,
    /// so the second call in a turn does nothing.
    void drainDragSuppressedRuleInvalidations();

    /// Resolve the per-window-rule SetHideTitleBar override for @p windowId
    /// and forward it to the DecorationManager as a tri-state rule override
    /// (unset = mode decides, true = rule hides, false = force-show veto).
    /// Windowed-fullscreen strip members are skipped outright, mirroring the
    /// layer reconciler below: the manager's veto path re-asserts geometry
    /// across a decoration flip, and mid-hold that re-assert would fight the
    /// strip's committed column rect, which the hold's whole contract says
    /// stays the slot. The un-flag paths drive updateAllDecorations, so a rule
    /// change made during the hold lands at un-flag time.
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
    /// reconcile. Also NOT applied to windowed-fullscreen strip members: the
    /// layer demotion owns their keep flags for the hold (see the early
    /// return in decoration_rules.cpp), so the two flag owners never trade
    /// writes mid-hold.
    ///
    /// Carries a STRUCTURAL own-surface shield as well: a broad match
    /// expression must never demote a dock, pin a notification, or strip the
    /// daemon overlay's own keep-above, so the own-overlay / plasma-shell /
    /// portal classes and the desktop / dock / notification / OSD types
    /// resolve as rule-free. Transients and popups are deliberately NOT
    /// shielded — transient exclusion is per-feature user opt-in in this
    /// project (the IsTransient match field), never hardcoded policy. A
    /// shielded window resolves rule-free rather than early-returning, so one
    /// that was rule-held BEFORE its class mutated into a shielded one still
    /// drains its snapshot through the restore branch.
    void reconcileRuleWindowLayer(const QString& windowId, KWin::EffectWindow* w);

    /// One-shot fullscreen-at-open verdict for the OpenFullscreen rule:
    /// true fullscreens the opening window, false vetoes the app's own
    /// fullscreen request. Called exactly once per window, from
    /// slotWindowAdded ahead of the routing block. The flip writes KWin's
    /// REQUESTED bit synchronously while the committed one lags a client
    /// round-trip on Wayland, so the announce path is kept honest by
    /// isEligibleForTilingNotify rejecting on requested-OR-committed
    /// fullscreen rather than by the commit having landed. Never re-reconciled — a
    /// rule edit mid-session leaves open windows alone (niri's
    /// open-fullscreen contract), which is also why there is no snapshot /
    /// restore pair here.
    void applyRuleOpenFullscreen(const QString& windowId, KWin::EffectWindow* w);

    /// The ScrollFactor rule's multiplier for @p w, or nullopt when no
    /// enabled rule scales it (or none exists at all — the no-rules fast
    /// path is two pointer reads). Consulted by ScrollOverhangInputFilter's
    /// pointerAxis per wheel tick; the resolve rides the verdict evaluator's
    /// per-window cache (resolveRuleVerdictActions), so repeats are a hash
    /// lookup. The verdict evaluator — not the animation one — is what keeps
    /// an ExcludeAnimations rule from cancelling the multiplier.
    std::optional<qreal> ruleScrollFactorFor(KWin::EffectWindow* w) const;

    /// The window's OWN keep-above flag — the app/user-set state, with
    /// written values substituted from the pre-write snapshot while either
    /// flag owner (a SetWindowLayer rule, or the windowed-fullscreen layer
    /// demotion) holds the window's layer; the rule snapshot wins when both
    /// exist, since only it predates the rule's write. Consulted by the
    /// keep-above overlay-tool gates (shouldHandleWindow / shouldDecorateWindow
    /// / isTileableWindow) and the engine-facing KWinCompositorBridge::windowInfo
    /// export; applyOwnLayerFlags is the query-side counterpart.
    bool windowOwnKeepAbove(KWin::EffectWindow* w) const;

    /// Substitute the pre-write snapshot's keepAbove/keepBelow pair into
    /// @p query while either flag owner (a SetWindowLayer rule, or the
    /// windowed-fullscreen layer demotion) holds @p windowId's layer, so
    /// neither owner's output feeds back into rule input (rule snapshot
    /// first when both exist). Shared by ruleQuery (the effect's live
    /// evaluation path) and pushWindowMetadata (the daemon's
    /// KeepAbove/KeepBelow match inputs) — the one invariant lives in one
    /// place. No-op with no snapshots (the no-rules case pays two isEmpty).
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
    // WindowLayerSnapshot moved to effect_state.h.
    QHash<QString, WindowLayerSnapshot> m_ruleWindowLayerSnapshots;

    std::unique_ptr<NavigationHandler> m_navigationHandler;
    std::unique_ptr<ScreenChangeHandler> m_screenChangeHandler;
    std::unique_ptr<SnapAssistHandler> m_snapAssistHandler;
    // The two snap-assist toggles cached from independent D-Bus replies; the
    // handler is enabled on their AND (see loadCachedSettings). Both seed
    // false so the handler stays off until the replies land, matching the
    // pre-split cold-start behaviour.
    bool m_snapAssistFeatureEnabled = false;
    bool m_snapAssistBehaviorEnabled = false;
    // Per-output motion clocks. One `CompositorClock` per `LogicalOutput`
    // so mixed refresh-rate displays (e.g., 60 Hz + 144 Hz) phase-lock
    // independently — see IMotionClock docs. Populated on construction
    // from `KWin::effects->screens()` and maintained via the
    // screenAdded/screenRemoved signals. A fallback unbound clock is
    // always present for the degenerate no-output / migrated-window
    // cases — for `m_windowAnimator` only; `m_stripViewAnimator`'s resolver
    // deliberately returns null on a miss, since a view offset belongs to an
    // output and an unresolvable one has nothing to slide. Every clock
    // outlives BOTH animators — each holds non-owning pointers into these via
    // captured MotionSpecs — guaranteed by destruction order (both animators
    // declared after). Anyone reordering these members has to keep that true
    // for both.
    std::unique_ptr<CompositorClock> m_motionClockFallback;
    std::unordered_map<KWin::LogicalOutput*, std::unique_ptr<CompositorClock>> m_motionClocksByOutput;
    /// The output whose pass is currently executing, latched in prePaintScreen
    /// and cleared in postPaintScreen. The scroll-strip overhang suppression
    /// compares against this by IDENTITY rather than testing the pass viewport
    /// against the managed output's rect: the rect test silently assumes
    /// renderRect() is expressed in global logical coordinates, and if any pass
    /// builds it output-local instead, the neighbouring output's viewport reads
    /// as (0,0,w,h), overlaps the managed output's rect, and the cull never
    /// fires. Comparing pointers cannot be wrong about which output is being
    /// painted. Null only OUTSIDE any prePaintScreen→postPaintScreen bracket
    /// (defensive bootstrap, test harnesses, a null data.screen from KWin) —
    /// offscreen captures run INSIDE a pass and keep that pass's output,
    /// which is what makes the desktop-capture cull agree with the live
    /// scene; the window-snapshot captures are exempted by
    /// m_capturingSnapshot instead. With the latch null the suppression does
    /// not engage (fails open).
    KWin::LogicalOutput* m_currentPassOutput = nullptr;
    /// Per-pass memo for scrollManagedOutputFor: prePaintWindow and
    /// paintWindow each probe the predicate for every window, and its chain
    /// (id lookup, tiled-bucket scan, float check, output resolve) is not
    /// free at per-window-per-output-per-frame rate. Cleared in
    /// prePaintScreen when the pass begins, and consulted/populated ONLY
    /// while a pass is executing — the input filter shares the predicate but
    /// runs between passes, where a tile batch may just have moved a column,
    /// so it always computes fresh. In default clamp mode the answer never
    /// differs from the window's own output (committed geometry cannot
    /// cross), so the cache also bounds what that mode pays for a cull that
    /// cannot fire for it.
    ///
    /// Deliberately NOT cleared in postPaintScreen beside the other per-pass
    /// state, and that is safe for one reason only: the clear that matters is
    /// prePaintScreen's, which runs BEFORE the pass's first read, and every
    /// read is gated on being inside a pass. Entries can therefore outlive
    /// their windows between passes — both key and value are raw pointers —
    /// but a stale entry is never read, and keys are only hashed by pointer
    /// value, never dereferenced. Anyone adding a read that is NOT behind the
    /// in-pass gate must add the postPaintScreen clear first.
    mutable QHash<KWin::EffectWindow*, KWin::LogicalOutput*> m_scrollManagedCache;

    /// Latched by StripTransitionManager around its capture's paintScreen:
    /// while set, paintWindow skips every window in the above-strip set
    /// below and records it, so the pass can composite exactly that set
    /// sharp on top of the shader output. Null outside a capture. The
    /// stored value is the capture's output; the record site only tests it
    /// for truthiness (membership in the set already encodes the output),
    /// so treat the pointer as a latch with a debugging-friendly value, not
    /// as something compared against.
    KWin::LogicalOutput* m_stripCaptureExclusionOutput = nullptr;
    /// Which windows the current capture excludes: everything ABOVE the
    /// topmost strip member (a column managed by the capture output, or its
    /// tab-indicator surface) in KWin's stacking order that also intersects
    /// the capture output. Prebuilt by StripTransitionManager::paintOutput
    /// right before its capture and cleared by the same scope guard as the
    /// latch — a stacking FACT, where the old role-based predicate promoted
    /// below-strip floats and closing columns above the shader output.
    QSet<KWin::EffectWindow*> m_stripCaptureAboveStrip;
    /// The windows skipped by the current capture, in paint (bottom-to-top
    /// stacking) order. Filled while the latch above is set; consumed by
    /// the same paintOutput call on the normal path, and cleared by an
    /// unwind guard when the capture's scene walk throws — either way
    /// entries never outlive the frame.
    QList<KWin::EffectWindow*> m_stripCaptureSkippedWindows;
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
    /// Scrolling-strip view motion, one spring per output. Separate from
    /// m_windowAnimator by GRANULARITY, not by kind: a scroll moves the whole
    /// strip by one amount, and folding that into per-window targets would
    /// make N springs that desync into a shear. The two compose additively at
    /// paint time — a window can be riding the view AND animating a residual
    /// of its own (an edge column whose width changed in the same batch).
    /// Rides the same per-output clocks, but with NO fallback, and holds no
    /// profile of its own — the caller resolves the scrolling.view motion node
    /// per batch and hands it in.
    std::unique_ptr<StripViewAnimator> m_stripViewAnimator;
    /// How far a PARKED scrolling column's drawing must be translated from
    /// its committed rect to sit at its strip position, by window id. Stored
    /// as the strip-minus-park DELTA of the batch's rects rather than the
    /// strip position itself: the committed frame is not always the park rect
    /// (applyWindowGeometry's X11 constrain-and-centre pass offsets a
    /// fixed-size client within it), and the delta rides on top of whatever
    /// was committed, preserving that offset — an absolute position erased it
    /// and drew such windows at the column's top-left. The committed rect is
    /// the park below the union of all outputs — the only rect that cannot
    /// stray onto a neighbouring monitor — so the paint path applies this
    /// delta and then adds the view offset, which keeps the column travelling
    /// with the rest of the strip instead of vanishing the moment it leaves
    /// the viewport. Absent for every window drawn at its committed rect,
    /// which is almost all of them.
    /// DAMAGE CONTRACT: adding, changing or removing an entry moves where
    /// the paint path draws the window, so every mutation site must either
    /// pair with addRepaint(Full) or sit on a path whose follow-up geometry
    /// apply (or membership clear that already stopped the relocation)
    /// provably damages — the batch writer change-gates and damages, and
    /// the removers each document which half covers them.
    /// Note the drawn position has TWO inputs under the delta form, this
    /// entry and the committed rect, where the absolute form it replaced had
    /// only one. The contract above covers the entry half. The committed
    /// half is NOT paired: a mover that changes the commit while the entry is
    /// unchanged (the X11 counter-assert, the windowed-fullscreen ack
    /// re-commit) damages the regions of the COMMIT, and for a column parked
    /// below the union of all outputs those regions intersect no output — so
    /// nothing damages where the window is actually drawn.
    ///
    /// What makes that survivable is a condition, not a property of having an
    /// entry: it holds only while the drawn position (commit + entry + view
    /// offset) touches no viewport, which is exactly what
    /// scrollParkedOffscreen decides and re-decides every pass. Mid-leg the
    /// view spring damages every frame regardless. At rest, a column whose
    /// drawn position DOES touch a viewport would hold a stale frame until
    /// something unrelated damaged it. Do not read "has a delta entry" as
    /// "off-viewport" — the predicate exists because the two are not the same
    /// question — and give any NEW commit-half mover its own repaint pairing
    /// rather than inheriting this argument.
    QHash<QString, QPoint> m_scrollVisualDelta;
    /// Windows in scrolling WINDOWED FULLSCREEN: the client holds KWin
    /// fullscreen state (set by the effect from the batch flag) while the
    /// committed rect stays the column slot, stored here as the value. The
    /// single source for every fullscreen exemption this feature needs — the
    /// applyWindowGeometry bail, tiling eligibility, and the screen-leave
    /// demote all consult membership, so it means "this window's fullscreen
    /// is OURS, keep managing it". The rect exists because KWin re-asserts
    /// the FullScreenArea when the client's fullscreen ack COMMITS, one
    /// round-trip after the batch already applied the column rect — the
    /// committed windowFullScreenChanged signal is where the column rect is
    /// re-asserted, and by then the batch is long gone. Maintained by
    /// TilingHandler's batch consumer and windowFullScreenChanged
    /// reconciliation. Membership is removed by the FORGET helper
    /// (forgetWindowedFullscreen) and by the direct removals on: close and
    /// the windowDeleted backstop, both float cleanups (active and passive
    /// channels), the batch un-flag and deferred-reconcile arms, the
    /// cross-output transfer, the mode-swap / screen-removal demotes, the
    /// untile pass, and the bulk drain (restoreAllWindowedFullscreen).
    /// releaseWindowedFullscreenState removes NOTHING here — it is the
    /// compositor-state drop only and deliberately never consults this
    /// hash; every membership removal pairs with a release or a
    /// layer-demotion restore.
    QHash<QString, QRect> m_windowedFullscreenWindows;
    /// Pre-demotion keep-above/keep-below flags for windowed-fullscreen
    /// windows. The feature holds keep-below on every flagged window because
    /// KWin's belongsToLayer() promotes an active fullscreen window to the
    /// ActiveLayer — and KEEPS it there while the active window sits on a
    /// different output — stacking the tile above its strip neighbours and
    /// the daemon's overlay surfaces; keep-below is the one input that layer
    /// resolve consults before the fullscreen promotion. Written by
    /// TilingHandler::applyWindowedFullscreenLayerDemotion (snapshot-once) and
    /// drained by its restore counterpart on every un-flag path; entries for
    /// closing windows are dropped beside the membership removals. Separate
    /// from m_ruleWindowLayerSnapshots: reconcileRuleWindowLayer skips flagged
    /// windows entirely, so the two owners never trade flags mid-hold.
    QHash<QString, WindowLayerSnapshot> m_windowedFsLayerSnapshots;
    /// Last minimum size reported to the daemon per managed window. KWin
    /// exposes Window::minSize with no change signal, so the batch consumer
    /// polls it per applied entry and re-reports through
    /// Tiling.windowMinSizeUpdated when it moved — clients that set their
    /// size hints after mapping (Wine games pin theirs to the configured
    /// resolution once up) otherwise leave the engine modelling a column
    /// the clamped real frame can never match. Seeded at announce (rolled
    /// back on a failed BATCH announce; the single-window error arm relies
    /// on the re-announce re-seeding instead). Dropped on close and the
    /// deleted backstop, evicted per-window by the min-size discovery leg
    /// (so the next batch re-asserts the true pair), and cleared wholesale
    /// on daemon loss AND at onDaemonReady (handover). NOT dropped by
    /// cleanupAutotileTracking — the re-announce re-seeds it inline.
    QHash<QString, QSize> m_lastReportedMinSize;
    /// Per scroll-managed X11 window: the rect the last batch commanded, so
    /// an EXTERNAL move can be detected and countered. X11 clients can
    /// reposition themselves through ConfigureRequests KWin honors — a Wine
    /// game re-asserting its saved window position was seen live undoing
    /// the strip's parks and straddles (the window crawled back on-screen
    /// over its neighbour's column, and the engine's emit-on-change gate
    /// stayed silent because its own rects never moved). Written by the
    /// batch apply, consumed by TilingHandler::slotWindowFrameGeometryChanged
    /// (counter-assert RATE-LIMITED to 3 per rolling second, re-armed by
    /// every fresh batch command — a client that refuses to stay put is
    /// countered at that ceiling indefinitely, it does not win outright).
    /// Wayland windows are covered by m_tileTargetZones instead and never
    /// appear here. Dropped on close, the deleted backstop, float cleanup
    /// (both channels), the untrack funnel (cleanupAutotileTracking), the
    /// per-batch disarm when the commit deferred or the fullscreen bail
    /// fired (load-bearing: it disarms the counter rather than recording a
    /// drag-time frame), and cleared wholesale on daemon loss and at
    /// onDaemonReady.
    QHash<QString, ScrollCommandedRect> m_scrollCommandedRects;
    /// wl_surface object ids of the daemon's scrolling tab-indicator surfaces,
    /// announced over D-Bus. The paint path slides these with the strip so the
    /// indicators travel with the columns they label.
    ///
    /// Held as a flat set because the paint path only asks "is this window one
    /// of them" and resolves the output from the window itself. The per-screen
    /// map beside it exists solely so an announcement can retract the id it
    /// replaces — the signal names a screen, not the id going away.
    ///
    /// Ids are dropped when the daemon retracts them and cleared wholesale at
    /// bringup: Wayland reuses object ids, so a registration outliving its
    /// surface would come to name an unrelated one.
    QSet<quint32> m_scrollTabSurfaceIds;
    QHash<QString, quint32> m_scrollTabSurfaceIdsByScreen;

    /// Per-output-pass state for the tab-indicator paint re-slotting (see
    /// injectScrollTabIndicators). Recomputed by prePaintScreen at the top of
    /// every pass, valid only inside its bracket — the same scope contract as
    /// m_scrollManagedCache, and for the same reason: one pass guarantees the
    /// stacking order cannot change under the answer.
    ///
    /// The anchor is the topmost scroll-managed window on the pass output that
    /// the scene will actually draw this frame; the deferred set holds that
    /// output's indicator surfaces, which paintWindow skips at their natural
    /// layer slot once drawn; the drawn set records the injection so the skip
    /// and the fallback (anchor never painted — paint at the natural slot
    /// after all) cannot disagree.
    ///
    /// The above-anchor set holds every OTHER window stacked over the anchor
    /// that is on the pass output or intersects it (a straddler assigned to
    /// the neighbouring output paints — and occludes — in this pass all the
    /// same) — dialogs raised over the strip, the passive
    /// overlay shell carrying an OSD. It exists because the anchor's own
    /// paint is not a reliable injection trigger: the scene culls a fully
    /// occluded anchor, and a culled anchor used to mean no injection at all,
    /// leaving the indicators to paint at their natural layer slot ON TOP of
    /// the very windows covering the strip. Whether the anchor was culled
    /// depends on what covered it that frame, so the indicators FLICKERED
    /// between correctly-stacked and over-everything as occlusion came and
    /// went. paintWindow now also injects just before the first above-anchor
    /// window paints, so the indicators land under it whether or not the
    /// anchor survived culling — an occluded anchor implies a painting
    /// occluder above it, so one of the two triggers always fires.
    KWin::EffectWindow* m_scrollTabPaintAnchor = nullptr;
    QSet<KWin::EffectWindow*> m_scrollTabDeferred;
    QSet<KWin::EffectWindow*> m_scrollTabDrawn;
    QSet<KWin::EffectWindow*> m_scrollTabAboveAnchor;

    // Phase 6: per-window shader transitions via OffscreenEffect.
    // Shader/texture cache, LRU eviction, warm-up pipeline, profile tree,
    // and transition lifecycle are managed by ShaderTransitionManager.
    ShaderTransitionManager m_shaderManager;

    // Full-screen desktop transitions: the virtual-desktop switch and the
    // show-desktop peek, which share the same path. By-value + `this` ctor,
    // same ownership shape as m_shaderManager; must be initialised AFTER it in
    // the ctor init list to match declaration order.
    DesktopTransitionManager m_desktopTransition;

    // The scrolling strip's per-output shader pass (`scrolling.view`,
    // appliesTo ["strip"]): a velocity-driven post-process over the live
    // scene while StripViewAnimator's view spring is in flight. Armed from
    // the tiling batch path (notifyLeg); liveness belongs to the spring.
    // Same ownership shape and init-order rule as m_desktopTransition.
    StripTransitionManager m_stripTransition;

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
    /// Resolve the compiled shader program for @p effectId, compiling it (read
    /// source, assemble entry point, expand includes, splice the param preamble +
    /// HDR finalize + PLASMAZONES_KWIN define, generate the KWin custom shader,
    /// and cache every uniform location) on the first miss. Assumes the GL
    /// context is already current — the sole caller (beginShaderTransition) makes
    /// it current before the call, since the same context also drives its texture
    /// uploads. Returns a pointer to the cached entry, whose `shader` is null when
    /// this @p effectId is a cached compile-failure sentinel. Returns nullptr on a
    /// transient failure (missing / empty / unexpandable source) that is NOT
    /// cached, so a later trigger re-attempts. Definition in shader_textures.cpp.
    const CachedShader* compileOrLoadAnimationShader(const QString& effectId,
                                                     const PhosphorAnimationShaders::AnimationShaderEffect& eff);
    void endShaderTransition(KWin::EffectWindow* window);

    // First-frame open suppression — implementations in window_lifecycle.cpp.
    // beginRestoreSuppression withholds a window from compositing the moment
    // it opens; endRestoreSuppression releases it once it has settled into
    // its zone / tile (or on the hard deadline). See RestoreSuppression.
    void beginRestoreSuppression(KWin::EffectWindow* window);
    /// Re-arm an already-suppressed window's deadline (no-op when the window
    /// is not suppressed). Used when a routing decision is deferred past the
    /// original deadline (screen-query wait) so the window does not flash at
    /// its spawn placement mid-route.
    void refreshRestoreSuppressionDeadline(KWin::EffectWindow* window);
    /// Consume (single-shot) and, when valid for a snap-mode screen, apply the
    /// instant snap-restore cache entry for this window's app. Returns true
    /// when the window was teleported (caller should re-evaluate its screen).
    /// Shared by slotWindowAdded and the deferred-routing dispatch so a
    /// deferred window cannot leave a stale entry for a same-app sibling.
    bool tryInstantSnapRestore(KWin::EffectWindow* w, const QString& windowId, bool canSnapRestore);
    void endRestoreSuppression(KWin::EffectWindow* window);

    void loadShaderProfileFromDbus();
    void loadMotionProfileTreeFromDbus();
    void loadShaderRegistryFromDbus();
    /// @param outOwnsResolvedLeg when non-null, receives true iff the live
    /// transition after this call is THIS event's leg — either freshly
    /// installed, or the same-effect short-circuit kept a leg whose cached
    /// shader IS this event's resolved pack (the identity test heldMove
    /// stamping uses). False on every early return (no shader assigned,
    /// applicability refusal, compile failure, filter rejection). Callers
    /// that mutate the transition after this call (the maximize morph
    /// endpoint writes) MUST gate on it: findTransition alone hands back
    /// whatever leg is live, and mutating an unrelated event's leg
    /// re-anchors its drawn rect mid-flight.
    void tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                bool reverse = false, bool holdCloseGrab = false, bool holdAddedGrab = false,
                                bool animateMinimized = false, bool* outOwnsResolvedLeg = nullptr);
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
    // window_connections.cpp beside its two call sites.
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
    /// The single owner of server-side decoration (title-bar) state. Every
    /// hide/restore goes through its owner model — handlers and the rule
    /// layer must never call KWin::Window::setNoBorder directly. Reached
    /// through this member from inside the effect's own TUs (the handlers are
    /// friends); there is deliberately no public accessor, so no code outside
    /// that boundary can acquire the manager and drive it directly.
    std::unique_ptr<DecorationManager> m_decorationManager;

    // Keyboard modifiers from KWin's input system
    // Updated via mouseChanged; that's the only reliable way to get modifiers in a
    // KWin effect on Wayland (QGuiApplication doesn't work here).
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;
    Qt::MouseButtons m_currentMouseButtons = Qt::NoButton;
    bool m_keyboardGrabbed = false;
    // Re-entrancy guard: true while a WINDOW-RECT offscreen capture walks the
    // chain, so paint/apply hooks behave plainly during the raw capture pass
    // (no morph quad deform / re-capture). TWO setters:
    // captureOldWindowSnapshot (paint_capture.cpp) and captureWindowSurface
    // (surface_capture.cpp) — both build their viewport from the WINDOW's
    // rect rather than an output's, which is why the foreign-output cull and
    // the strip-capture exclusion both exempt on this flag.
    bool m_capturingSnapshot = false;

    /// True while prePaintScreen has switched vertex snapping to None for an
    /// in-flight animation. Mirrors the mode we last set so the per-frame
    /// toggle only calls setVertexSnappingMode on the edges. Starts false:
    /// KWin's default is Round and the ctor leaves it there (see
    /// initRenderingAndRegistries).
    bool m_vertexSnappingDisabled = false;

    /// True while a direct-drive caller runs paintWindow OUTSIDE KWin's chain
    /// walk. THREE setters: DesktopTransitionManager::compositeWindowsInto —
    /// the shared tail of both desktop captures, captureDesktop (the switch
    /// legs) and capturePeekWindowsScene (the peek's windows layer) —
    /// StripTransitionManager's top-composite, which draws the above-strip
    /// windows onto the SCREEN target after its quad (not a capture, and
    /// per-frame for the whole leg), and injectScrollTabIndicators, which
    /// draws the tab-indicator surfaces at the anchor's stacking slot rather
    /// than at their own (also not a capture, and on every frame a strip
    /// carries a tabbed column). paintWindow's tail then terminates
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
    // Mirrors ConfigDefaults::decorationAnimateFocusedOnly() — the daemon pushes
    // the real setting on connect, but until it does (or when it is gone) the
    // effect must sit on the same default the settings UI shows.
    bool m_animateFocusedOnly = true;

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
     * @brief Whether this drag's cursor/modifier ticks must reach the daemon.
     *
     * True once any activation family is in play, and latched thereafter via
     * m_dragActivation.detected so a mid-drag release does not silence the
     * stream the daemon's rising-edge latches depend on.
     *
     * Pure predicate. It does NOT take the keyboard grab, despite the name it
     * used to carry: the grab is the SNAP path's, taken unconditionally at
     * dragStarted so Escape reaches cancelSnap rather than KWin's
     * MoveResizeFilter, and engine-owned drags deliberately take none. Doing
     * it here made a held drag-insert trigger (the shipped default is Alt)
     * swallow the keyboard on an ordinary snap-screen drag.
     */
    bool shouldForwardDragTicks();

    // beginDrag is called unconditionally at drag-start; the deferred-send
    // optimization is obsolete now that the daemon always knows about the drag.

    // Drag-gate exclusion rule set — the placement-exclusion slice
    // (Exclude ∪ ExcludePlacement) of the unified Rule store the effect
    // mirrors over D-Bus. Filled by loadRuleAnimationsFromDbus's parse step
    // (which already deserialises the full rule set for the animation
    // override path), via
    // `PhosphorRules::ExclusionRules::excludePlacementRulesFrom`. The
    // bound RuleEvaluator drives shouldHandleWindow()'s exclusion gate.
    // Declaration ORDER MATTERS — the rule set must precede (and outlive)
    // the evaluator that binds a reference to it.
    PhosphorRules::RuleSet m_snappingExclusionRuleSet;
    PhosphorRules::RuleEvaluator m_snappingExclusionEvaluator{m_snappingExclusionRuleSet};

    // Decoration exclusion rule set — the decoration-exclusion slice
    // (Exclude ∪ ExcludeDecorations) of the unified Rule store, filled at
    // the same loadRuleAnimationsFromDbus sync point via
    // `PhosphorRules::ExclusionRules::excludeDecorationsRulesFrom`. The
    // bound RuleEvaluator drives shouldDecorateWindow()'s exclusion gate:
    // blanket Exclude keeps stripping decorations (the behavior from when
    // that gate reused the snapping slice), while the scoped
    // ExcludeDecorations strips only decorations. Same declaration-order
    // contract as the pair above.
    PhosphorRules::RuleSet m_decorationExclusionRuleSet;
    PhosphorRules::RuleEvaluator m_decorationExclusionEvaluator{m_decorationExclusionRuleSet};

    // True when any enabled rule in the unified store references a frame-
    // geometry match field (Width / Height / PositionX / PositionY).
    // Recomputed at the loadRuleAnimationsFromDbus sync point. Gates the
    // geometry-edge rule-cache invalidation in flushPendingFrameGeometry:
    // those fields are stamped live into the per-window query but the
    // verdict caches key on (windowId, ruleSet revision), so without an
    // invalidation edge a `Width LessThan N` appearance or exclusion verdict
    // pins at the window's first resolve. The per-tick geometry lambda must
    // NEVER invalidate directly (discussion #816 — animated geometry fires
    // hundreds of ticks per second); the 50 ms flush plus this set-level
    // gate keeps the no-geometry-rules user at zero cost.
    bool m_hasGeometryScopedRules = false;

    // True when the last completed loadRuleAnimationsFromDbus admission pass
    // ran with the active-layout map UNSEEDED and dropped at least one rule
    // that would otherwise have been bound to an effect rule set, purely
    // because it references Field::ActiveLayout (see effectNeverStampedFields
    // in shader_config_dbus.cpp).
    //
    // Read by exactly one consumer: the seeding edge in
    // TilingHandler::setActiveLayouts, which re-drives the whole rule fetch so
    // the held-out rules are admitted. Without this marker that re-drive is
    // unconditional, and every session with no ActiveLayout rule at all — the
    // overwhelming majority — pays a getAllRules round-trip, a full RuleSet
    // parse and an updateAllDecorations sweep for rules that do not exist.
    //
    // Cleared on exactly one path: consumption by that seeding edge. Every
    // loadRuleAnimationsFromDbus reply that PARSES recomputes it outright
    // (shader_config_dbus.cpp assigns the pass verdict, never ORs). The
    // refusal arms (over-cap payload, non-object JSON, RuleSet::fromJson
    // refusal) return before that assignment and RE-ARM the marker to true on
    // the way out: the seeding edge consumed it before dispatching the fetch,
    // and those arms admit nothing, so leaving it false would disarm the next
    // unseed→seed cycle while the rules are still out of every evaluator.
    // TRUE is the safe polarity — a spare re-drive is one redundant fetch.
    // The unseeding paths themselves deliberately do NOT
    // clear it — see
    // TilingHandler::clearActiveLayoutsForTeardown, which re-slices the
    // ActiveLayout rules out of the five rule sets and SETS this marker when
    // it removed any. Clearing on teardown or bring-up would disarm the edge
    // for the session whenever the following getAllRules never lands; a
    // stale-TRUE marker only costs one redundant re-drive.
    bool m_activeLayoutRulesWithheld = false;

    // Minimum window size for autotile eligibility. Windows smaller than this
    // are rejected by isEligibleForTilingNotify() to prevent small utility
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
    // (min-size 0). The rule-driven exclusion gate binds the dedicated
    // decoration slice (m_decorationExclusionEvaluator above).
    bool m_decorationExcludeTransientWindows = true;
    // Plasma panel opt-in, same group and the same default-preserving reason:
    // panels were never decorated before, so this starts excluded and a
    // pre-D-Bus pass leaves them alone. Decorations-only — there is no
    // snapping or animation twin because both of those filters reject
    // plasma-shell surfaces structurally.
    bool m_decorationExcludeShellPanels = true;
    bool m_decorationExcludeShellAppletPopups = true;
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

    // Autotile: true when the current drag was started on an engine-managed (autotile or scrolling) screen
    // (callDragStarted was skipped). Captured at drag start so the drag end
    // handler uses the same decision, preventing a race where m_managedScreens
    // changes mid-drag (e.g., async D-Bus signal) and leaves the popup visible.
    bool m_dragBypassedForEngine = false;
    QString m_dragBypassScreenId; // Screen at drag start (for float D-Bus call on drag end)

    // Cached activation settings (loaded from daemon via D-Bus, updated on settingsChanged)
    // Used for local trigger checking to gate D-Bus calls (see anyLocalTriggerHeld)
    //
    // Defaults are PERMISSIVE (matching old always-send behavior) so that during the
    // startup window before async loads complete, no D-Bus calls are incorrectly skipped.
    // Once real settings arrive, they override these conservative defaults.
    QVector<ParsedTrigger> m_parsedTriggers; // pre-parsed via TriggerParser::parseTriggers() at load time (avoids
                                             // QVariant unboxing in hot path)
    // Drag-insert trigger lists, cached so shouldForwardDragTicks can force
    // tick forwarding while a HOLD-mode insert trigger is physically held
    // (the toggle bools below cover toggle mode only; without these, a drag
    // starting off-engine could never reach hold-mode drag-insert).
    QVector<ParsedTrigger> m_parsedAutotileDragInsertTriggers;
    QVector<ParsedTrigger> m_parsedScrollingDragInsertTriggers;
    bool m_triggersLoaded =
        false; // false until D-Bus reply arrives — permissive default bypasses trigger gating (#175)
    bool m_cachedToggleActivation = false;
    bool m_cachedAutotileDragInsertToggle = false;
    bool m_cachedScrollingDragInsertToggle = false;
    bool m_cachedZoneSpanToggleMode = false;
    // AutotileDragBehavior cached so the synchronous drag-start fast path can
    // decide whether to skip the handleDragToFloat(immediate=true) call.
    // Refreshed by loadCachedSettings on every settingsChanged D-Bus
    // notification. Unknown values clamp to the safe default (Float) rather
    // than the highest-known value so an older effect build against a newer
    // daemon doesn't silently enter the wrong mode.
    EffectAutotileDragBehavior m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
    bool m_cachedZoneSelectorEnabled = true; // true until proven false — ensures dragMoved passes through at startup
    // Same rule as the duration two members below: seeded from the canonical
    // constant, not an inline literal. It was 0 (all at once) while the
    // shipped default is the cascade, so every batch apply before the async
    // reply lands — bringup included — ran the wrong sequencing.
    int m_cachedAnimationSequenceMode = PhosphorAnimation::Limits::DefaultAnimationSequenceMode;
    // Pinned to the canonical Limits constant rather than an inline magic
    // number so a future bump in the suite-wide default propagates here
    // automatically and a malformed daemon reply (zero/negative) clamped
    // through Limits at the assignment site stays structurally safe even
    // before the first reply arrives.
    int m_cachedAnimationDuration =
        PhosphorAnimation::Limits::DefaultAnimationDurationMs; // ms, fallback until loaded from daemon
    // ms between each window start when cascading. Canonical constant for the
    // reason the member above now gives; it was 30 against a shipped 40.
    int m_cachedAnimationStaggerInterval = PhosphorAnimation::Limits::DefaultAnimationStaggerIntervalMs;
    /// Mirror of the scrollingCropStraddlers setting, and the BRING-UP-ONLY
    /// fallback for the direct-scanout gate: blocksDirectScanout consults it
    /// solely while the daemon's resolved per-screen crop map is unseeded, so
    /// the gate is never worse than the old global-flag test before the first
    /// reply lands. Once the map is seeded, membership in it is the whole
    /// answer — that is what lets a per-context SetScrollCropStraddlers=false
    /// hand direct scanout back while the global setting stays on (a surface
    /// presented on a hardware plane bypasses the effect chain and with it
    /// the crop, so the gate only forces composition where a crop is actually
    /// resolved).
    bool m_cachedScrollCropStraddlers = false;

    // Per-drag activation / float tracking. Fields + rationale in effect_state.h
    // (DragActivationState).
    DragActivationState m_dragActivation;

    // Per-rule-cache invalidations accumulated within one event-loop turn,
    // flushed once by flushPendingRuleInvalidations(). Coalesces the double
    // invalidation a float toggle triggers (windowFloatingChanged + windowStateChanged).
    QSet<QString> m_pendingRuleInvalidations;

    // Window ids whose cross-screen rule invalidation was suppressed because a
    // drag was in flight when the screen change landed. The outputChanged and
    // virtual-screen-crossing handlers stamp m_trackedScreenPerWindow
    // unconditionally, so at drag end the tracked screen already equals the
    // live one and no comparison there can recover the skipped invalidation.
    // Each suppressed handler records the id here instead, and callEndDrag
    // drains the set once the daemon's outcome has been applied. Cleared on
    // daemon loss, where invalidateAllRuleCaches supersedes it. An id whose
    // window died meanwhile is harmless: the flush's findWindowById returns
    // null and skips it.
    QSet<QString> m_dragSuppressedRuleInvalidations;

    // Set while a coalesced border sweep is queued for the end of the turn (see
    // scheduleBorderSweep); collapses a burst of appearance-setting replies into
    // one updateAllDecorations().
    bool m_borderSweepPending = false;

    // Daemon readiness / virtual-screen fetch gate state. Fields + rationale in
    // effect_state.h (DaemonGateState).
    DaemonGateState m_daemonGate;

    // Screen/window id caches (mutable: populated from const accessors). Fields in
    // effect_state.h (IdCacheState). m_trackedScreenPerWindow below is a
    // non-mutable member and is deliberately kept out of this group.
    mutable IdCacheState m_idCaches;

    // Per-window tracked screen ID for cross-screen move detection.
    // Replaces the per-window `new QString` heap allocation that was leaked.
    QHash<KWin::EffectWindow*, QString> m_trackedScreenPerWindow;

    // Windows that already have their per-window connections. setupWindowConnections
    // issues raw connects with lambda slots, so a second call on the same window
    // doubles every per-window handler — and Qt::UniqueConnection is illegal with a
    // lambda slot, so the check has to live here.
    //
    // Two callers exist and their window sets are disjoint today (the ctor sweep
    // covers windows that predate the effect; slotWindowAdded covers windows that
    // appear after `windowAdded` is connected, and nothing between the two spins
    // the event loop). This makes that an enforced invariant rather than an
    // argument a future edit could silently invalidate.
    //
    // Erased in the windowDeleted backstop beside its raw-pointer-keyed siblings,
    // for the same address-reuse reason: a stale entry would refuse to wire a new
    // window that reused a dead one's address.
    QSet<KWin::EffectWindow*> m_wiredWindows;

    // Blocks pointer/touch input on strip straddlers' clipped-away overhangs
    // (see input_filter.h). Installed once daemon subscriptions are wired;
    // destruction uninstalls it from InputRedirection.
    std::unique_ptr<ScrollOverhangInputFilter> m_overhangInputFilter;

    // Windows withheld from compositing between windowAdded and the frame
    // their snap-restore / autotile reposition lands — see RestoreSuppression.
    // paintWindow draws nothing for a window present here. Entries are
    // erased on settle, on a negative resolve, on the deadline, and on
    // window close/delete.
    QHash<KWin::EffectWindow*, RestoreSuppression> m_restoreSuppress;

    // The one in-flight deferred geometry replay per window (applyWindowGeometry
    // postponing a tile apply until the user's interactive move ends).
    //
    // WHY A STORE AND NOT A BARE connect(): two applies for the same window
    // inside one batch generation on one screen both survive the supersession
    // guard, so without this each one connected its own replay and both fired at
    // drag end, paying a full moveResize plus an animator retarget plus a rule
    // resolve twice. Last-write-wins made the final rect right, which is exactly
    // why it stayed invisible.
    //
    // A blanket disconnect on the signal is NOT a substitute: window_connections
    // holds a permanent connection from the same signal to the same receiver, and
    // a blanket disconnect destroys it. Qt::UniqueConnection is illegal with a
    // lambda slot. So the connection is held here and disconnected by handle.
    //
    // Erased when the replay fires, and in the windowDeleted backstop beside its
    // raw-pointer-keyed siblings.
    QHash<KWin::EffectWindow*, QMetaObject::Connection> m_deferredGeometryReplay;

    // Stamp of the last going-to-minimized shader install per window, used
    // by slotWindowMinimizedChanged to detect KWin's spurious
    // minimize→unminimize pairs (plasmashell notification stacking emits
    // them on tiled windows ~1-2 ms apart; the float side debounces the
    // same quirk with the shared kSpuriousMinimizePairMs — see
    // tilinghandler/minimizefloat.cpp). An unminimize landing inside the
    // window silently drops the reverse leg instead of replaying a full
    // un-minimize animation. `generation` pins the stamp to the exact
    // transition the minimize event installed (or kept running), so the
    // cancel can never hit an unrelated reverse leg (a superseding leg,
    // or any future reverse event). Entries are erased on consume and on
    // windowDeleted (raw-pointer-keyed, bounded like its siblings above).
    // MinimizeShaderStamp moved to effect_state.h.
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

    /// Re-resolve every entry in m_trackedScreenPerWindow (and the autotile
    /// handler's notified-screen map) against the current virtual-screen
    /// definitions. Called from every path that changes m_virtualScreenDefs,
    /// including the removal paths: a subdivided monitor losing its definitions
    /// strands its windows on "<output>/vs:N" ids that no longer resolve, which
    /// then reads as a phantom crossing on the next geometry change.
    void reresolveTrackedScreens();

    /// Process window state that depends on virtual screen definitions being loaded.
    /// Called from fetchAllVirtualScreenConfigs completion callback after all
    /// async D-Bus replies have arrived.
    void processDaemonReadyWindowState();

private Q_SLOTS:
    /// Handle daemon signal when virtual screen definitions change
    void onVirtualScreensChanged(const QString& physicalScreenId);

    /// Handle the daemon naming the wl_surface that draws @p screenId's
    /// scrolling tab indicators. A @p surfaceId of 0 retracts the registration.
    void onScrollTabSurfaceChanged(const QString& screenId, uint surfaceId);

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
    /// sole source of per-window effect overrides. The effect's ONE
    /// rule-store sync point: the same parsed payload also refreshes the
    /// three exclusion slices (placement = Exclude ∪ ExcludePlacement for the
    /// drag gate, decoration = Exclude ∪ ExcludeDecorations for
    /// shouldDecorateWindow, animation = the ExcludeAnimations slice for
    /// shouldAnimateWindow) and the geometry-scoped-rules gate. Called once
    /// at bringup; the bringup also subscribes to the interface's
    /// `rulesChanged` signal (via a debounce timer — see
    /// m_animationRulesRefreshDebounce) so a settings-UI edit takes effect
    /// without restarting the effect.
    void loadRuleAnimationsFromDbus();

private:
    /// Re-slice the five effect-bound rule sets for an active-layout map that
    /// has just gone UNSEEDED (daemon-loss teardown / bring-up clear), by
    /// removing every rule whose match references `Field::ActiveLayout`.
    /// A plain member, not a slot: nothing connects to it — the sole caller is
    /// `TilingHandler::clearActiveLayoutsForTeardown` (a friend), so it earns
    /// no moc metadata.
    ///
    /// The rule sets deliberately survive daemon loss, but the admission
    /// filter that filled them ran while the map was seeded, so they hold
    /// rules that resolve against a map which is now empty — and an empty
    /// ActiveLayout stamp is not inert (a `None{ActiveLayout Equals X}` leaf
    /// matches EVERY window). Dropping them restores the both-polarities-inert
    /// shape `effectNeverStampedFields` gives a cold start.
    ///
    /// Sets `m_activeLayoutRulesWithheld` when anything was removed, so the
    /// next seeding edge in `TilingHandler::setActiveLayouts` re-drives
    /// `loadRuleAnimationsFromDbus` and restores them from the live store.
    /// That makes the marker correct BY CONSTRUCTION on this path: the same
    /// call that withholds the rules records that it did.
    ///
    /// Runs no border sweep and no rule-cache invalidation of its own — the
    /// sanctioned callers of `TilingHandler::clearActiveLayoutsForTeardown`
    /// (its only caller) already run `invalidateAllRuleCaches`, whose
    /// window-layer sweep a removed `SetWindowLayer` rule needs, and each then
    /// rebuilds the affected decorations its own way: `onDaemonReady` with
    /// `scheduleBorderSweep`, the `serviceUnregistered` teardown with
    /// `clearAllDecorations` (which needs no sweep, having removed them).
    ///
    /// It DOES take the `SetOpacity` repaint bookend, because nothing else
    /// covers it on the bring-up caller: opacity resolves in the paint path,
    /// and a straight old→new daemon handover emits no `serviceUnregistered`
    /// edge, so the decorations (and the tint layer whose teardown covers the
    /// daemon-loss caller) are still live there.
    void sliceActiveLayoutRulesForUnseededMap();

    /// One getAllRules round trip: the body of loadRuleAnimationsFromDbus,
    /// which is the budget-granting wrapper. The failure arm re-dispatches
    /// this directly (bounded, m_ruleFetchRetriesLeft) so retries do not
    /// re-grant themselves a fresh budget.
    void fetchAllRulesOnce();

private Q_SLOTS:
    /// D-Bus signal handler for `Rules.rulesChanged`. Re-arms the
    /// debounce timer rather than refetching the full ruleset on every
    /// signal — the daemon emits one signal per per-rule mutation, so a
    /// 50-rule batch edit would otherwise drive 50 full-ruleset fetches
    /// and parses. A 50ms single-shot debounce coalesces the burst into a
    /// single fetch at the trailing edge.
    void slotRulesChanged();

private:
};

} // namespace PlasmaZones
