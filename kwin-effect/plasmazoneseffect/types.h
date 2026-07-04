// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorSurface/SurfaceShaderContract.h>

#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QColor>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector4D>

#include <array>
#include <memory>
#include <vector>

namespace KWin {
class EffectWindow;
class Item;
}

namespace PlasmaZones {

/// One compiled buffer pass of a multipass SURFACE pack (the idle drawWindow
/// path). Each buffer.frag is a fullscreen-quad fragment that samples the
/// captured window surface (uTexture0) plus any prior buffer outputs
/// (iChannel0..N-1) and writes into its own FBO; the main effect.frag then
/// samples the final buffer output(s) as iChannel0..3. Compiled in
/// compiledPack() right after the main pack shader, cleared (fail-closed) if any
/// buffer pass fails to compile so the pack degrades to single-pass. The vector
/// of these is shared by every decorated window — the per-window FBO targets
/// live in SurfaceMultipassState.
struct CompiledSurfaceBufferPass
{
    std::unique_ptr<KWin::GLShader> shader;
    int uTexture0Loc = -1; ///< the captured surface (bound to GL_TEXTURE0)
    int uTimeLoc = -1; ///< iTime — continuous seconds (-1 for a static buffer pass)
    int uSurfaceSizeLoc = -1; ///< uSurfaceSize — the composite canvas extent, device px
    int uScaleLoc = -1; ///< uSurfaceScale — logical-to-device scale (blur radii etc.)
    /// Backdrop sampling (needsBackdrop packs): sampler + valid-rect + gate
    /// locations; -1 for the common pass that never references the scene
    /// behind the window.
    int uBackdropLoc = -1;
    int uBackdropRectLoc = -1;
    int uHasBackdropLoc = -1;
    /// iChannel0..3 sampler locations — prior buffer outputs feeding this pass.
    std::array<int, 4> iChannelLoc{{-1, -1, -1, -1}};
    /// iChannelResolution[0..3] element locations (the .xy pixel size of each).
    std::array<int, 4> iChannelResolutionLoc{{-1, -1, -1, -1}};
    /// Pack-declared parameter slot locations (reuse the main pass's values).
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> a;
        a.fill(-1);
        return a;
    }();
};

/// Resolved customParams / customColors uniform VALUES for one surface pack —
/// the pack's declared parameter defaults merged with a DecorationProfile's
/// per-pack overrides, translated to contract slots. Stored per (window, pack)
/// on WindowBorder::packParamValues so windows on different surface paths
/// (window.tiled vs window.snapped vs window.floating) can carry different
/// override values for the SAME pack; the pack's compiled program is
/// value-independent, so only these arrays vary per window.
struct SurfaceParamValues
{
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> params{};
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> colors{};
};

/// Compiled state for ONE surface shader pack (e.g. "border", "glow"), keyed by
/// pack id in PlasmaZonesEffect::m_compiledPacks. Holds everything the old single
/// borderShader() global produced for the one selected pack — the main MapTexture
/// GLShader, its contract uniform locations, the pack-declared customParams /
/// customColors locations + resolved-default VALUES, the main-pass iChannel
/// locations, and the compiled multipass buffer passes — now parameterised per
/// pack id so per-window decoration chains can each render their own base pack.
///
/// Compiled on first use (compiledPack), cached for the effect's lifetime, and
/// fail-closed: a pack whose compile fails latches `compileFailed = true` and
/// `shader == nullptr`, so subsequent lookups return it without re-attempting the
/// compile every frame. The whole map is cleared on a SurfaceShaderRegistry
/// hot-reload (effectsChanged) so the next paint recompiles against fresh source.
///
/// The param VALUES baked here at first-compile time are the BASELINE fallback
/// only (the profile that triggered the compile). The render paths prefer the
/// per-window values updateWindowBorder resolves into
/// WindowBorder::packParamValues, so windows on different surface paths can
/// carry different overrides for the same pack; these baked arrays are used
/// only when a window has no per-window entry for the pack.
struct CompiledSurfacePack
{
    std::unique_ptr<KWin::GLShader> shader;
    bool compileFailed = false; ///< latch a failed compile so we don't retry every frame

    // Contract uniform locations (1:1 with PhosphorSurfaceShaders::SurfaceShaderContract).
    int uSurfaceSizeLoc = -1; ///< uSurfaceSize — uTexture0 extent, device px
    int uFrameTopLeftLoc = -1; ///< uSurfaceFrameTopLeft — frame top-left within the texture, device px
    int uFrameSizeLoc = -1; ///< uSurfaceFrameSize — frame size excluding shadows, device px
    int uScaleLoc = -1; ///< uSurfaceScale — logical-to-device pixel scale
    int uFocusedLoc = -1; ///< uSurfaceFocused — 1.0 focused / 0.0 unfocused
    int uOpacityLoc = -1; ///< uSurfaceOpacity — rule-resolved window opacity (handlesOpacity packs)
    int uTimeLoc = -1; ///< iTime — continuous seconds; -1 ⟺ static pack (drives the repaint gate)
    /// uTexture0 — the input-surface sampler (unit 0). On the single-pack path
    /// OffscreenData::paint binds the redirected surface to unit 0 automatically,
    /// so this is unused there; the multi-pack composite (renderSurfaceChainComposite)
    /// runs the main pass as a fullscreen FBO pass and binds the running composite
    /// to unit 0 itself, setting this explicitly.
    int uTexture0Loc = -1;
    /// Backdrop sampling (needsBackdrop packs, main pass): sampler +
    /// valid-rect + gate locations; -1 for packs that never reference the
    /// scene behind the window.
    int uBackdropLoc = -1;
    int uBackdropRectLoc = -1;
    int uHasBackdropLoc = -1;

    /// MAIN-pass iChannel0..3 sampler + iChannelResolution[0..3] element
    /// locations. -1 when the linker dropped the uniform (single-pass pack).
    std::array<int, 4> iChannelLoc{{-1, -1, -1, -1}};
    std::array<int, 4> iChannelResolutionLoc{{-1, -1, -1, -1}};

    /// Pack-declared parameter uniform locations + resolved-default values.
    /// float/int/bool params pack into customParams[N], colours into
    /// customColors[N]. The border pack declares borderWidth / cornerRadius /
    /// useSystemAccent (customParams[0]) and active/inactive colours
    /// (customColors[0..1]) — pushBorderUniforms overrides those slots with
    /// the per-window rule appearance. Slots a pack does not reference
    /// resolve to -1 and push nothing.
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> a;
        a.fill(-1);
        return a;
    }();
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsValues{};
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsValues{};

    /// Compiled multipass buffer passes (idle drawWindow path). Empty for a
    /// single-pass pack (the border). Cleared fail-closed if any pass fails to
    /// compile (the pack then renders single-pass). The per-window FBO targets
    /// live in m_surfaceMultipass.
    std::vector<CompiledSurfaceBufferPass> bufferPasses;
};

/// Per-window FBO render targets for the multipass SURFACE buffer chain (idle
/// drawWindow path). `surfaceTex` holds the raw captured window surface (full
/// resolution, the multipass equivalent of uTexture0); `bufferTex[i]` holds the
/// output of buffer pass `i` (sized at textureSize × bufferScale). All are
/// bottom-origin GL FBOs, the SAME layout as KWin's redirected uTexture0, so a
/// passthrough buffer (fragColor = surfaceTexel(vTexCoord)) reproduces the
/// surface upright. Reallocated only when the window's expanded size × scale
/// changes; erased on window close / border removal to free GPU memory.
struct SurfaceMultipassState
{
    // ── Single-pack-with-buffers path (renderSurfaceBufferPasses) ────────────
    // One pack with buffer passes, presented through OffscreenData. Unused on
    // the multi-pack path below (a window uses one path or the other).
    std::unique_ptr<KWin::GLTexture> surfaceTex;
    std::vector<std::unique_ptr<KWin::GLTexture>> bufferTex;
    QSize size; ///< full textureSize the single-pack targets were allocated for

    // ── Multi-pack chain compositing path (renderSurfaceChainComposite) ──────
    // The two composite textures ping-pong as the chain is folded pack-by-pack;
    // `finalSlot` names the slot holding the last fold (presented by drawWindow
    // through the passthrough shader). `chainBufferTex[k]` caches pack k's
    // buffer-pass outputs so an ANIMATED pack (future iTime support) does not
    // reallocate its scratch textures every frame. All are sized for
    // `compositeSize` / `chainKey` and rebuilt only when the window size or the
    // resolved chain changes.
    std::array<std::unique_ptr<KWin::GLTexture>, 2> compositeTex;
    std::vector<std::vector<std::unique_ptr<KWin::GLTexture>>> chainBufferTex;
    QStringList chainKey; ///< the chain `chainBufferTex` was allocated for
    QSize compositeSize; ///< full textureSize the composite targets were allocated for
    int finalSlot = 0; ///< which compositeTex slot holds the final fold
    /// The logical rect the composite canvas covers (expanded geometry
    /// inflated by the chain's outer padding, captured when the fold ran).
    /// The layer-rect remap and the padded quads read THIS instead of
    /// recomputing from live geometry, so a CLOSING (deleted) window — whose
    /// frozen composite is reused and whose live geometry may drift — keeps
    /// its decoration aligned to the texture that actually exists.
    QRectF canvasGeo;

    /// Backdrop capture for needsBackdrop chains: the scene behind the
    /// window blitted from the live render target over the SAME padded
    /// canvas as the composite (texel-aligned — a pack samples both with one
    /// uv). Reallocated on size change; freed with the rest of this state in
    /// removeWindowBorder, and NEVER sampled on the deleted/close path (the
    /// fold doesn't run there; the frozen composite carries the last-alive
    /// frost baked in).
    std::unique_ptr<KWin::GLTexture> backdropTex;
    QSize backdropSize;
    /// Valid sub-rect of backdropTex in TOP-DOWN normalized coords (xy=min,
    /// zw=size) — the part actually blitted (canvas ∩ output). Zero-size
    /// means "no capture this frame" and pushes uHasBackdrop = 0.
    QVector4D backdropRect;
};

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
/// borderless windows: it clips the window content to the INNER rounded rect
/// (inset by the border width) and lays the outline band OVER the background just
/// outside it — so the content sits inside the border and a translucent border
/// blends with the desktop, not the content. It runs over the COMPOSITED
/// redirected texture, so it rounds the outer frame corners (titlebar included)
/// without ever clipping an individual client subsurface. No drop shadow is drawn
/// (KWin does not render one into this texture).
///
/// The border APPEARANCE (width / radius / colour) is no longer host state on
/// this struct: it is the resolved pack's own declared PARAMETERS, baked into the
/// CompiledSurfacePack's customParams/customColors at compile time and pushed by
/// pushBorderUniforms. This struct now only records WHICH pack chain renders and
/// the per-window hide-titlebar choice; the appearance lives with the pack.
struct WindowBorder
{
    /// True when THIS border owns the window's OffscreenEffect redirect +
    /// border shader slot. False while an animation transition has taken over
    /// the slot (the animation path's begin/end coordinates the handover) —
    /// the per-frame uniform push and the transition-end re-apply both consult
    /// this so the border path never fights the transition lifecycle.
    bool shaderApplied = false;

    /// The resolved decoration shader-pack chain for this window
    /// (DecorationProfile::effectiveChain()), e.g. {"border"} or {"border",
    /// "glow"}. The idle present path composites the FULL chain
    /// (renderSurfaceChainComposite folds chain[1..] over the base); only the
    /// animation surface-layer path renders chain[0] (basePackId) alone.
    QStringList chain;

    /// The base pack id to render — chain.value(0), defaulting to "border".
    /// The render path (drawWindow / pushBorderUniforms / renderSurfaceBufferPasses)
    /// looks this up in m_compiledPacks to get the CompiledSurfacePack instead
    /// of the old single global border shader.
    QString basePackId;

    /// True when the base "border" pack in `chain` is driven by the window
    /// rules (the rule-backed appearance path) rather than a user decoration
    /// pack. When set, the fields below carry the per-window border appearance
    /// resolved from the rules (resolveWindowAppearance); pushBorderUniforms
    /// pushes them in place of the "border" pack's shared metadata defaults so
    /// each window can show a different width / radius / colour. The shader picks
    /// activeColor vs inactiveColor by uSurfaceFocused. False for a pure
    /// user-pack chain, where the pack's own baked param defaults apply.
    bool ruleBorder = false;
    int ruleBorderWidth = 0;
    int ruleBorderRadius = 0;
    QColor ruleBorderActiveColor;
    QColor ruleBorderInactiveColor;

    /// Transparent OUTER MARGIN (logical px) the chain's packs need around
    /// the window to draw into — the max over each pack's resolved
    /// `paddingParam` value (e.g. the glow pack's glowSize). When > 0 the
    /// window takes the padded composite path: the capture canvas is
    /// inflated by this margin on every side, the packs run over the padded
    /// canvas, and apply() presents the composite on a matching padded quad
    /// (prePaintWindow marks the window transformed so KWin doesn't clip).
    /// 0 = the classic path, byte-for-byte unchanged.
    int outerPadding = 0;

    /// True when any pack in the chain declares `"needsBackdrop": true`
    /// (frost / glass). Forces the composite-fold path, has paintWindow
    /// capture the scene behind the window each frame (uBackdrop), and
    /// drives continuous repaints (the backdrop changes without any damage
    /// landing on this window).
    bool needsBackdrop = false;

    /// The window's rule-resolved opacity (SetOpacity), 1.0 when no rule
    /// applies. Custom MapTexture redirect shaders IGNORE
    /// WindowPaintData::opacity (KWin only applies it through its own
    /// default shader's modulation), so a decorated window would render
    /// fully opaque no matter the rule. When < 1.0 the window routes
    /// through the composite fold, whose window CAPTURE applies the dim
    /// (the nested draw uses KWin's default modulating shader) — content
    /// dims once, decoration packs stack over it at full strength, and a
    /// frost pack gets the translucency it fills. Kept fresh by
    /// updateWindowBorder, which re-runs on every trigger that can change
    /// a rule verdict (focus, snap state, rule/config edits).
    double ruleOpacity = 1.0;

    /// True when any chain pack declares `"handlesOpacity": true`. The
    /// present pass then pushes uOpacity = 1.0 (no final modulation) and the
    /// declaring pack applies uSurfaceOpacity itself.
    bool chainHandlesOpacity = false;

    /// Damage bookkeeping for padded chains across window moves/resizes:
    /// KWin damages the window's own old/new rects on a geometry change, but
    /// not the margin band OUTSIDE them, so the glow would trail during a
    /// drag. updateWindowBorder connects windowFrameGeometryChanged for
    /// padded windows and damages lastPaddedGeo ∪ the new padded rect at
    /// screen level; removeWindowBorder disconnects.
    QRectF lastPaddedGeo;
    QMetaObject::Connection paddedGeoConnection;

    /// Texcoord-handedness cache for the padded present quad built in
    /// apply() — same rationale as ShaderTransition::handednessCached: the
    /// convention comes from KWin's own window quad and doesn't shift, so
    /// derive it once from the first source quad instead of per frame.
    bool presentHandednessCached = false;
    double uAtLeft = 0.0;
    double uAtRight = 1.0;
    double vAtTop = 0.0;
    double vAtBottom = 1.0;

    /// Per-window resolved param values for each pack in `chain`, keyed by
    /// pack id — the window's DecorationProfile overrides translated to
    /// contract slots (ShaderInternal::resolveSurfaceParamValues). Filled by
    /// updateWindowBorder alongside the chain itself, so it refreshes on
    /// exactly the same triggers (tree change, tile/snap membership change).
    /// The render paths prefer these over the CompiledSurfacePack's baked
    /// baseline values; a missing key falls back to the baked ones.
    QHash<QString, SurfaceParamValues> packParamValues;
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
    ///   zone.* events that flow through `applyWindowGeometry` and inherit
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

    /// Surface-layer-stack render targets (compositor path). The window's
    /// surface with its active surface layers (border / rounded corners today;
    /// tint / glow / ... in future) composited in order, rendered each animated
    /// frame so the animation samples the LAYERED surface (bound as
    /// `uSurfaceLayer`) instead of the bare live `uTexture0` — surface layers
    /// stay visible for the whole transition rather than vanishing when the
    /// animation shader takes the draw slot.
    ///
    /// Two textures for ping-pong chaining when more than one layer is active
    /// (layer N reads slot `N%2`, writes slot `(N+1)%2`); a single-layer stack
    /// (the common border-only case) uses slot 0 alone. Reused across frames and
    /// reallocated only when the window's expanded size × scale changes; freed
    /// with the transition. `renderSurfaceChain` returns the slot holding the
    /// final composited surface.
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
