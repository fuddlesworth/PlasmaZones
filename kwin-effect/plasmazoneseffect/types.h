// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>

#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector4D>

#include <array>
#include <memory>

namespace KWin {
class EffectWindow;
class Item;
}

namespace PlasmaZones {

/// Per-window border + rounded corners, rendered by sampling the redirected
/// window through an offscreen MapTexture fragment shader (the KDE-Rounded-
/// Corners / shapecorners technique) rather than a scene-graph
/// `OutlinedBorderItem`.
///
/// A scene-graph child item composites against KWin's own decoration frame +
/// drop shadow, so on server-side-decorated windows an outline looks "inset".
/// Sampling the redirected texture runs over the decoration with KWin's own MVP,
/// so the outline is flush and the SAME path rounds the corners.
///
/// The shader evaluates one analytic rounded-rect signed-distance field over the
/// window FRAME and does TWO things from it, IDENTICALLY for decorated and
/// borderless windows: it clips the frame corners (reducing content alpha so the
/// corners round) and draws the outline as an inner band of `width` just inside
/// the rounded edge. It runs over the COMPOSITED redirected texture, so it
/// rounds the outer frame corners (titlebar included) without ever clipping an
/// individual client subsurface. The drop shadow in the expanded-geometry margin
/// is left untouched (the clip cuts only the corner overhang INSIDE the frame
/// box); KWin's own square shadow corner is currently left as-is (in-shader
/// shadow synthesis is a follow-up).
///
/// The resolved appearance (width / radius / colour) is stored in LOGICAL
/// pixels; pushBorderUniforms multiplies by `viewport.scale()` per frame to
/// reach the device-pixel uniforms the shader works in.
struct WindowBorder
{
    /// Resolved border appearance in LOGICAL pixels (the paint path multiplies
    /// width/radius by `viewport.scale()` to reach device px for the shader).
    int width = 0;
    int radius = 0;
    QColor color;

    /// True when THIS border owns the window's OffscreenEffect redirect +
    /// border shader slot. False while an animation transition has taken over
    /// the slot (the animation path's begin/end coordinates the handover) —
    /// the per-frame uniform push and the transition-end re-apply both consult
    /// this so the border path never fights the transition lifecycle.
    bool shaderApplied = false;
};

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
    int iOldWindowLoc = -1;
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
    /// Two-mode progress source.
    /// • `durationMs > 0`: time-based — `startTimeMs` is the monotonic
    ///   `shaderClockNowMs()` (steady_clock) at begin time and paintWindow
    ///   computes `progress = clamp01((now - startTimeMs) / durationMs)`.
    ///   Used by lifecycle events (window.open/close/focus/etc.) that have
    ///   no `m_windowAnimator` animation to ride.
    /// • `durationMs == 0`: animator-driven — paintWindow reads progress
    ///   from `m_windowAnimator->animationFor(w)->state().value`. Used by
    ///   zone.* events that flow through `applySnapGeometry` and inherit
    ///   the geometry animation's timeline.
    qint64 startTimeMs = 0;
    int durationMs = 0;
    /// Monotonic per-window generation. Each `beginShaderTransition` bumps
    /// the counter for that window; the timer-driven `endShaderTransition`
    /// scheduled by `tryBeginShaderForEvent` captures the generation at
    /// schedule time and bails on fire if the live transition has been
    /// superseded — without this, a stale timer would tear down a fresh
    /// transition that replaced it before it had a chance to play out.
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
    /// Set true when a morph transition begins (wired in applySnapGeometry);
    /// the first morph paint captures the still-old window content into
    /// `oldSnapshot` and clears this. The window content is captured before
    /// the moveResize configure round-trips, so it holds the OLD frame.
    bool needsSnapshot = false;
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
    /// The resolved snap / tile rect, stamped by `applySnapGeometry` once
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

/// A single virtual screen subdivision within a physical monitor.
///
/// Virtual screens divide a physical monitor into independent sub-screens,
/// each with its own zones, autotile state, etc. The daemon manages
/// definitions; the effect fetches them via D-Bus and resolves positions.
///
/// Named `EffectVirtualScreenDef` to avoid collision with the daemon's
/// `PhosphorScreens::VirtualScreenDef` (which has many more fields).
struct EffectVirtualScreenDef
{
    QString id; ///< e.g., "Dell:U2722D:115107/vs:0"
    QRect geometry; ///< Absolute geometry in global compositor coords
};

} // namespace PlasmaZones
