// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical uniform contract for animation/transition shaders. Both
// PlasmaZones runtimes that load animation shaders honor this header:
//
//   • Daemon overlay-surface execution (PhosphorRendering::ShaderEffect →
//     ShaderNodeRhi → BaseUniforms UBO at binding=0). Qt-RHI's SPIR-V
//     pipeline mandates UBO-bound uniforms; default-block uniforms aren't
//     supported. The UBO branch below is std140-aligned with
//     `PhosphorShaders::BaseUniforms` for its 672-byte base, extended to
//     720 bytes by `AnimationUniformExtension`.
//
//   • Compositor window-content execution (kwin-effect → KWin::GLShader,
//     classic OpenGL). KWin's `GLShader::setUniform(loc, val)` API
//     addresses default-block uniforms only and never binds UBOs. The
//     kwin-effect prepends `#define PLASMAZONES_KWIN` after the
//     `#version` line of every animation shader before compile, which
//     selects the default-block branch below.
//
// Author guidance: include this file from each animation shader's
// effect.frag with `#include <animation_uniforms.glsl>` and use the
// declared names directly (`iTime`, `iResolution`, `customParams[N]`,
// `uTexture0`, etc.) — both branches expose the same identifiers, so
// shader code does not need its own `#ifdef PLASMAZONES_KWIN` blocks.
// The `#define PLASMAZONES_KWIN` switch describes the uniform-binding
// ABI, not the runtime's feature set.
//
// Coordinate convention: `vTexCoord` is a Y-down screen UV — y = 0 at
// the TOP of the surface — on BOTH runtimes, so effect-space math may
// use it directly. The vertex stage is what guarantees that: the
// daemon's Qt-RHI quad emits texCoord Y-down already and passes it
// through (`vTexCoord = texCoord`); the kwin path receives texCoord
// from KWin's Y-up offscreen FBO and flips it
// (`vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y)`). `gl_Position`
// also differs: the kwin path multiplies `position` by
// `modelViewProjectionMatrix`, the daemon emits clip-space directly.
// See `kKwinDefaultVertexSource` in
// `kwin-effect/plasmazoneseffect/shader_textures.cpp` and
// `kDefaultVertexShaderSource` in
// `libs/phosphor-rendering/src/shadereffect.cpp`.
//
// Surface texture: the redirected surface is stored in a Y-up texture
// on the kwin path (KWin's bottom-origin FBO) and a Y-down texture on
// the daemon, so — unlike `vTexCoord` — it does NOT share one
// convention. Never sample `uTexture0` directly; call
// `surfaceColor(uv)` (defined at the end of this header). It flips on
// the kwin path so one shader source reads the surface upright on
// both runtimes.
//
// Layout-drift guard: the offsets in the UBO branch MUST stay aligned
// with `PhosphorShaders::BaseUniforms`. The C++ side enforces that via
// `static_assert(offsetof(...))` in `<PhosphorShaders/BaseUniforms.h>`
// for every BASE field declared below (through iIsReversed at 660); the
// anchor-extension tail (iSurfaceScreenPos .. iAnchorRectInTexture,
// bytes 672-719, 720 total) is supplied by AnimationUniformExtension and pinned by
// the size static_asserts in `<PhosphorAnimation/AnimationUniformExtension.h>`.
// If any assert fails after a C++-side change, this header has to move
// in lockstep. The bake test in
// `tests/unit/ui/test_animation_shader_bake.cpp` surfaces GLSL-side
// drift by running `qsb` over every built-in shader.

#ifndef PLASMAZONES_ANIMATION_UNIFORMS_GLSL
#define PLASMAZONES_ANIMATION_UNIFORMS_GLSL

#ifdef PLASMAZONES_KWIN

// ─── KWin classic-GL default-block branch ──────────────────────────────
// `qt_Matrix` / `qt_Opacity` / `_appField0` / `_appField1` /
// `iFlipBufferY` are daemon-only (Qt scene-graph transform/opacity,
// consumer escape-hatch ints, daemon Y-flip signal); they are absent
// here. `iTimeHi` is documented dead and never read by animation
// shaders, but it stays in the contract for parity with the daemon
// side — declared as a default-block uniform so source written against
// the canonical header still compiles. Reads zero on this path.
//
// Sampler binding decorations are intentionally absent: KWin's
// `OffscreenData::paint` binds the redirected window texture to the
// default active unit (TEXTURE0) with no `glActiveTexture` call. With
// `#version 450`, an explicit `layout(binding = N)` would pin the
// sampler to unit N at link time and KWin's bind to unit 0 would be
// invisible — texture sample returns transparent, transition is a
// see-through no-op. Default-bound samplers fall back to GL's default
// (unit 0), which matches KWin's bind.
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform vec2 iResolution;
uniform vec4 iMouse;
uniform vec4 iDate;
uniform vec4 customParams[8];
uniform vec4 customColors[16];
// `iChannelResolution[4]` from the UBO branch is intentionally absent
// here: the kwin-effect never calls `setUniform` for it (single-pass, no
// buffer FBOs), and no animation shader references it. Adding a
// default-block declaration would compile but read garbage at runtime —
// better to surface the gap as a compile error if a future shader
// reaches for it on the kwin path. (The UBO branch keeps the field for
// std140 layout parity with `PhosphorShaders::BaseUniforms`; see
// static_asserts in `<PhosphorShaders/BaseUniforms.h>`.)
// `iAudioSpectrumSize` is likewise absent from THIS header on the kwin
// branch, but it is not a dead end: the opt-in audio module
// (data/animations/shared/audio.glsl) declares it as a default-block
// uniform alongside the `uAudioSpectrum` sampler, and paint_pipeline.cpp
// pushes both per frame for packs that include the module. A pack that
// reaches for it WITHOUT the module still gets the compile error.
// `iTextureResolution[4]` IS populated on the kwin path: the per-effect
// uTexture<N> setter loop in `paint_pipeline.cpp::paintWindow` writes the
// pixel size of each user texture into this uniform array before
// `drawWindow`. Listed here distinct from the "absent on kwin" set above
// (iChannelResolution, iAudioSpectrumSize) so a future reader doesn't
// mistake the active declaration for an undocumented "compile-but-zero"
// hazard.
uniform vec4 iTextureResolution[4];
uniform float iTimeHi;
// 1 when the runtime is driving this leg in the "reverse" direction
// (window.close / going-to-minimized / unmaximize on the kwin path,
// hide leg on the daemon path); 0 for the forward direction. Symmetric
// shaders ignore this — the runtime ALSO flips iTime so they auto-mirror.
// Asymmetric shaders (matrix's directional rain + open-vs-close window
// reveal) branch on it. See UBO branch below for the full contract.
uniform int iIsReversed;
// .xy = surface origin in logical-screen pixels; .zw = (screenW, screenH).
// Vertex / fragment shaders that need to know where the surface sits on
// its host screen (fly-in from closest edge, screen-relative noise) read
// this. Both runtimes populate it once per leg attach + on every anchor
// or window geometry signal.
uniform vec4 iSurfaceScreenPos;
// Anchor (card) pixel size in logical pixels. Decoupled from iResolution
// because Qt auto-resets iResolution to the QQuickItem's bounds on any
// geometry event: a `fboExtent: "surface"` shader item is surface-sized
// and that auto-reset would clobber any anchor-size override. Vertex
// shaders mapping a captured small texture into a surface-sized FBO
// read this for the card's pixel dimensions; iResolution still carries
// the FBO size as usual.
uniform vec2 iAnchorSize;
// Anchor's top-left position inside the shader item's FBO, in logical
// pixels. Combined with `iAnchorSize` and `iResolution`, shaders compute
// the anchor's UV region inside the FBO:
//   vec2 anchorTopLeftUv = iAnchorPosInFbo / iResolution;
//   vec2 anchorSizeUv    = iAnchorSize    / iResolution;
//   vec2 anchorUv        = (vTexCoord - anchorTopLeftUv) / anchorSizeUv;
// This generalises the previous `customParams[7].x` ring-padding remap
// (morph, broken-glass) and the surface-extent vertex remap (fly-in)
// onto one contract that works for any FBO size the runtime allocates.
// On the kwin-effect path the value is the captured window's offset
// within whatever rect `vTexCoord` spans: the shadow inset for an
// anchor-extent transition, the window's position within the output for
// a surface-extent transition. `anchorRemap` (anchor_remap.glsl)
// consumes it.
uniform vec2 iAnchorPosInFbo;

// Window's effective rule-resolved opacity in [0, 1] — compositor path
// only. A `SetOpacity` rule must dim the window for the whole
// transition, but the custom transition shader is compiled without the
// `Modulate` trait, so KWin never applies `data.opacity()` to it.
// `surfaceColor()` below multiplies the
// premultiplied surface sample by this uniform so the dim holds across
// the animation instead of snapping in only after it ends. The
// kwin-effect pushes 1.0 for windows with no matching rule, so the
// multiply is a no-op in the common case. Daemon-only animations have no
// window-rule opacity, so the UBO branch omits this field entirely.
uniform float iWindowOpacity;

// Card's UV sub-rect within `uTexture0`, as (x, y, width, height) in
// the texture's [0, 1] space. Both runtimes capture more than the bare
// card: KWin's OffscreenEffect redirects the whole window item —
// decoration and shadow included — so `uTexture0` covers the EXPANDED
// geometry; the daemon captures the shader anchor, which a PopupFrame
// enlarges by a glow margin so the glow animates with the card.
// `surfaceColor()` folds this rect in so a card-space [0, 1] sample
// addresses the card's sub-region of that padded texture rather than
// stretching the whole texture across the card. A bare card-sized
// anchor carries the (0, 0, 1, 1) identity.
//
// Lifecycle: populated on every leg attach (alongside `iAnchorPosInFbo`
// / `iAnchorSize`) and on every geometry change while the leg is live.
// kwin-effect: `paint_pipeline.cpp::paintWindow`'s
// `cached->iAnchorRectInTextureLoc` setUniform site writes the value
// computed by `ShaderInternal::computeTextureSubRect(frameGeo,
// expandedGeo)`. Daemon: `SurfaceAnimator::syncShaderGeometryNow` pushes
// it through `AnimationUniformExtension`. Both stamp the value before
// the first painted frame, so a fragment never sees the GL default
// `vec4(0)` — `surfaceColor` would otherwise sample the corner texel
// for every pixel and flash a one-frame solid colour.
uniform vec4 iAnchorRectInTexture;

// uTexture0 — redirected window content (the surface the shader is
// transitioning). Auto-bound by the runtime: KWin's OffscreenEffect
// binds the redirected window texture to TEXTURE0 before drawWindow,
// and the daemon's SurfaceAnimator wires the shaderAnchor's live
// texture provider to slot 0 of the underlying user-texture array.
// Animation shaders treat this as their input image; overlay shaders
// (see data/overlays/) use the same uTexture0 name for their first
// user-declared texture, so author shader source compiles unchanged
// across categories — semantics differ only at the runtime binding
// layer.
uniform sampler2D uTexture0;
// User-declared textures — see AnimationShaderEffect::TextureSlot in
// metadata.json or the runtime `uTexture<N>` parameter override. The
// kwin-effect binds these to TEXTURE1..3 before drawWindow; the
// daemon binds them to SRB bindings 8..10 via
// ShaderNodeRhi::setUserTexture. Default `vec4(0)` (transparent black)
// when neither declaration nor override resolves to a loadable file.
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
// Surface-layer stack output (kwin path). When `iHasSurfaceLayer != 0`,
// `surfaceColor()` samples this in place of `uTexture0`: it holds the window
// with its surface layers (border / rounded corners, future tint, ...) already
// composited, so an animation runs OVER the layered surface and the border
// does not disappear when the animation shader takes the draw slot. The
// kwin-effect renders it (PlasmaZonesEffect::renderSurfaceChain) and binds it
// to a dedicated unit; it shares uTexture0's bottom-origin layout, so the same
// Y-flip applies when sampling. Default unit / `iHasSurfaceLayer == 0` leaves
// the unlayered path untouched.
uniform sampler2D uSurfaceLayer;
uniform int iHasSurfaceLayer;

// 1 when uOldWindow holds a real captured old-content snapshot, 0 when the
// transition began without a capture (lifecycle moves/resizes). Old-content
// samplers gate on this and fall back to surfaceColor() — the no-snapshot
// compositor fallback points uOldWindow at unit 0 (the RAW window), which
// would otherwise blank the decoration for the old side of a cross-fade.
uniform int iHasOldWindow;

// Interactive-move motion state (held move/resize transitions only; zero
// otherwise). iMoveVelocity is spring-smoothed logical px/s and decays
// through zero with a slight overshoot after release, so velocity-driven
// deformations settle naturally. iMoveOffset is the raw frame-origin
// displacement since the grab, logical px.
uniform vec2 iMoveVelocity;
uniform vec2 iMoveOffset;
// Second, looser spring over the same motion: lags iMoveVelocity and rings
// longer after release. Blend by distance from the grip for phase spread.
uniform vec2 iMoveVelocity2;
// Motion history for held move/resize transitions: slot k = the window
// origin k*15 ms ago, relative to now (all zeros at rest). Sample at a
// per-vertex delay for delayed path-following deformations.
uniform vec2 iMoveTrail[16];
// Solved 4x4 soft-body control lattice for held move/resize transitions:
// node (i,j) = iMoveMesh[i + 4*j], deflection from its ideal grid spot on
// the current frame in logical px (row 0 top). All zeros at rest. Sample
// via bilinear or bicubic Bezier across the render mesh for physics-driven
// deformation.
uniform vec2 iMoveMesh[16];
// The animated surface's [0,1] sub-rect within uSurfaceLayer's canvas — the
// layer analogue of iAnchorRectInTexture. The compositor pads the layer canvas
// by the decoration chain's outer margin (glow reach), so the layer cannot be
// sampled with uTexture0's mapping; surfaceColor() remaps through this
// instead. An unpadded layer carries the same value as iAnchorRectInTexture.
uniform vec4 iLayerRectInTexture;

#else

// ─── Daemon Qt-RHI / SPIR-V UBO branch (default) ───────────────────────
// Std140 array stride note: `int[N]` and `float[N]` arrays have a per-
// element stride of 16 bytes in std140 (rule 4: rounded up to vec4
// alignment). That makes `int _pad0[2]` 32 bytes — NOT 8 — which would
// shove the next field 24 bytes past where the C `BaseUniforms` upload
// places it. Sibling daemon `data/overlays/common.glsl` solves this by
// declaring no explicit padding and relying on std140's natural
// vec4-alignment of the next array to bridge the gap. Match that
// pattern here: after `int iFlipBufferY` (4 bytes at offset 580,
// occupying [580, 584)), the next `vec4 iTextureResolution[4]` is
// auto-aligned to offset 592 by std140 (rule 4 → 16-byte boundary),
// implicitly filling the same 8 bytes the C struct's
// `_pad_after_audioSpectrum[2]` covers. Likewise `iSurfaceScreenPos`
// below is auto-aligned to a 16-byte boundary (rule 2: vec2 at 672),
// bridging the 8 bytes C's `_pad_after_iIsReversed[2]` owns after
// `iIsReversed` at 660 and landing the base block at 672 bytes. The
// iSurfaceScreenPos / iAnchor* fields then extend the AnimationUniforms
// UBO block to 720 bytes (they are supplied by AnimationUniformExtension,
// not BaseUniforms, which stays pinned at 672).
layout(std140, binding = 0) uniform AnimationUniforms {
    mat4 qt_Matrix;              // offset 0   (64 bytes) — Qt scene-graph transform; daemon-only
    float qt_Opacity;            // offset 64  (4 bytes)  — Qt scene-graph opacity; daemon-only
    float iTime;                 // offset 68  — animation progress in [0, 1]
    float iTimeDelta;            // offset 72  — real-time seconds between SurfaceAnimator
                                 //              ticks (daemon path)
    int iFrame;                  // offset 76  — per-leg frame counter starting at 0
    vec2 iResolution;            // offset 80  — surface size in logical pixels
    int _appField0;              // offset 88  — consumer escape-hatch int (daemon-only)
    int _appField1;              // offset 92  — consumer escape-hatch int (daemon-only)
    vec4 iMouse;                 // offset 96  — cursor position in shader-local pixels
                                 //              (.xy = position, (-1,-1) when off-region)
    vec4 iDate;                  // offset 112 — year, month, day, seconds-since-midnight
    vec4 customParams[8];        // offset 128 (128 bytes) — per-effect float/int/bool parameter slots
    vec4 customColors[16];       // offset 256 (256 bytes) — per-effect color parameter slots
    vec4 iChannelResolution[4];  // offset 512 (64 bytes)  — buffer texture sizes (multipass)
    int iAudioSpectrumSize;      // offset 576 — audio spectrum bin count
    int iFlipBufferY;            // offset 580 — always 1 (Y-flip); daemon-only
    // implicit 8-byte std140 padding here — see layout note above.
    vec4 iTextureResolution[4];  // offset 592 (64 bytes)  — user texture sizes (bindings 7-10)
    float iTimeHi;               // offset 656 — wrap-offset counterpart of iTime;
                                 //              always 0 on the animation path. Do NOT
                                 //              read in animation shaders — exists only
                                 //              to keep std140 layout aligned with the
                                 //              overlay UBO so a single effect.frag
                                 //              source compiles for either runtime.
    int iIsReversed;             // offset 660 — 1 on reverse legs (close / hide /
                                 //              unmaximize), 0 on forward legs.
                                 //              Symmetric shaders ignore this; the
                                 //              runtime ALSO flips iTime for reverse
                                 //              legs so they auto-mirror. Asymmetric
                                 //              shaders branch on this when the iTime
                                 //              flip alone can't express the open-vs-
                                 //              close difference (e.g. matrix's rain
                                 //              direction + windowAlpha reveal).
    // implicit 8-byte std140 pad here — `vec4 iSurfaceScreenPos` below
    // has 16-byte alignment, so std140 forces the next field to offset
    // 672. The C struct mirrors this with `_pad_after_iIsReversed[2]`
    // (BaseUniforms' trailing pad; the anchor fields themselves live in
    // AnimationUniformExtension).
    vec4 iSurfaceScreenPos;      // offset 672 (16 bytes) — .xy = surface origin
                                 //              in logical-screen pixels;
                                 //              .zw = (screenWidth, screenHeight).
                                 //              Populated by SurfaceAnimator (daemon)
                                 //              and paint_pipeline (kwin-effect) once
                                 //              per leg attach + on every anchor /
                                 //              window geometry change.
    vec2 iAnchorSize;            // offset 688 (8 bytes) — anchor (card) pixel size
                                 //              in logical pixels. Decoupled from
                                 //              iResolution because Qt's QQuickItem
                                 //              geometryChange auto-resets iResolution
                                 //              to the item's bounds on any geometry
                                 //              event, which clobbers an anchor-size
                                 //              override under fboExtent=surface.
                                 //              Vertex shaders that need the captured
                                 //              card's pixel dimensions read this.
    vec2 iAnchorPosInFbo;        // offset 696 (8 bytes). Anchor's top-left position
                                 //              inside the shader item's FBO, in
                                 //              logical pixels. Lets shaders compute
                                 //              the anchor's UV region for vTexCoord
                                 //              -> anchor-space remap (replaces the
                                 //              old customParams[7].x ring-padding
                                 //              fraction).
    vec4 iAnchorRectInTexture;   // offset 704 (16 bytes) — card's UV sub-rect within
                                 //              uTexture0, as (x, y, width, height) in
                                 //              the captured texture's [0,1] space.
                                 //              iAnchorPosInFbo ends at 704 which is
                                 //              already 16-aligned, so std140 inserts
                                 //              no pad. Total struct size 720 bytes,
                                 //              zero trailing pad. The daemon captures
                                 //              the shader anchor into uTexture0; when
                                 //              the anchor is larger than the visible
                                 //              card (a PopupFrame capture item that
                                 //              bundles the glow margin), this is the
                                 //              card's region within that texture.
                                 //              `animation.vert` divides texCoord by it
                                 //              so anchor-extent shaders see vTexCoord
                                 //              as [0,1] over the card; `surfaceColor`
                                 //              multiplies by it when sampling. A bare
                                 //              card-sized anchor carries the
                                 //              (0,0,1,1) identity. Populated by
                                 //              SurfaceAnimator per leg attach + on
                                 //              every geometry change.
};

layout(binding = 7) uniform sampler2D uTexture0;
// User-declared textures — see AnimationShaderEffect::TextureSlot or
// the runtime `uTexture<N>` parameter override. Bindings 8..10 match
// the overlay shader convention (data/overlays/shared/textures.glsl)
// so animation and overlay shaders speak the same sampler-name and
// binding-point dialect.
layout(binding = 8) uniform sampler2D uTexture1;
layout(binding = 9) uniform sampler2D uTexture2;
layout(binding = 10) uniform sampler2D uTexture3;

#endif // PLASMAZONES_KWIN

// ─── Surface sampling helper ───────────────────────────────────────────
// Read the transitioning surface (the redirected window / popup
// content) through this helper instead of sampling `uTexture0`
// directly. `uv` is a Y-down screen UV — pass `vTexCoord` (or any
// coordinate derived from it) straight in.
//
// The surface texture's storage orientation is the one thing the two
// runtimes genuinely disagree on: KWin's offscreen FBO is bottom-origin
// (Y-up), the daemon's Qt-RHI texture is top-origin (Y-down). This
// helper flips the sample coordinate on the kwin path so the returned
// texel is upright on both runtimes — keeping effect-space math, which
// uses the Y-down `vTexCoord` directly, decoupled from the surface
// texture's physical layout.
//
// Both runtimes capture more than the bare card into `uTexture0`: KWin
// redirects the EXPANDED window geometry (frame + decoration + shadow);
// the daemon captures the shader anchor, which a PopupFrame enlarges by
// a glow margin so the glow animates with the card. `iAnchorRectInTexture`
// is the card's UV sub-rect within that texture on both paths, so this
// helper folds the same remap regardless of runtime — a card-space
// `uv` addresses the card's region of `uTexture0`. A bare card-sized
// anchor carries the (0, 0, 1, 1) identity and the remap is a
// passthrough.
vec4 surfaceColor(vec2 uv) {
    vec2 t = iAnchorRectInTexture.xy + uv * iAnchorRectInTexture.zw;
#ifdef PLASMAZONES_KWIN
    // KWin's FBO is bottom-origin (Y-up) — flip last. Then scale by the
    // window-rule opacity: uTexture0 is premultiplied, so multiplying the
    // whole sample by the [0, 1] `iWindowOpacity` is the correct
    // premultiplied-alpha dim. This is what makes a `SetOpacity` rule hold
    // throughout a transition (with no `Modulate` trait the shader can't see
    // `data.opacity()`); the effect feeds 1.0 when no rule matches, so the
    // common case is unchanged. Every window-content shader reads the
    // surface through this helper (directly or via bmw_compat's
    // `getInputColor`), so the dim propagates uniformly without per-shader
    // edits.
    vec2 fc = vec2(t.x, 1.0 - t.y);
    // Surface-layer redirect: when the window has an active surface-layer stack
    // (border / rounded corners, ...), sample the pre-composited layered surface
    // so the animation runs over it. `uSurfaceLayer` shares uTexture0's
    // bottom-origin layout and premultiplied alpha, so the flip + opacity dim
    // are identical. Falls back to the live redirected surface when the window
    // has no layers (`iHasSurfaceLayer == 0`).
    if (iHasSurfaceLayer != 0) {
        // The layer canvas may be PADDED past uTexture0's extent (the
        // decoration chain's outer margin), so it has its own sub-rect
        // remap; uv outside [0,1] reaches the halo in the margin band.
        vec2 tl = iLayerRectInTexture.xy + uv * iLayerRectInTexture.zw;
        return texture(uSurfaceLayer, vec2(tl.x, 1.0 - tl.y)) * iWindowOpacity;
    }
    return texture(uTexture0, fc) * iWindowOpacity;
#else
    // The daemon's Qt-RHI texture is top-origin (Y-down) — no flip. The
    // daemon path has no window-rule opacity (SetOpacity is a compositor
    // window-rule feature), so there is no iWindowOpacity multiply here.
    return texture(uTexture0, t);
#endif
}

// The decoration chain's outer margin, expressed in the SAME card-space
// units `surfaceColor()`'s `uv` takes — half-width per axis, so the halo
// band spans `uv` in [-surfacePadRel(), 1 + surfacePadRel()].
//
// A card-space mask built on the bare [0, 1] range clips the halo at the
// frame edge, discarding the margin the compositor composited into
// `uSurfaceLayer`. Widening the mask by this value lets it through, and
// `surfaceColor()`'s unclamped `iLayerRectInTexture` remap resolves the
// out-of-range `uv` into the band.
//
// Derived from the two rect uniforms rather than pushed separately: both
// describe the SAME inner rect, `iAnchorRectInTexture` within uTexture0's
// expanded capture and `iLayerRectInTexture` within the layer canvas the
// compositor padded by the margin. So `inner / rect.zw` recovers each
// outer extent, and their difference is the two margins — the shadow inset
// common to both cancels, leaving the padding alone. An unpadded window
// carries canvas == expanded, hence equal rects and an exact zero, which
// keeps its mask bit-identical to the un-widened form.
//
// Valid only for SURFACE-EXTENT packs, whose card space is the frame: there
// both rects share the frame as inner rect, so the result is pad/frame in
// the card units their masks use. An anchor-extent pack carries
// `iAnchorRectInTexture == (0, 0, 1, 1)` (inner == expanded), so this would
// return pad/expanded instead — a different unit that would under-widen its
// mask. No anchor-extent pack calls this.
vec2 surfacePadRel() {
#ifdef PLASMAZONES_KWIN
    // No layer means no composited canvas, so `iLayerRectInTexture` carries
    // nothing to difference against.
    if (iHasSurfaceLayer == 0) {
        return vec2(0.0);
    }
    vec2 layerSpan = max(iLayerRectInTexture.zw, vec2(1.0e-6));
    vec2 anchorSpan = max(iAnchorRectInTexture.zw, vec2(1.0e-6));
    // max() against 0: a capture-scale clamp on a huge window can shrink the
    // canvas below the expanded rect, and a negative pad would eat INTO the
    // card rather than extend past it.
    return max(0.5 * (1.0 / layerSpan - 1.0 / anchorSpan), vec2(0.0));
#else
    // Daemon path: the padded decoration chain is a compositor-side fold.
    return vec2(0.0);
#endif
}

// ─── Direction helpers (T1.5) ──────────────────────────────────────────
// A transition plays forward (in: window.open / snapIn / show) or reverse
// (out: window.close / snapOut / hide). The runtime exposes that as
// `iIsReversed` (1 on reverse legs) and by flipping `iTime` (0→1 in, 1→0
// out) so symmetric shaders auto-mirror. Both names below resolve in the
// daemon UBO and the kwin default-block branch, so a shader uses them
// identically on either runtime.

// Direction as a clean boolean — replaces the `iIsReversed == 1` exact-
// equality test (branching on `!= 0` was the documented footgun).
#define p_reversed (iIsReversed == 1)

// Un-flipped, always-forward 0→1 leg progress. Bundled shaders used to
// hand-roll exactly this `(iIsReversed == 1) ? (1.0 - iTime) : iTime` to
// recover direction-independent progress; the T1.5 migration replaced
// every hand-rolled copy with this helper — new packs call it directly.
float legProgress() {
    return p_reversed ? (1.0 - iTime) : iTime;
}

// ─── Small shared helpers ──────────────────────────────────────────────
// Premultiply a straight-alpha colour (scale rgb by alpha). Many shaders
// build a straight-alpha result and premultiply once at output for the
// premultiplied-alpha blend pipeline; this is that final step by name.
vec4 premultiply(vec4 c) {
    return vec4(c.rgb * c.a, c.a);
}

// iResolution floored to at least 1 px per axis. Guards the early-frame
// `iResolution = (0, 0)` case (Qt resets it to the QQuickItem bounds on
// geometry change) so per-pixel math never divides by zero.
vec2 resolutionSafe() {
    return max(iResolution, vec2(1.0));
}

// ─── Final-colour hook (HDR colour management) ─────────────────────────
// Every final `fragColor` write in the animation pipeline routes through
// `PZ_FINALIZE_COLOR(...)`: the entry scaffold's generated main()s
// (animationshaderregistry.cpp) and bmw_compat's `setOutputColor()`. The
// guarded default below is identity.
//
// The kwin-effect WINDOW-animation path overrides it before this header
// is included: shader_textures.cpp splices KWin's colormanagement.glsl
// plus `#define PZ_FINALIZE_COLOR(c) pzFinalizeColor(c)` right after
// `#version`, converting the shader's sRGB output into the output's
// blending space. Installing a shader via OffscreenEffect::setShader
// REPLACES KWin's present shader (which normally carries that
// conversion), so without the override HDR outputs render every window
// animation dim and desaturated.
//
// Everything else keeps identity: the daemon RHI runtime (Qt colour-
// manages the scene graph output), shadervalidate, the bake tests, and
// the desktop-switch path (its capture FBOs inherit the output's
// colorDescription, so its inputs already live in the blending space and
// converting again would be wrong).
#ifndef PZ_FINALIZE_COLOR
#define PZ_FINALIZE_COLOR(c) (c)
#endif

#endif // PLASMAZONES_ANIMATION_UNIFORMS_GLSL
