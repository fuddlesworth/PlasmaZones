// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical uniform contract for SURFACE shaders, one of the four shader-pack
// categories alongside animation (data/animations), overlay (data/overlays) and
// pointer (data/pointer). A surface shader decorates a rendered surface: it
// samples the surface's own content (uTexture0) and the geometry of the content
// rect within that texture, and paints decoration over it. Two dozen packs ship
// today, from the plain border and rounded corners through tint, glow, frost,
// blur and the refracting glass family.
//
// DUAL-RUNTIME, like the animation contract. The SAME pack source compiles for:
//
//   • The compositor (kwin-effect): uTexture0 is the redirected window surface
//     (expanded geometry, device px). The effect prepends `#define
//     PLASMAZONES_KWIN` after `#version`, so the KWin branch below is taken;
//     uniforms are classic default-block uniforms set with GLShader::setUniform.
//
//   • The daemon (PhosphorRendering Qt-RHI): uTexture0 is the daemon surface's
//     own texture. Qt-RHI's SPIR-V pipeline mandates UBO-bound uniforms, so the
//     #else branch binds a std140 UBO at binding=0, mirrored byte-for-byte by
//     SurfaceShaderUniforms.h and filled by SurfaceUniformProfile — the daemon
//     consumes it through SurfaceShaderItem on the OSD/popup decoration hosts.
//
// The host (compositor or daemon) provides the surface STATE: the geometry of
// the content rect within the texture in device px, a logical-to-device scale, a
// focus value, and — for packs that ask for them — time, the audio spectrum, the
// cursor, the backdrop and its rect, the folded opacity, and the sizes of any
// buffer-pass and user-texture inputs. Every one of those is declared below.
// Decoration APPEARANCE — border width, corner radius, colours, glow, etc. — is
// NOT host state: it is each pack's own declared PARAMETERS
// (customParams / customColors via the standard parameter slots), so "border" is
// just a shader whose width/radius/colour are its params, not a separately
// defined concept. Lengths a pack declares are LOGICAL px; multiply by
// uSurfaceScale to reach the device-px space the geometry uniforms use.

#ifndef PLASMAZONES_SURFACE_UNIFORMS_GLSL
#define PLASMAZONES_SURFACE_UNIFORMS_GLSL

#ifdef PLASMAZONES_KWIN

// ── Compositor branch — classic default-block uniforms ──────────────────────
// uTexture0 lives on texture unit 0. The effect binds it and sets the sampler
// explicitly in the composite fold rather than relying on the unset-sampler
// default, because the fold rebinds unit 0 several times per pack and an unset
// sampler would follow whatever was bound last.
uniform sampler2D uTexture0;

// Geometry of the surface texture and the content rect within it (device px).
// uSurfaceSize is the full uTexture0 extent (the compositor redirects the
// EXPANDED window geometry: frame + decoration + shadow — further inflated by
// the chain's outer margin when a pack declares `paddingParam`, so an outer
// effect always has canvas to draw into). The content/frame rect sits at
// uSurfaceFrameTopLeft (top-down) and spans uSurfaceFrameSize, so the
// decoration rounds to the frame corners, not the padded bounds.
uniform vec2 uSurfaceSize;
uniform vec2 uSurfaceFrameTopLeft;
uniform vec2 uSurfaceFrameSize;

// Logical-to-device scale: multiply a pack's logical-px parameter (e.g. a
// border width or corner radius) by this to reach the device-px space the
// geometry uniforms above are in.
uniform float uSurfaceScale;
// How focused/active the surface is, 0.0 to 1.0. A pack with active/inactive
// colour params mixes them on this rather than the host picking one, and should
// test `>= 0.5` rather than `== 1.0`, because the ends are not the only values
// it takes. The compositor ramps it toward its target over the focus-fade
// duration setting, so a real window passes through every value between; daemon
// hosts (the settings preview, OSD and popup decorations) push a hard 0.0 or
// 1.0. The same pack therefore cross-fades on a window and snaps in a preview,
// and cannot tell which host it has.
uniform float uSurfaceFocused;

// Continuously-increasing seconds, for ANIMATED packs (pulsing glow, shimmer,
// …) — the same role iTime plays in the overlay / animation categories. The
// host captures an epoch at first use so this begins near 0 (float precision).
// The linker drops it for a static pack (e.g. the border). iTime is not the only
// repaint driver, though: a pack that reads the audio spectrum or iMouse is
// driven by those instead, so "references no iTime" means free only for a pack
// that reads none of the three.
uniform float iTime;

// Audio spectrum bar count (CAVA), 0 when the audio visualizer is off. Both
// runtimes populate it: the daemon writes the UBO member for its OSD / popup
// surfaces, and the KWin effect pushes it from its own CAVA provider for window
// decorations. surface_audio.glsl's helpers no-op while it is 0. The
// uAudioSpectrum sampler is declared in surface_audio.glsl, which packs #include
// to opt in.
uniform int iAudioSpectrumSize;

// Pack-specific tweakable parameters (declared in metadata.json, addressed by
// `#define p_<id> customParams[N].x` / `customColors[N]` preambles the registry
// generates — identical to the animation/overlay categories). The metadata KEY
// for a preset value is the flat `customParamsN_x` spelling; the GLSL the
// preamble emits is the indexed one, and only the latter appears in a shader.
uniform vec4 customParams[8];
uniform vec4 customColors[16];

// Multipass buffer-pass output SIZES: iChannelResolution[N].xy is the pixel
// size of iChannelN, FOR N < 4 ONLY. There are up to eight buffer passes and
// only four declared sizes, so a pass reading iChannel4..7 sizes it with
// textureSize(iChannelN, 0) instead, which the builtin Kawase passes do for
// every channel. The iChannelN samplers themselves live in the opt-in
// surface_multipass.glsl module — a single-pass pack (the border) declares
// neither. This resolution array stays in the core contract because it is a
// pinned std140 UBO member on the daemon.
uniform vec4 iChannelResolution[4];

// Backdrop capture GATE: 1.0 when the host bound something behind the surface
// this frame (packs that declare `"needsBackdrop": true`), else 0.0. On the
// compositor that is the captured scene under the window's canvas. A daemon or
// preview host has no scene to capture, but MAY bind the desktop wallpaper as a
// stand-in, so this reads 1.0 there too whenever one is bound and 0.0 when
// nothing is. Branch on the gate, never on the runtime. The capture sampler +
// backdropTexel() live in the opt-in surface_backdrop.glsl module; this gate
// stays here so a pack can branch on it without pulling in the sampler.
uniform float uHasBackdrop;

// LEGACY — always 1.0 on both runtimes. This used to carry the window's
// rule-resolved SetOpacity for packs declaring the retired handlesOpacity
// contract; SetOpacity is layer-backed now (the plain opacity-tint layer
// folds it into its own pack param) and content dimming inside a pack is a
// plain pack parameter (frost/glass `contentOpacity`). Kept declared so
// existing third-party packs referencing it keep compiling and the daemon
// UBO layout is unchanged.
uniform float uSurfaceOpacity;

// Cursor position for hover-reactive packs. .xy is the cursor in the SAME
// top-down device-px space as the geometry uniforms above (origin at the
// padded canvas's top-left), (-1, -1) when the cursor is outside the canvas.
// .zw is .xy normalized by uSurfaceSize (negative while .xy carries the
// sentinel), so `iMouse.x < 0.0` is the canonical off-surface test on both
// runtimes. A pack that reads iMouse does NOT need `"animated": true`, and is
// better off without it: there IS a per-cursor-move damage path, and the
// compositor drives it from the introspected iMouse uniform rather than from
// any metadata flag, comparing the cursor its fold keyed on so the repaints
// stop as soon as the pointer does. Declaring `animated` asks instead for a
// repaint every vsync for as long as the window is up. On a daemon host no
// hover source is wired at all, so iMouse holds the off-surface sentinel
// there and a hover pack simply reads "not hovered".
uniform vec4 iMouse;

// User-declared image textures (metadata `textures` — logo, mask, pattern).
// Bound to dedicated units at draw time. iTextureResolution[i].xy is the pixel
// size of uTexture<i>, so index 0 is the surface's own content and a metadata
// slot N, which feeds uTexture<N+1>, has its size at index N+1. The sampler
// names are one-based over the metadata list and iTextureResolution is
// zero-based over the texture slots, which is a real trap and the reason it is
// spelled out twice. Mirrors the animation contract. A slot with no loadable
// file reads transparent black.
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
uniform vec4 iTextureResolution[4];

#else

// ── Daemon branch — std140 UBO at binding 0 ─────────────────────────────────
// qt_Matrix / qt_Opacity lead the block to match Qt Quick's scene-graph
// expectation (same as the animation/overlay UBOs). Field order is laid out for
// std140: vec2s packed in pairs, vec4/array members 16-aligned. The C++
// mirror struct + offset static_asserts are added with the daemon consumer.
layout(std140, binding = 0) uniform SurfaceUniforms {
    mat4 qt_Matrix;              // offset 0   (64)
    float qt_Opacity;            // offset 64  (4)
    float uSurfaceScale;         // offset 68  (4)
    float uSurfaceFocused;       // offset 72  (4)
    float iTime;                 // offset 76  (4) — fills the former std140 pad
    vec2 uSurfaceSize;           // offset 80  (8)
    vec2 uSurfaceFrameTopLeft;   // offset 88  (8)
    vec2 uSurfaceFrameSize;      // offset 96  (8)
    float uHasBackdrop;          // offset 104 (4) — 1 when the host bound a backdrop (scene or wallpaper stand-in), else 0
    float uSurfaceOpacity;       // offset 108 (4) — LEGACY: a constant 1.0 on both runtimes, nothing to read.
                                 //   NOT the blur/glass family's `contentOpacity`, which is a pack
                                 //   PARAMETER reaching the shader as p_contentOpacity via customParams.
    vec4 customParams[8];        // offset 112 (128)
    vec4 customColors[16];       // offset 240 (256)
    vec4 iChannelResolution[4];  // offset 496 (64) — multipass buffer sizes (.xy)
    int iAudioSpectrumSize;      // offset 560 (4) — CAVA bar count (0 = audio off)
    // implicit 12-byte std140 pad here — vec4 iMouse below is 16-aligned.
    vec4 iMouse;                 // offset 576 (16) — cursor in the surface texture's
                                 //   top-down device-px space (.xy; negative when
                                 //   off-surface / no hover source), .zw = .xy
                                 //   normalized by uSurfaceSize
    vec4 iTextureResolution[4];  // offset 592 (64) — texture sizes (.xy);
                                 //   index i is uTexture<i>, so index 0 is the
                                 //   surface and metadata slot N is index N+1
    vec4 uBackdropRect;          // offset 656 (16) — the sub-rect of the bound
                                 //   backdrop this surface should sample, in
                                 //   normalized texture coords (xy = min,
                                 //   zw = size). (0,0,1,1) means the whole
                                 //   texture. See surface_backdrop.glsl.
};                               // total 672 bytes, no trailing pad

layout(binding = 11) uniform sampler2D uTexture0;
// User-declared image textures (metadata `textures`), bindings 12-14 — the
// same sampler-name and binding-point dialect the animation and overlay
// categories use, provided by the base ShaderEffect's user-texture plumbing.
layout(binding = 12) uniform sampler2D uTexture1;
layout(binding = 13) uniform sampler2D uTexture2;
layout(binding = 14) uniform sampler2D uTexture3;

// The multipass iChannel sampler bindings (2-9) live in surface_multipass.glsl,
// which a multipass pack includes; the border and other single-pass packs bind
// only uTexture0.

#endif // PLASMAZONES_KWIN

// ── Shared helpers (runtime-agnostic; packs use these, not raw uniforms) ─────

// This fragment's position in the surface texture, TOP-DOWN device pixels, with
// the content/frame rect occupying [uSurfaceFrameTopLeft, +uSurfaceFrameSize].
// `uv` is the incoming vTexCoord. The compositor's redirected FBO is
// bottom-origin (Y-up), so the Y is flipped there to reach the top-down space
// the geometry uniforms are expressed in.
vec2 surfacePixel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    return vec2(uv.x, 1.0 - uv.y) * uSurfaceSize;
#else
    return uv * uSurfaceSize;
#endif
}

// The surface's own texel at `uv`, upright on both runtimes. Both runtimes end
// up sampling with the incoming `uv` directly: the compositor delivers a Y-up
// vTexCoord against its bottom-origin redirect FBO, and the daemon delivers a
// Y-down vTexCoord against Qt-RHI's top-origin texture — either way `uv` already
// addresses the texel upright (the daemon path lets surface.vert's qt_Matrix
// carry the per-backend NDC correction, mirroring animation_uniforms.glsl's
// surfaceColor daemon branch, rather than flipping here).
vec4 surfaceTexel(vec2 uv) {
    return texture(uTexture0, uv);
}

// backdropTexel() (the scene behind the surface) moved to the opt-in
// surface_backdrop.glsl module — a pack that samples the backdrop includes it.

#endif // PLASMAZONES_SURFACE_UNIFORMS_GLSL
