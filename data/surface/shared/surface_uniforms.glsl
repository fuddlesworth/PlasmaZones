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

// Pack-specific tweakable parameters (declared in metadata.json, addressed by
// `#define p_<id> customParamsN_x` / `customColorN` preambles the registry
// generates — identical to the animation/overlay categories).
uniform vec4 customParams[8];
uniform vec4 customColors[16];

// Multipass buffer-pass outputs. A multipass surface pack (one that declares
// `bufferShaders` in metadata.json) runs each buffer pass into an FBO; the
// pass output is bound here as iChannelN for downstream passes and for the main
// effect, exactly like the overlay/animation categories. Single-pass packs (the
// border) never reference these — the linker drops them. iChannelResolution[N].xy
// is the pixel size of iChannelN.
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform vec4 iChannelResolution[4];

// Backdrop capture — the scene BEHIND the window over the same (padded)
// canvas as uTexture0, captured by the compositor each frame for packs that
// declare `"needsBackdrop": true` (frost / glass). uBackdropRect is the
// VALID sub-rect of the capture in TOP-DOWN normalized coords (xy = min,
// zw = size): canvas parts that fall off the output are never blitted, so
// backdropTexel() clamps samples into this rect. uHasBackdrop is 1.0 when
// the capture exists this frame, else 0.0. Packs sample ONLY through
// backdropTexel() — the daemon branch declares no uBackdrop sampler, so a
// raw texture(uBackdrop, …) reference fails the daemon compile by design.
uniform sampler2D uBackdrop;
uniform vec4 uBackdropRect;
uniform float uHasBackdrop;

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
    // implicit 4-byte std140 pad (108 → 112) before the next vec4
    vec4 customParams[8];        // offset 112 (128)
    vec4 customColors[16];       // offset 240 (256)
    vec4 iChannelResolution[4];  // offset 496 (64) — multipass buffer sizes (.xy)
};                               // total 560 bytes

layout(binding = 7) uniform sampler2D uTexture0;

// Multipass buffer-pass outputs (bindings 2-5, matching the overlay category's
// shared/multipass.glsl convention so surface and overlay packs speak the same
// iChannel binding dialect). See the KWin branch above for semantics.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;

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

// The scene texel BEHIND the surface at `uv` (the same uv space surfaceTexel
// takes), clamped into the capture's valid sub-rect (uBackdropRect) so edge
// windows never smear the cleared off-output margin. Compositor-only: the
// daemon has no scene behind a surface, so this returns transparent there —
// gate styling on uHasBackdrop for an explicit fallback.
vec4 backdropTexel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    vec2 td = vec2(uv.x, 1.0 - uv.y); // top-down normalized, like surfacePixel
    td = clamp(td, uBackdropRect.xy, uBackdropRect.xy + uBackdropRect.zw);
    return texture(uBackdrop, vec2(td.x, 1.0 - td.y));
#else
    return vec4(0.0);
#endif
}

#endif // PLASMAZONES_SURFACE_UNIFORMS_GLSL
