// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared geometry / composite / focus helpers for SURFACE shader packs — the
// decoration-category cousin of the overlay category's data/shaders/shared/
// common.glsl. Pulls in the uniform contract (surface_uniforms.glsl) and layers
// the small idioms every decoration pack was re-deriving inline: the
// rounded-rect SDF, the frame-geometry setup, the AA slab mask, the focus dim,
// and the three composite forms (border band, slab over window, additive
// margin). Colour-space and noise helpers live in the opt-in modules
// surface_color.glsl / surface_noise.glsl.
//
// Runtime-agnostic: every helper reads only the contract uniforms, which are
// global in both the compositor (default-block) and daemon (UBO) branches.

#ifndef PLASMAZONES_SURFACE_LIB_GLSL
#define PLASMAZONES_SURFACE_LIB_GLSL

#include <surface_uniforms.glsl>

const float TAU = 6.28318530718;

// Rounded-box signed distance (iq). `p` is box-centred, `b` half-extents, `r`
// the corner radius. Negative inside, positive outside.
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// True before a host has wired a real frame rect (uSurfaceFrameSize == 0). The
// SDF would otherwise collapse to "edge everywhere"; packs pass content through
// untouched in this state.
bool surfaceFrameDegenerate() {
    return uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0;
}

// Frame geometry + signed distance for a fragment at device-px `p`, with corner
// radius `radiusPx` (device px) clamped to half the smaller side. One call
// replaces the centre / half-size / radius-clamp / SDF idiom every decoration
// pack repeated. Always clamps the radius (blur previously did not — a
// pathological radius on a tiny frame is now clamped like every sibling).
struct FrameSDF {
    vec2 center;
    vec2 halfSize;
    float radius;
    float d;
};
FrameSDF frameSdf(vec2 p, float radiusPx) {
    FrameSDF fs;
    fs.halfSize = 0.5 * uSurfaceFrameSize;
    fs.center = uSurfaceFrameTopLeft + fs.halfSize;
    fs.radius = clamp(radiusPx, 0.0, min(fs.halfSize.x, fs.halfSize.y));
    fs.d = sdRoundedBox(p - fs.center, fs.halfSize, fs.radius);
    return fs;
}

// Slab AA coverage from an SDF distance (±1 px feather). Border packs use a
// tighter ±0.7 band and pass their own width, so this is the slab form only.
float frameMask(float d) {
    return 1.0 - smoothstep(-1.0, 1.0, d);
}

// Focus dim: `lo` when unfocused, ramping to 1.0 focused, cross-faded on the
// contract's uSurfaceFocused. The unfocused floor is the caller's (packs pick
// 0.30 / 0.55 / 0.65 deliberately).
float focusDim(float lo) {
    return mix(lo, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));
}

// Border-band composite: lay a premultiplied `col` band (coverage
// `edge * insideMask * col.a`) over `tex`, over transparency.
vec4 borderComposite(vec4 tex, vec4 col, float edge, float insideMask) {
    float ba = edge * insideMask * col.a;
    vec4 contentPx = tex * (1.0 - edge);
    return vec4(col.rgb * ba, ba) + contentPx * (1.0 - ba);
}

// Slab-over-window composite: `pane` over the (already opacity-dimmed) window.
vec4 slabComposite(vec4 window, vec4 pane) {
    return window + pane * (1.0 - window.a);
}

// Additive outer-margin composite (glow / shadow halo `col` at coverage `a`
// over `base`).
vec4 marginComposite(vec4 base, vec3 col, float a) {
    return vec4(base.rgb + col * a, clamp(base.a + a, 0.0, 1.0));
}

// Frame-normalized [0,1] UV for a device-px fragment.
vec2 frameUv(vec2 px) {
    return (px - uSurfaceFrameTopLeft) / max(uSurfaceFrameSize, vec2(1.0));
}

// px-space (top-down) vector -> canvas UV offset. vTexCoord is Y-up on the
// compositor, so this flips Y; used for backdrop refraction offsets.
vec2 pxToUv(vec2 v) {
    return vec2(v.x, -v.y) / max(uSurfaceSize, vec2(1.0));
}

// Normalized perimeter angle in [-0.5, 0.5) around the frame centre, aspect-
// corrected (divided by the half-extents) so dashes / hues stay uniform
// per-side on non-square frames.
float framePerimeter(vec2 p, vec2 center, vec2 halfSize) {
    vec2 rel = (p - center) / max(halfSize, vec2(1.0));
    return atan(rel.y, rel.x) / TAU;
}

#endif // PLASMAZONES_SURFACE_LIB_GLSL
