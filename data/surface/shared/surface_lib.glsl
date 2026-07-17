// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared geometry / composite / focus helpers for SURFACE shader packs — the
// decoration-category cousin of the overlay category's data/overlays/shared/
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

// The border family's shared band assembly: the OUTER-radius rounded-rect SDF
// (content radius + border width, both logical px scaled to device px by
// uSurfaceScale), the content-clip mask, and the band edge. `p` is the
// device-px fragment (surfacePixel); `borderWidth` / `cornerRadius` are the
// pack's logical-px params (pack macros the shared code can't name, so they are
// passed in). Packs whose band geometry differs (border-double's three-width
// stack) build their own.
//
// The `aa` feather is the SDF edge softness in DEVICE px (kept unscaled so the
// anti-alias width stays ~constant across output scales). The historical
// family value is 0.7 px — a soft, sub-pixel band. The three-arg form keeps
// that default so every existing caller (window border included) is unchanged;
// the four-arg form lets a pack expose it as a parameter and pass a smaller
// value (~0.5) for a crisper, more grid-hinted 1px hairline.
struct BorderBand {
    FrameSDF fs;
    float insideMask;
    float edge;
};
BorderBand standardBorderBand(vec2 p, float borderWidth, float cornerRadius, float aa) {
    float width = borderWidth * uSurfaceScale;
    float radius = (cornerRadius + borderWidth) * uSurfaceScale;
    // A zero feather makes both smoothstep() edges equal, which is undefined in
    // GLSL (NaN / garbage on the boundary fragment). Floor it at a hair so an
    // aggressively crisp (or hand-edited) value degrades to a near-hard edge
    // rather than misrendering.
    float feather = max(aa, 1e-3);
    BorderBand b;
    b.fs = frameSdf(p, radius);
    b.insideMask = 1.0 - smoothstep(-feather, feather, b.fs.d);
    b.edge = smoothstep(-width - feather, -width + feather, b.fs.d);
    return b;
}
BorderBand standardBorderBand(vec2 p, float borderWidth, float cornerRadius) {
    return standardBorderBand(p, borderWidth, cornerRadius, 0.7);
}

// Backdrop-slab family shared open (blur / duotone / frosted-glass / glass /
// rain-glass / rippled-glass / mosaic): the raw window content sample, the
// device-px fragment, the frame SDF at the pack's corner radius, and the AA
// slab mask — the four lines every backdrop-slab pack repeats before its
// pack-specific pane. Content dimming is a per-pack concern now: a pack that
// wants a fadeable window sample declares its own parameter (frost/glass
// `contentOpacity`) and multiplies `window` itself, so its knob lives in its
// param editor instead of riding the retired SetOpacity rule feed.
// `cornerRadiusPx` is the pack's p_cornerRadius already scaled to device px.
struct SurfaceSlab {
    vec4 window;
    vec2 px;
    FrameSDF fs;
    float mask;
};
SurfaceSlab surfaceSlabOpen(vec2 uv, float cornerRadiusPx) {
    SurfaceSlab s;
    s.window = surfaceTexel(uv);
    s.px = surfacePixel(uv);
    s.fs = frameSdf(s.px, cornerRadiusPx);
    s.mask = frameMask(s.fs.d);
    return s;
}

// The backdrop-slab family's shared no-backdrop fallback (daemon hosts, where
// uHasBackdrop is 0): a faint premultiplied tint slab at 0.35 * tintStrength,
// clipped to the slab `mask`. Only packs that use exactly this constant (blur /
// glass / rippled-glass) call it; siblings with a different fallback keep theirs.
vec4 faintTintSlab(vec3 tint, float tintStrength, float mask) {
    return vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
}

// Glow/shadow outer-margin falloff (the ~12 lines glow and shadow shared): the
// exp(-4t²) reach profile from SDF distance `d`, feathered to zero just inside
// the texture edge so a thin capture margin fades out instead of clipping in a
// hard rectangle, confined to the transparent margin (1 - baseAlpha), and
// scaled by `strength` and the focus dim at floor `focusFloor`. `edgePx` is the
// REAL (undisplaced) fragment position for the edge feather — the shadow pack
// evaluates `d` against a displaced frame but feathers on the true position.
float haloFalloff(float d, float reach, vec2 edgePx, float baseAlpha, float strength, float focusFloor) {
    float t = max(d, 0.0) / reach;
    float halo = exp(-4.0 * t * t);
    float edgeDist = min(min(edgePx.x, edgePx.y), min(uSurfaceSize.x - edgePx.x, uSurfaceSize.y - edgePx.y));
    halo *= smoothstep(0.0, min(0.35 * reach, 12.0 * max(uSurfaceScale, 0.001)), edgeDist);
    halo *= (1.0 - baseAlpha);
    halo *= strength * focusDim(focusFloor);
    return halo;
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
