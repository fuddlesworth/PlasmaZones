// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Separable-Gaussian buffer-pass helpers shared by every multipass surface
// pack whose buffer passes blur the backdrop (blur / duotone / frosted-glass /
// glass / rain-glass / rippled-glass). The two halves of a 9-tap separable
// Gaussian (sigma ~ radius/3), previously copy-pasted into each pack's
// buffer0.frag / buffer1.frag, live here once and are called by the shared
// gaussian_h.frag / gaussian_v.frag standard passes (builtin:gaussian-h/-v).
//
// BUFFER-PASS CONVENTION: buffer shaders compile WITHOUT the generated p_<id>
// parameter preamble (bakeBufferShaders), so parameters are read by their RAW
// contract slot. `blurRadius` is the first scalar parameter of every blur-family
// pack, so it lives in customParams[0].x (declaration-order auto-slotting; see
// buildParamPreamble). Offsets step in canvas UV: the logical-px radius is
// scaled to device px by uSurfaceScale, normalized by the canvas extent
// (uSurfaceSize), and spread over the kernel's 4-tap reach.

#ifndef PLASMAZONES_SURFACE_BLUR_GLSL
#define PLASMAZONES_SURFACE_BLUR_GLSL

#include <surface_uniforms.glsl>
// This kernel samples the backdrop (pass 0) and iChannel0 (pass 1), so it pulls
// in both opt-in modules — a buffer pass that includes surface_blur.glsl gets
// them transitively and needs no include of its own.
#include <surface_backdrop.glsl>
#include <surface_multipass.glsl>

// 9-tap Gaussian weights (sigma ~ radius/3), summing to ~1.
const float kSurfaceGaussW0 = 0.227027;
const float kSurfaceGaussW1 = 0.1945946;
const float kSurfaceGaussW2 = 0.1216216;
const float kSurfaceGaussW3 = 0.054054;
const float kSurfaceGaussW4 = 0.016216;

// Buffer pass 0: HORIZONTAL half over the BACKDROP capture (through
// backdropTexel(), which is transparent on a host that bound no backdrop).
// Rendered at the pack's bufferScale.
//
// The outermost taps reach up to 4 steps past uv, which near an edge lands
// outside this surface's slice of the backdrop. The two runtimes answer that
// differently, and both are right for what they hold: the compositor clamps
// into the captured rect, because it has no scene data outside it, while the
// daemon lets the sampler's clamp-to-edge handle it, because its backdrop is
// the whole desktop wallpaper and the neighbouring pixels are real.
vec4 surfaceGaussianBackdropH(vec2 uv) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 1.0);
    vec2 stepUv = vec2(radiusPx / (4.0 * max(uSurfaceSize.x, 1.0)), 0.0);
    vec4 sum = backdropTexel(uv) * kSurfaceGaussW0;
    sum += (backdropTexel(uv + stepUv) + backdropTexel(uv - stepUv)) * kSurfaceGaussW1;
    sum += (backdropTexel(uv + 2.0 * stepUv) + backdropTexel(uv - 2.0 * stepUv)) * kSurfaceGaussW2;
    sum += (backdropTexel(uv + 3.0 * stepUv) + backdropTexel(uv - 3.0 * stepUv)) * kSurfaceGaussW3;
    sum += (backdropTexel(uv + 4.0 * stepUv) + backdropTexel(uv - 4.0 * stepUv)) * kSurfaceGaussW4;
    return sum;
}

// Buffer pass 1: VERTICAL half over buffer 0's result (iChannel0, same
// bufferScale resolution). Together the two passes approximate a full 2D
// Gaussian; the main pass samples the result as iChannel1.
vec4 surfaceGaussianChannelV(vec2 uv) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 1.0);
    vec2 stepUv = vec2(0.0, radiusPx / (4.0 * max(uSurfaceSize.y, 1.0)));
    vec4 sum = texture(iChannel0, uv) * kSurfaceGaussW0;
    sum += (texture(iChannel0, uv + stepUv) + texture(iChannel0, uv - stepUv)) * kSurfaceGaussW1;
    sum += (texture(iChannel0, uv + 2.0 * stepUv) + texture(iChannel0, uv - 2.0 * stepUv)) * kSurfaceGaussW2;
    sum += (texture(iChannel0, uv + 3.0 * stepUv) + texture(iChannel0, uv - 3.0 * stepUv)) * kSurfaceGaussW3;
    sum += (texture(iChannel0, uv + 4.0 * stepUv) + texture(iChannel0, uv - 4.0 * stepUv)) * kSurfaceGaussW4;
    return sum;
}

// ── Dual Kawase pyramid ─────────────────────────────────────────────────────
//
// The blur family's standard chain since the eight-channel budget: four DOWN
// passes that halve the resolution each step from a quarter-res base, then
// three UP passes back to quarter res, declared in a pack as
//
//   "bufferShaders": ["builtin:kawase-down-0", "builtin:kawase-down-1",
//                     "builtin:kawase-down-2", "builtin:kawase-down-3",
//                     "builtin:kawase-up-0",   "builtin:kawase-up-1",
//                     "builtin:kawase-up-2"],
//   "bufferScales":  [0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25]
//
// and read by the main pass as iChannel6 (surfaceBlurTexel). Reach comes from
// pyramid DEPTH, not tap spacing: each level's taps sit a texel or so apart,
// and a level's texel is twice the size of the one above, so four levels cover
// a 256 px radius at offsets that never show Kawase's square ghosting. The
// radius picks how many levels are USED (surfaceKawaseDepth): the down passes
// always run (they are cheap, each a quarter the area of the last), and each
// up pass reads either the deeper up pass or the down level the pyramid
// bottoms out at, so a small radius is a shallow pyramid rather than a deep
// one with tiny offsets, which would blur far past the radius asked for.
//
// Every pass sizes its input with textureSize(), which is why the chain can
// run past the four iChannelResolution slots the UBO carries, and why it
// works at any global blur-scale multiplier. The one nominal constant is the
// base level's texel size, used only by the first pass: it reads the backdrop,
// whose size on the daemon is the whole wallpaper rather than this surface's
// slice, so its tap spacing cannot be derived from the sampler.

// Canvas px per texel of the chain's declared base level (bufferScales[0] =
// 0.25). Nominal: the global blur-scale multiplier moves the real density,
// which only widens or narrows the first pass's taps by that factor.
const float kSurfaceKawaseBaseTexel = 4.0;

// How many pyramid levels the radius asks for (1..4), and the tap offset that
// reaches the radius at that depth.
//
// Calibrated against KWin's blur effect (src/plugins/blur/blur.cpp), the
// reference dual Kawase on this desktop. KWin runs 1..4 iterations from a
// half-res base and, per iteration, walks its `offset` (in read-texture half
// pixels) through a fixed band before adding the next level, and expands
// repaints by a fixed reach at each level:
//
//   iteration  texel  offset band  reach (px)
//   1          2 px   1.0 .. 2.0    10
//   2          4 px   2.0 .. 3.0    20
//   3          8 px   2.0 .. 5.0    50
//   4         16 px   3.0 .. 8.0   150
//
// Our pyramid starts one level deeper (a quarter-res base, so 4 / 8 / 16 /
// 32 px texels) and its taps are in whole source texels, (o + 0.5) of them,
// where KWin's are offset / 2. So each of our depths takes the band and
// reach of the KWin iteration with the same texel size, the fourth
// extrapolated, and o = offset / 2 - 0.5 converts the band. The radius picks
// the shallowest depth whose reach covers it and interpolates the offset
// through that depth's band, which is why a growing radius widens the taps
// first and only then adds a level, the way KWin's strength slider does.
// Hyprland's blur was checked too: it keeps one screen-space tap at every
// level (size x passes), which reaches further per pass but shows the
// square Kawase footprint sooner, so KWin's banded model is the one used.
// The reaches sit at three quarters of KWin's per-level figures. Measured on
// the compositor against 40 px stripes, KWin's own values left a 24 px radius
// at 94% of the unblurred contrast and 48 px at 68%, where a Gaussian of
// sigma = radius / 3 gives 82% and 45%; the shorter reaches move each depth's
// band down to where the radius reads as the width it names, and 64 px and
// beyond were already on the curve.
const vec4 kSurfaceKawaseReach = vec4(15.0, 40.0, 120.0, 320.0);
const vec4 kSurfaceKawaseOffsetMin = vec4(1.0, 2.0, 3.0, 3.0);
const vec4 kSurfaceKawaseOffsetMax = vec4(3.0, 5.0, 8.0, 8.0);

int surfaceKawaseDepth() {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 0.0);
    if (radiusPx <= kSurfaceKawaseReach.x) return 1;
    if (radiusPx <= kSurfaceKawaseReach.y) return 2;
    if (radiusPx <= kSurfaceKawaseReach.z) return 3;
    return 4;
}
float surfaceKawaseOffset(int depth) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 0.0);
    int i = clamp(depth, 1, 4) - 1;
    float lo = i == 0 ? 0.0 : kSurfaceKawaseReach[i - 1];
    float hi = kSurfaceKawaseReach[i];
    float t = clamp((radiusPx - lo) / max(hi - lo, 1.0), 0.0, 1.0);
    float kwinOffset = mix(kSurfaceKawaseOffsetMin[i], kSurfaceKawaseOffsetMax[i], t);
    return max(kwinOffset * 0.5 - 0.5, 0.0);
}

// DOWN tap over @p src (the level above, twice this pass's resolution): the
// centre weighted 4 plus the four diagonals at (offset + 0.5) source texels,
// over 8. Premultiplied in, premultiplied out.
vec4 surfaceKawaseDown(sampler2D src, vec2 uv, float offset) {
    vec2 d = (offset + 0.5) / vec2(textureSize(src, 0));
    vec4 sum = texture(src, uv) * 4.0;
    sum += texture(src, uv - d);
    sum += texture(src, uv + d);
    sum += texture(src, uv + vec2(d.x, -d.y));
    sum += texture(src, uv - vec2(d.x, -d.y));
    return sum / 8.0;
}

// The first DOWN pass reads the backdrop capture through backdropTexel(),
// which clamps into the capture's valid rect on the compositor and into this
// surface's slice of the wallpaper on the daemon. Its offsets are in canvas
// space at the nominal base texel, since the backdrop's own size is not the
// canvas's.
vec4 surfaceKawaseDownBackdrop(vec2 uv, float offset) {
    vec2 d = (offset + 0.5) * kSurfaceKawaseBaseTexel * 0.5 / max(uSurfaceSize, vec2(1.0));
    vec4 sum = backdropTexel(uv) * 4.0;
    sum += backdropTexel(uv - d);
    sum += backdropTexel(uv + d);
    sum += backdropTexel(uv + vec2(d.x, -d.y));
    sum += backdropTexel(uv - vec2(d.x, -d.y));
    return sum / 8.0;
}

// UP tap over @p src (the level below, half this pass's resolution): the
// standard eight-tap diamond, over 12.
vec4 surfaceKawaseUp(sampler2D src, vec2 uv, float offset) {
    vec2 d = (offset + 0.5) * 0.5 / vec2(textureSize(src, 0));
    vec4 sum = texture(src, uv + vec2(-d.x * 2.0, 0.0));
    sum += texture(src, uv + vec2(-d.x, d.y)) * 2.0;
    sum += texture(src, uv + vec2(0.0, d.y * 2.0));
    sum += texture(src, uv + vec2(d.x, d.y)) * 2.0;
    sum += texture(src, uv + vec2(d.x * 2.0, 0.0));
    sum += texture(src, uv + vec2(d.x, -d.y)) * 2.0;
    sum += texture(src, uv + vec2(0.0, -d.y * 2.0));
    sum += texture(src, uv + vec2(-d.x, -d.y)) * 2.0;
    return sum / 12.0;
}

// The chain's result, for a main pass: the last UP pass, at the base level.
vec4 surfaceBlurTexel(vec2 uv) {
    return texture(iChannel6, uv);
}

#endif // PLASMAZONES_SURFACE_BLUR_GLSL
