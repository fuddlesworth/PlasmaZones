// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file transition_types.h
/// Per-window ANIMATION transition state (the shader legs and their timing).
///
/// Split out of types.h — see surface_types.h for the rationale. types.h remains
/// as an umbrella that includes both, so existing consumers are unaffected.

#include "mesh_sim.h"

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/Curve.h>

#include <effect/effectwindow.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector4D>

#include <array>
#include <memory>

namespace PlasmaZones {

/// User-texture cache entry. Owns the uploaded `GLTexture` and tracks the wrap
/// mode last applied to its GL state — so two shader transitions sharing the
/// same on-disk path can run with different wrap modes without invalidating
/// each other's cache entry, and paintWindow can skip redundant `setWrapMode`
/// calls (each is two `glTexParameteri`s; on a high-Hz display with multiple
/// slots in flight the redundant traffic is non-trivial).
struct CachedTexture
{
    std::unique_ptr<KWin::GLTexture> texture;
    GLenum lastAppliedWrap = GL_CLAMP_TO_EDGE;
    /// Monotonic access counter (NOT a wall-clock frame number). Updated on
    /// every successful lookup or fresh insert. The LRU eviction sweep
    /// evicts the entry with the smallest value (least recently used) that
    /// is NOT currently referenced by any in-flight transition.
    quint64 lastAccessTick = 0;
};

/// Compiled shader + cached uniform locations for the animation contract.
struct CachedShader
{
    std::unique_ptr<KWin::GLShader> shader;
    /// Animation-shader contract uniform locations (see
    /// `PhosphorAnimationShaders::AnimationShaderContract`). Per-effect
    /// declared parameters land in the `customParams[N]` slots — the
    /// translation from friendly parameter ids (e.g. `direction`) to slot
    /// keys is performed by `AnimationShaderRegistry::translateAnimationParams`
    /// when the transition starts; paintWindow just pushes vec4s.
    int iTimeLoc = -1;
    int iResolutionLoc = -1;
    int iTimeDeltaLoc = -1;
    int iFrameLoc = -1;
    int iDateLoc = -1;
    int iMouseLoc = -1;
    int iIsReversedLoc = -1;
    int iSurfaceScreenPosLoc = -1;
    int iAnchorSizeLoc = -1;
    int iAnchorPosInFboLoc = -1;
    int iAnchorRectInTextureLoc = -1;
    /// `iWindowOpacity` location — -1 when the shader never reads
    /// `surfaceColor()` (so the GLSL linker dropped the uniform). paintWindow
    /// pushes the rule-resolved opacity here every frame so a SetOpacity rule
    /// dims the surface throughout the transition; the MapTexture-only shader
    /// can't see `data.opacity()`.
    int iWindowOpacityLoc = -1;
    // Slot counts sourced from AnimationShaderContract so a future change to
    // the contract (e.g. growing the customParams budget) can't silently
    // desync this cache from the translation + upload sites in
    // shader_transitions.cpp. The default-initialiser sets every entry to -1;
    // std::array's value-initialisation doesn't, so wrap the construction.
    std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParamsLoc = []() {
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColorsLoc = []() {
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> a;
        a.fill(-1);
        return a;
    }();
    /// Sampler uniform locations for `uTexture1..3`. -1 means the GLSL linker
    /// dropped the sampler (the shader source never read it), in which case
    /// paintWindow skips both the bind and the setUniform — saves a
    /// glActiveTexture call per unused slot.
    std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> userTextureLoc = []() {
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> a;
        a.fill(-1);
        return a;
    }();
    /// Element locations for `iTextureResolution[0..N-1]`. Symmetric with
    /// `customParamsLoc` / `customColorsLoc` — the element-name lookup
    /// happens at compile time so paintWindow does not re-resolve
    /// `"iTextureResolution[0]"` string lookups per frame.
    std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> iTextureResolutionLoc =
        []() {
            std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> a;
            a.fill(-1);
            return a;
        }();
    /// Geometry-morph uniform locations. The window moves instantly via
    /// `moveResize`; the morph shader animates the visual transition by
    /// interpolating the window quad from its old frame (`iFromRect`) to its
    /// new frame (`iToRect`) by `iTime`, cross-fading a captured snapshot of
    /// the old content (`uOldWindow`) into the live new content (`uTexture0`).
    /// -1 when the shader doesn't read the uniform (linker dropped it) — a
    /// non-morph shader leaves all three at -1 and pays nothing.
    int iFromRectLoc = -1;
    int iToRectLoc = -1;
    /// `iIconRect` — the window's task-manager icon rect (see
    /// AnimationShaderContract::kIIconRect). -1 when the shader doesn't
    /// read it; only minimize-to-icon packs (genie, phosphor-siphon)
    /// declare it.
    int iIconRectLoc = -1;
    int iOldWindowLoc = -1;
    /// `iHasOldWindow` — 1 when a captured old-content snapshot is bound for
    /// this transition, 0 otherwise (the shader then falls back to
    /// surfaceColor() for its "old" side instead of sampling the raw window
    /// through the unit-0 alias).
    int iHasOldWindowLoc = -1;
    /// Audio-spectrum uniforms (opt-in module
    /// `data/animations/shared/audio.glsl` + metadata `"audio": true`).
    /// -1 when the pack doesn't include the module — paintWindow then skips
    /// the spectrum push and bind entirely.
    int iAudioSpectrumSizeLoc = -1;
    int uAudioSpectrumLoc = -1;
    /// Interactive-move motion uniforms (held move/resize transitions).
    int iMoveVelocityLoc = -1;
    int iMoveOffsetLoc = -1;
    int iMoveVelocity2Loc = -1;
    int iMoveTrailLoc = -1;
    int iMoveMeshLoc = -1;
    /// Surface-layer-stack uniforms (compositor path). `uSurfaceLayer` is the
    /// pre-composited layered surface (border / rounded corners, ...) sampled in
    /// place of the live `uTexture0` while a layered window animates;
    /// `iHasSurfaceLayer` gates the redirect. Both resolve in every animation
    /// shader (declared in the shared header), so they are non-negative whenever
    /// the shader reads the surface through `surfaceColor()`. See
    /// AnimationShaderContract::kUSurfaceLayer / kIHasSurfaceLayer.
    int uSurfaceLayerLoc = -1;
    int iHasSurfaceLayerLoc = -1;
    /// The layer's own sub-rect remap (padded canvas support) — see
    /// AnimationShaderContract::kILayerRectInTexture.
    int iLayerRectInTextureLoc = -1;
    /// uTexture0 sampler location. Used to RETARGET the window sampler at the
    /// decorated composite during transitions whose layer maps 1:1 onto the
    /// window texture (unpadded chains): 25 of the bundled animation packs
    /// sample uTexture0 raw somewhere, bypassing surfaceColor()'s uSurfaceLayer
    /// path — without the retarget those shaders drop the decoration (border,
    /// frost, glass) for the whole transition.
    int uTexture0Loc = -1;
};

/// Per-window in-flight shader transition.
struct ShaderTransition
{
    const CachedShader* cached = nullptr;
    /// `customParams[N]` slot values resolved at transition begin time.
    /// `AnimationShaderRegistry::translateAnimationParams` translates the
    /// friendly parameter map (e.g. `{"direction": 1, "parallax": 0.2}`) to
    /// slot keys (`{"customParams1_x": 1, "customParams1_y": 0.2}`); we pack
    /// those into vec4s here so paintWindow only does 8 setUniform calls
    /// per frame, not per-frame string lookups. Slots with no declared
    /// parameters stay at (0, 0, 0, 0).
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParamsValues = {};
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColorsValues = {};
    /// Progress source + timing curve. Two modes:
    /// • `durationMs > 0` (time-based): linear `(now - startTimeMs)/durationMs`
    ///   eased through `progressCurve`. Lifecycle events; the held-move leg
    ///   varies it — see `holdUntilRelease` / `releaseStartMs` (pin-at-1,
    ///   un-eased release) and `regrabStartMs`.
    /// • `durationMs == 0` (animator-driven, window.movement.* via applyWindowGeometry):
    ///   reads the already curve-shaped `animationFor(w)->state().value`;
    ///   `progressCurve` null (no double-ease).
    /// `progressCurve`: motion cascade (global → "All" → node → rule), null →
    /// linear; a stateful spring steps `progressCurveState` toward 1. The eased
    /// result is clamped to [0,1] ONLY for a non-overshooting curve — a spring or
    /// back-ease delivers its overshoot, so iTime can leave [0,1] (see
    /// ShaderInternal::easeProgress). The desktop switch is NOT a ShaderTransition
    /// (it owns an OutputTransition instead).
    qint64 startTimeMs = 0;
    int durationMs = 0;
    std::shared_ptr<const PhosphorAnimation::Curve> progressCurve;
    PhosphorAnimation::CurveState progressCurveState;
    /// Monotonic per-window generation. Each `beginShaderTransition` bumps
    /// the counter for that window; the timer-driven `endShaderTransition`
    /// scheduled by `tryBeginShaderForEvent` captures the generation at
    /// schedule time and bails on fire if the live transition has been
    /// superseded — without this, a stale timer would tear down a fresh
    /// transition that replaced it before it had a chance to play out.
    /// Two non-install sites bump it too, both for the held drag: the
    /// drag-start (re-)hold fences a prior drag's release timers on a
    /// re-grab, and the mesh-release handler fences the start-scheduled
    /// duration timer when it hands the lifetime to the settle gate. The
    /// paint pipeline's deferred expiry teardown is a second
    /// generation-guarded consumer alongside the event timers.
    quint64 generation = 0;
    /// When true, paintWindow flips progress to 1 - progress before the
    /// uniform write, so the shader runs from `iTime = 1.0` down to
    /// `iTime = 0.0` over the transition's lifetime. Used by lifecycle
    /// events whose semantic is "going away" — window.close,
    /// going-to-minimized, going-to-unmaximized — so a single shader effect
    /// doubles as both directions: an `appear` shader (e.g. fade-in from
    /// glitch) reads progress=0 as "fully obscured" and progress=1 as
    /// "fully revealed", which is exactly the semantic users expect for
    /// the corresponding `disappear` event.
    bool reverse = false;
    /// True when the active animation declares `fboExtent: "surface"` in
    /// its metadata. Surface-extent shaders (bounce, fly-in, broken-glass,
    /// morph) paint past the window bounds, so `apply()` expands the
    /// drawn quad to the window's output and paintWindow feeds
    /// output-relative `iResolution` / `iAnchorPosInFbo` / `iAnchorSize`.
    /// Anchor-extent transitions (the default) keep the window-sized
    /// quad and uniforms.
    bool surfaceExtent = false;
    /// Per-axis quad subdivision count for vertex-stage geometry
    /// deformation, copied from the effect's `geometryGrid` metadata.
    /// 0 (default) means `apply()` emits the single output-spanning
    /// surface quad; > 0 means it emits an NxN grid of `WindowQuad` cells
    /// over the window's destination rect so a custom vertex shader has
    /// interior vertices to displace — the `flow` window-move effect lags
    /// trailing grid rows behind the leading edge. Only honoured when
    /// `surfaceExtent` is also true.
    int gridSubdivisions = 0;
    /// Per-leg frame counter. Bumped each paintWindow tick where this
    /// transition feeds the shader; reset to 0 on every fresh
    /// beginShaderTransition install (or supersession). Mirrors the daemon's
    /// `SurfaceAnimator` `iFrame` semantic: starts at 0 on each fresh
    /// attach so shaders that read it (e.g. for staggered per-frame
    /// randomness) see the same trajectory on both runtimes.
    ///
    /// Type pinned to `int` to match GLSL `uniform int iFrame;` in
    /// `data/animations/shared/animation_uniforms.glsl`. The shared
    /// header's UBO offset (76) is also `int`-sized so the kwin
    /// `setUniform(iFrameLoc, iFrameValue)` push and the daemon std140
    /// layout agree. A static_assert at the push site pins this contract.
    int frameCount = 0;
    /// Per-leg user-texture pointers. Resolved at `beginShaderTransition`
    /// time from the translated params' `uTexture<N>` keys (pack defaults
    /// merged with any per-leg runtime override). Raw non-owning pointers
    /// into `m_textureCache`, which owns the underlying `GLTexture` and
    /// outlives every transition that references it. nullptr means
    /// "no texture for this slot" — paintWindow skips the bind and the
    /// sampler reads transparent black. Pointing at the cache entry rather
    /// than the bare `GLTexture` lets paintWindow skip redundant
    /// `setWrapMode` calls by comparing against
    /// `CachedTexture::lastAppliedWrap`.
    std::array<CachedTexture*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> userTextures =
        {};
    /// Wrap-mode GL enum applied at bind time (GL_CLAMP_TO_EDGE / GL_REPEAT
    /// / GL_MIRRORED_REPEAT). Stored per-leg rather than on the cached
    /// `GLTexture` so two transitions sharing the same on-disk path can
    /// run with different wrap modes without invalidating each other's
    /// cache entry.
    std::array<GLenum, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> userTextureWrap = []() {
        std::array<GLenum, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots> a;
        a.fill(GL_CLAMP_TO_EDGE);
        return a;
    }();
    /// Wall-clock timestamp of the previous paintWindow tick that fed this
    /// transition. -1 means "no prior paint yet" — first paint produces
    /// `iTimeDelta = 0` rather than a spurious huge delta from the install
    /// timestamp. `shaderClockNowMs()` based, so monotonic and immune to
    /// NTP jumps mid-transition.
    qint64 lastPaintTimeMs = -1;
    /// True when this transition holds @c KWin::WindowClosedGrabRole on the
    /// window. The grab keeps a closing window alive past KWin's normal
    /// unmap-and-delete sequence so paintWindow has frames to run the
    /// close shader on. OffscreenEffect's @c redirect alone is
    /// insufficient — the OffscreenEffect docstring explicitly states
    /// "The window will be automatically unredirected if it's deleted",
    /// meaning a closing window is auto-released without the grab and the
    /// close shader never gets a paint cycle.
    ///
    /// Set true only by the @c slotWindowClosed → @c tryBeginShaderForEvent
    /// path (via the @c holdCloseGrab parameter threaded through
    /// @c beginShaderTransition). Released in @c endShaderTransition
    /// unconditionally — clearing the role on a non-closing window is a
    /// no-op, and clearing it on a deleted window lets KWin proceed with
    /// final destruction.
    bool closeGrabHeld = false;
    /// True when this transition holds @c KWin::WindowAddedGrabRole on the
    /// window. The grab role tells every OTHER effect in the chain to
    /// ignore this window for built-in window-open animations (KWin's
    /// stock fade / scale / slide / glide on windowAdded). Without it,
    /// those built-in effects render the window concurrently with our
    /// fly-in / bounce shader — KWin's fade paints the window at its
    /// natural position with its own progress while our shader paints
    /// the same window at the UV-shifted position. Both end up in the
    /// same framebuffer and the user sees multiple visible copies of
    /// the window, each animating on a different timeline.
    ///
    /// Set true only by the @c slotWindowAdded → @c tryBeginShaderForEvent
    /// path (via the @c holdAddedGrab parameter threaded through
    /// @c beginShaderTransition). Released in @c endShaderTransition
    /// unconditionally — clearing the role on a window with no
    /// pending built-in open animation is a no-op.
    bool addedGrabHeld = false;
    /// Forces KWin to keep painting a MINIMIZED window for this
    /// transition's lifetime — the same mechanism KWin's own Magic Lamp /
    /// Squash effects use. Minimizing does not destroy the surface or its
    /// geometry; it only sets PAINT_DISABLED_BY_MINIMIZE on the window
    /// item, and this ref lifts exactly that bit, so the going-to-minimized
    /// leg of window.appearance.minimize has live frames to paint.
    /// Installed by @c beginShaderTransition whenever the target window is
    /// already minimized at install time; released automatically when the
    /// transition entry is destroyed (RAII — expiry, supersession, and the
    /// windowDeleted handler all erase the entry; the ref's copy ctor
    /// re-refs and its dtor unrefs, so the move-into-map stays balanced).
    KWin::EffectWindowVisibleRef visibleRef;
    /// The window's task-manager icon rect in logical screen pixels,
    /// captured from `EffectWindow::iconGeometry()` at install time and
    /// pushed per frame as `iIconRect` for packs that declare it (genie,
    /// phosphor-siphon). Stays a null QRectF — pushed as (0, 0, 0, 0) —
    /// when the window sits in no task manager; packs treat that as "no
    /// icon target". Captured once rather than read per frame so a
    /// taskbar relayout mid-animation can't teleport the deformation
    /// target.
    QRectF iconRect;
    /// Cached texcoord handedness derived from the first quad of the source
    /// list at the first `apply()` call. The handedness depends on KWin's
    /// WindowQuad texcoord convention which doesn't change between frames,
    /// so recomputing the (top/bottom/left/right) vertex search per frame
    /// per surface-extent transition is wasted hot-path work — bounce,
    /// fly-in, broken-glass, morph all hit this path on every frame for
    /// the transition's lifetime.
    ///
    /// `handednessCached == false` means "not yet computed"; the first
    /// `apply()` populates the four `*At*` slots and flips the bool.
    /// Cleared on every fresh `beginShaderTransition` install for the same
    /// window (transitions are torn down + reinstalled when the underlying
    /// shader changes), so a follow-up install starts with a fresh probe.
    bool handednessCached = false;

    double uAtLeft = 0.0;
    double uAtRight = 1.0;
    double vAtTop = 0.0;
    double vAtBottom = 1.0;
    /// ── Geometry-morph state (cross-fade old→new on snap/move/resize) ──
    /// `fromGeometry` is the window's frame rect BEFORE the instant
    /// `moveResize`; `toGeometry` is the destination rect (the live
    /// `frameGeometry` once the configure lands). The morph shader
    /// interpolates between them by progress, so daemon-driven geometry
    /// changes animate via shader instead of the C++ `WindowAnimator`
    /// translate+scale. Both invalid (default QRectF) for non-morph
    /// transitions (window.open/close/etc.), which signals paintWindow to
    /// skip the morph uniforms.
    QRectF fromGeometry;
    QRectF toGeometry;
    /// Set true when a morph transition begins (wired in applyWindowGeometry);
    /// the first morph paint captures the still-old window content into
    /// `oldSnapshot` and clears this. The window content is captured before
    /// the moveResize configure round-trips, so it holds the OLD frame.
    bool needsSnapshot = false;
    /// ── Held interactive-move state (window.movement.move) ──
    /// True while the user is still dragging: the transition stays active
    /// past durationMs (progress clamps at 1) and the duration-timer
    /// teardown is skipped — windowFinishUserMovedResized schedules the
    /// real teardown after a settle tail so velocity-driven shaders can
    /// relax. Set at the move-start install site.
    bool holdUntilRelease = false;
    /// This leg was installed for `window.movement.move` — i.e. it IS the held-move
    /// transition, not merely some transition that happens to be live during a drag.
    ///
    /// Load-bearing because `window.movement.move` is an OPT-IN class with no
    /// default and no inherited shader, so in the stock configuration
    /// `tryBeginShaderForEvent(WindowMove, ...)` installs NOTHING. The drag-start
    /// handler cannot therefore use `findTransition(window)` as a proxy for "the
    /// move leg" — it would hand back whatever is live, most reachably the
    /// `window.focus` leg the click that started the drag installed milliseconds
    /// earlier. Setting `holdUntilRelease` on that leg pins it at progress 1 for the
    /// whole drag, the generation bump kills its teardown timer, and on release it
    /// plays BACKWARD. A maximize pack declaring iFromRect is worse: it owns the
    /// drawn rect, so the window paints frozen at its pre-drag geometry for the
    /// entire drag.
    ///
    /// Gate the drag-start and release handlers on THIS, not on `holdUntilRelease`
    /// (which is precisely what gets wrongly set) and not on liveness.
    bool heldMove = false;
    /// Held-move release leg (velocity/trail move packs, no lattice):
    /// `shaderClockNowMs()` stamped by windowFinishUserMovedResized. The
    /// hold flag stays SET through the release tail (it also drives the
    /// velocity-spring integration and the expanded-repaint injection,
    /// which the ramp-down still needs), and paintWindow ramps the pinned
    /// progress back toward 0 over `durationMs` from this stamp, so the
    /// shader plays iTime 1→0 after the button lets go and a
    /// dissolve-while-held pack can rematerialise. -1 = not released (or
    /// a mesh pack, whose ring-out rides the settle gate instead). The
    /// drag-start handler clears it on a re-grab and hands the accrued
    /// ramp-down to `regrabDownOffset` (see below).
    qint64 releaseStartMs = -1;
    /// Re-grab during a release leg: the down-ramp accrued at the moment of
    /// re-grab, decaying back to 0 over `durationMs` from `regrabStartMs`.
    /// -1 = no re-grab in flight.
    ///
    /// The resumed progress CANNOT be reconstructed by rewinding `startTimeMs`.
    /// That worked only while iTime was linear: the painted value is
    /// `curve(linear)`, so rebasing the LINEAR clock to the ramped-down value v
    /// paints `curve(v)`, not v (a 0.375 jump at v=0.5 under the default
    /// OutCubic). For a stateful spring the rewind is not merely wrong but inert:
    /// `easeProgress` ignores `linear` and reads the integrator, still settled at
    /// ~1 from the completed grab-in, so progress snaps straight back up.
    ///
    /// Subtracting a decaying offset from the PAINTED progress is instead
    /// continuous by construction for both curve kinds, and needs no curve
    /// inverse (curves have no general one, and an overshooting curve is not
    /// injective). It is symmetric with the release leg, itself a linear offset
    /// in progress space.
    qint64 regrabStartMs = -1;
    qreal regrabDownOffset = 0.0;
    /// Frame origin at grab — iMoveOffset = current origin - this.
    QPointF grabOrigin;
    /// Underdamped spring filter over the instantaneous frame velocity;
    /// springLag is what iMoveVelocity publishes (logical px/s). The
    /// underdamping makes the published velocity decay through zero with a
    /// small overshoot after release, giving stateless shaders a natural
    /// settle for free. Integrated per paint in the transition branch.
    QPointF springLag;
    QPointF springVel;
    /// Second, looser spring over the same motion (iMoveVelocity2): lower
    /// stiffness and lighter damping so it trails springLag and rings
    /// longer — the phase-spread source for jelly deformations.
    QPointF springLag2;
    QPointF springVel2;
    QPointF lastMovePos;
    qint64 lastMoveSampleMs = -1;
    /// Motion-history ring for iMoveTrail: absolute frame origins recorded
    /// every kTrailStepMs while held; uploaded relative to the current
    /// origin. Index 0 is the newest sample. Dumb data — every deformation
    /// decision lives in the pack's shader.
    static constexpr int kTrailSlots = 16;
    static constexpr qreal kTrailStepMs = 15.0;
    QPointF moveTrail[kTrailSlots];
    qint64 trailLastMs = -1;
    /// Generic soft-body control lattice (iMoveMesh). Simulated by the host
    /// each held frame; its displacements feed any mesh-consuming pack.
    MeshSim meshSim;
    MeshSimParams meshParams;
    qint64 meshLastMs = -1;
    /// Snapshot of the window's OLD content, bound as `uOldWindow` so the
    /// shader can cross-fade the old content out while the live new content
    /// fades in. Captured on the first morph paint AFTER the instant
    /// `moveResize` — so it is sized to the window's current (post-moveResize)
    /// `expandedGeometry`, but the buffer it holds is still the OLD content
    /// because the client has not yet re-rendered for the configure. (The
    /// matching new-geometry sub-rect `iAnchorRectInTexture` therefore maps
    /// card-space uv into it correctly, same as for the live `uTexture0`.)
    /// Owned per-transition (unique per capture, not path-keyed like
    /// `userTextures`) and freed when the transition ends. Null when capture
    /// was not requested or failed — paintWindow then binds a transparent
    /// fallback (or the shader falls back to a non-cross-fade morph).
    std::unique_ptr<KWin::GLTexture> oldSnapshot;
};

/// First-frame suppression bookkeeping for a window that is about to be
/// repositioned on open (snap-restore or autotile).
///
/// KWin places a new window at its centred placement geometry and
/// composites it there BEFORE the effect's `windowAdded` handler can move
/// it into a zone / tile — `windowAdded` is the earliest hook an effect
/// gets, and it fires post-placement. The effect's `moveResize` is an
/// asynchronous Wayland configure, so without suppression the window
/// visibly flashes at screen centre for the 1-N frames the configure
/// takes to round-trip (longer for the autotile / async-resolve D-Bus
/// paths). While a `RestoreSuppression` entry exists for a window,
/// `paintWindow` draws nothing for it — the window is invisible until it
/// has settled into its destination.
struct RestoreSuppression
{
    /// frameGeometry at `windowAdded`. The window is released once its
    /// live geometry leaves this point — i.e. the repositioning configure
    /// has landed.
    QRectF spawnGeometry;
    /// The resolved snap / tile rect, stamped by `applyWindowGeometry` once
    /// the window is actually being repositioned. Invalid until then:
    /// while invalid a geometry change is NOT treated as "settled" — it is
    /// just the client's own initial size negotiation — so suppression
    /// holds until the real reposition target is known.
    QRectF targetGeometry;
    /// `shaderClockNowMs()` hard deadline. If the window is never
    /// repositioned (it floats, all zones are full, the daemon is
    /// unreachable) suppression is released unconditionally here so a
    /// window can never be lost behind a stuck suppression.
    qint64 deadlineMs = 0;
};

} // namespace PlasmaZones
