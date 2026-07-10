// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical uniform contract for SURFACE shaders — the third shader-pack
// category, alongside animation (data/animations) and overlay (data/shaders).
// A surface shader decorates a rendered surface: it samples the surface's own
// content (uTexture0) and the geometry of the content rect within that texture,
// and paints decoration (rounded corners + border today; tint / glow / frost in
// future packs). The window border/rounded-corner effect is the first surface
// pack (data/surface/border).
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
// The host (compositor or daemon) provides only the surface GEOMETRY (the
// content rect within the texture, in device px), a logical-to-device SCALE, and
// a FOCUS flag. Decoration APPEARANCE — border width, corner radius, colours,
// glow, etc. — is NOT host state: it is each pack's own declared PARAMETERS
// (customParams / customColors via the standard parameter slots), so "border" is
// just a shader whose width/radius/colour are its params, not a separately
// defined concept. Lengths a pack declares are LOGICAL px; multiply by
// uSurfaceScale to reach the device-px space the geometry uniforms use.

#ifndef PLASMAZONES_SURFACE_UNIFORMS_GLSL
#define PLASMAZONES_SURFACE_UNIFORMS_GLSL

#ifdef PLASMAZONES_KWIN

// ── Compositor branch — classic default-block uniforms ──────────────────────
// uTexture0 is bound to texture unit 0 by KWin's OffscreenData::paint; a
// sampler2D left unset defaults to unit 0, so no explicit binding is needed.
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
// 1.0 when the surface is focused/active, else 0.0. A pack with active/inactive
// colour params mixes them on this rather than the host picking one.
uniform float uSurfaceFocused;

// Continuously-increasing seconds, for ANIMATED packs (pulsing glow, shimmer,
// …) — the same role iTime plays in the overlay / animation categories. The
// host captures an epoch at first use so this begins near 0 (float precision).
// The linker drops it for a static pack (e.g. the border); a window whose packs
// never reference iTime is not driven to repaint, so static decoration is free.
uniform float iTime;

// Audio spectrum bar count (CAVA), 0 when the audio visualizer is off. Both
// runtimes populate it: the daemon writes the UBO member for its OSD / popup
// surfaces, and the KWin effect pushes it from its own CAVA provider for window
// decorations. surface_audio.glsl's helpers no-op while it is 0. The
// uAudioSpectrum sampler is declared in surface_audio.glsl, which packs #include
// to opt in.
uniform int iAudioSpectrumSize;

// Pack-specific tweakable parameters (declared in metadata.json, addressed by
// `#define p_<id> customParamsN_x` / `customColorN` preambles the registry
// generates — identical to the animation/overlay categories).
uniform vec4 customParams[8];
uniform vec4 customColors[16];

// Multipass buffer-pass output SIZES: iChannelResolution[N].xy is the pixel
// size of iChannelN. The iChannelN samplers themselves live in the opt-in
// surface_multipass.glsl module — a single-pass pack (the border) declares
// neither. This resolution array stays in the core contract because it is a
// pinned std140 UBO member on the daemon.
uniform vec4 iChannelResolution[4];

// Backdrop capture GATE: 1.0 when the compositor captured the scene behind the
// window this frame (packs that declare `"needsBackdrop": true`), else 0.0, and
// always 0.0 on the daemon (no scene behind a surface). The capture sampler +
// backdropTexel() live in the opt-in surface_backdrop.glsl module; this gate
// stays here so a pack can branch on it without pulling in the sampler.
uniform float uHasBackdrop;

// The window's rule-resolved opacity (1.0 when no SetOpacity rule applies).
// The host applies a KWin-style FINAL modulation by default, so most packs
// never read this; a pack that declares `"handlesOpacity": true` suppresses
// that and applies this value itself (frost dims only its content sample so
// the frost slab stays solid).
uniform float uSurfaceOpacity;

// Cursor position for hover-reactive packs. .xy is the cursor in the SAME
// top-down device-px space as the geometry uniforms above (origin at the
// padded canvas's top-left), (-1, -1) when the cursor is outside the canvas.
// .zw is .xy normalized by uSurfaceSize (negative while .xy carries the
// sentinel), so `iMouse.x < 0.0` is the canonical off-surface test on both
// runtimes. A pack that reads iMouse should also declare `"animated": true`
// in its metadata — the host repaints on its vsync loop while a pack
// animates, and there is no per-cursor-move damage path for static packs.
uniform vec4 iMouse;

// User-declared image textures (metadata `textures` — logo, mask, pattern).
// Bound to dedicated units at draw time; iTextureResolution[N].xy carries
// each bound texture's pixel size (slot N feeds uTexture<N+1>, mirroring the
// animation contract's slot layout). A slot with no loadable file reads
// transparent black.
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
    float uHasBackdrop;          // offset 104 (4) — always 0 (no scene behind daemon surfaces)
    float uSurfaceOpacity;       // offset 108 (4) — always 1 on the daemon (qt_Opacity carries host opacity)
    vec4 customParams[8];        // offset 112 (128)
    vec4 customColors[16];       // offset 240 (256)
    vec4 iChannelResolution[4];  // offset 496 (64) — multipass buffer sizes (.xy)
    int iAudioSpectrumSize;      // offset 560 (4) — CAVA bar count (0 = audio off)
    // implicit 12-byte std140 pad here — vec4 iMouse below is 16-aligned.
    vec4 iMouse;                 // offset 576 (16) — cursor in the surface texture's
                                 //   top-down device-px space (.xy; negative when
                                 //   off-surface / no hover source), .zw = .xy
                                 //   normalized by uSurfaceSize
    vec4 iTextureResolution[4];  // offset 592 (64) — user texture sizes (.xy;
                                 //   slot N feeds uTexture<N+1>)
};                               // total 656 bytes, no trailing pad

layout(binding = 7) uniform sampler2D uTexture0;
// User-declared image textures (metadata `textures`), bindings 8-10 — the
// same sampler-name and binding-point dialect the animation and overlay
// categories use, provided by the base ShaderEffect's user-texture plumbing.
layout(binding = 8) uniform sampler2D uTexture1;
layout(binding = 9) uniform sampler2D uTexture2;
layout(binding = 10) uniform sampler2D uTexture3;

// The multipass iChannel sampler bindings (2-5) live in surface_multipass.glsl,
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
