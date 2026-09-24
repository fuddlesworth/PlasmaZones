// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Buffer-pass blur helpers shared by every multipass surface pack whose buffer
// passes blur the backdrop: blur, duotone, frosted-glass, glass, phosphor-glass,
// rain-glass and rippled-glass. All SEVEN declare the dual Kawase chain further
// down, which is what the blur family runs today.
//
// The separable-Gaussian pair immediately below is the older helper set, kept
// because the builtin:gaussian-h and builtin:gaussian-v passes call it and a
// third-party pack may still declare those. It is 9 taps spread over a 4-tap
// reach, so the outermost tap sits at the full radius and the effective sigma is
// roughly 0.45 of the radius, not the radius/3 this file used to claim.
//
// BUFFER-PASS CONVENTION: buffer shaders compile WITHOUT the generated p_<id>
// parameter preamble, so parameters are read by their RAW contract slot. THREE
// compile paths have to agree on that, not one: bakeBufferShaders on the daemon,
// the compositor's own buffer compile, and validateSurfacePack in the offline
// validator. Each skips the preamble deliberately, and a pack that referenced a
// p_<id> from a buffer pass would fail on every path it ships on. `blurRadius` is the first scalar parameter of every blur-family
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

// 9-tap Gaussian weights, summing to ~1. See the header for why the effective
// sigma is about 0.45 of the radius rather than a third of it.
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
//
// The tap reach is measured against the CANVAS, not the buffer: stepUv is
// radiusPx / (4 * uSurfaceSize.y), so the outermost tap sits a full
// radiusPx / uSurfaceSize.y away from uv. On a short canvas with a large
// radius that is well outside [0,1] — blurRadius 256 on a 400 px canvas puts
// it at 0.64 — and what happens out there is the buffer sampler's wrap mode.
// It is CLAMP on the compositor unconditionally: the buffer targets are
// created GL_LINEAR / GL_CLAMP_TO_EDGE and the `bufferWraps` and
// `bufferFilters` keys are daemon-only, which the fields themselves declare.
// So a big radius on a small surface smears the edge texel rather than
// blurring, identically on both hosts at the default wrap. Do NOT "fix" that
// by clamping uv here; that changes the Gaussian's edge behaviour everywhere.
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
// passes, the first PRODUCING the quarter-res base from the full-res backdrop
// and the other three halving from there, then three UP passes back to quarter
// res, declared in a pack as
//
//   "bufferShaders": ["builtin:kawase-down-0", "builtin:kawase-down-1",
//                     "builtin:kawase-down-2", "builtin:kawase-down-3",
//                     "builtin:kawase-up-0",   "builtin:kawase-up-1",
//                     "builtin:kawase-up-2"],
//   "bufferScales":  [0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25]
//
// and read by the main pass as iChannel6 (surfaceBlurTexel). Reach comes from
// pyramid DEPTH, not tap spacing: a level's taps sit between half a texel and
// four texels apart depending on where the radius falls in that level's band,
// and a level's texel is twice the size of the one above, so four levels cover
// the 256 px radius every blur-family pack declares as its blurRadius maximum,
// at offsets that never show Kawase's square ghosting. The
// radius picks how many levels are USED (surfaceKawaseDepth): the down passes
// always run (they are cheap, each a quarter the area of the last), and each
// up pass reads either the deeper up pass or the down level the pyramid
// bottoms out at, so a small radius is a shallow pyramid rather than a deep
// one with tiny offsets, which would blur far past the radius asked for.
//
// Every pass but the first sizes its input with textureSize(), which is why the
// chain can run past the four iChannelResolution slots the UBO carries, and why
// it still COMPOSES at any global blur-scale multiplier. It does not blur the
// same amount at every multiplier: a texel-relative offset over a texture the
// multiplier made larger or smaller covers proportionally more or less canvas,
// so lowering the multiplier softens and widens the result as well as making it
// cheaper. That is the setting's declared contract, not a defect.

// Canvas px per texel of the chain's declared base level (bufferScales[0] =
// 0.25).
//
// NOMINAL, and it is the ONE pass the blur-scale multiplier does not move. The
// first DOWN pass reads the backdrop, whose size on the daemon is the whole
// wallpaper rather than this surface's slice, so its tap spacing cannot come
// from textureSize() and is derived from this constant instead. Every OTHER
// pass sizes itself from its real input and therefore does follow the
// multiplier. (This comment used to say the opposite, naming the first pass as
// the only one the multiplier widens.)
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
//
// Two places where the correspondence is not exact, both deliberate:
//   • Depth 1's band is 1.0 .. 3.0, where the KWin iteration with the same 4 px
//     texel runs 2.0 .. 3.0. The floor is dropped so a small radius can still
//     reach below KWin's shortest offset instead of jumping straight to it.
//   • The bands and reaches here describe the DOWN passes. surfaceKawaseUp runs
//     at half this spacing (the extra 0.5 factor in its `d`), which is the
//     standard dual-Kawase asymmetry rather than a second calibration.
//
// The reaches sit at three quarters of KWin's figure for the first band and four
// fifths for the two after it (15 against 20, 40 against 50, 120 against 150,
// comparing each depth with the KWin iteration of the same texel size). Measured on
// the compositor against 40 px stripes, KWin's own values left a 24 px radius
// at 94% of the unblurred contrast and 48 px at 68%, where a Gaussian of
// sigma = radius / 3 gives 82% and 45%; the shorter reaches move each depth's
// band down to where the radius reads as the width it names, and 64 px and
// beyond were already on the curve.
// Only ONE of the three band boundaries is made continuous, and the other two are
// deliberate steps.
//
// 120 -> 121 IS continuous, and that is what the 3.6 floor below buys. The summed
// tap extent per (o + 0.5) is 14 at depth 2, 38 at depth 3 and 86 at depth 4, so a
// radius of 120 reaches 4.0 x 38 = 152 px. With a 3.0 floor a radius of 121 reached
// only 1.5125 x 86 = 130 px, i.e. asking for more blur gave LESS. Continuity needs
// a floor of at least 3.5349; 3.6 clears it and keeps the band monotonic to the top
// (320 px reaches 344).
//
// 40 -> 41 is left as a step, from 35 px to 58 px. Closing it would need a depth-3
// offset floor near 1.84, below the reference's own floor for a texel that size, so
// the step is the lesser evil.
//
// 15 -> 16 is left as a step for a different reason: depth 1 IS the quarter-res
// floor. Closing it would need an offset near 6.9 at quarter resolution, which is
// the square Kawase ghosting this calibration exists to avoid.
const vec4 kSurfaceKawaseReach = vec4(15.0, 40.0, 120.0, 320.0);
const vec4 kSurfaceKawaseOffsetMin = vec4(1.0, 2.0, 3.0, 3.6);
const vec4 kSurfaceKawaseOffsetMax = vec4(3.0, 5.0, 8.0, 8.0);

int surfaceKawaseDepth() {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 0.0);
    if (radiusPx <= kSurfaceKawaseReach.x) return 1;
    if (radiusPx <= kSurfaceKawaseReach.y) return 2;
    if (radiusPx <= kSurfaceKawaseReach.z) return 3;
    return 4;
}
// @p depth is ONE-BASED (1..4), matching surfaceKawaseDepth's return value, while
// the passes around it are named with zero-based indices (kawase_down_0..3,
// kawase_up_0..2). Passing a pass index straight in is therefore off by one, and
// the clamp below hides it rather than reporting it, so a caller that means
// "pass i" must pass i + 1.
float surfaceKawaseOffset(int depth) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 0.0);
    int i = clamp(depth, 1, 4) - 1;
    // max(i - 1, 0) rather than i - 1: the ternary makes the -1 index unreachable
    // in principle, but a negative constant-folded index is exactly the kind of
    // thing a driver compiler is free to reject while folding both arms.
    float lo = i == 0 ? 0.0 : kSurfaceKawaseReach[max(i - 1, 0)];
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
// space at HALF the nominal base texel, since it reads the full-res backdrop to
// produce the quarter-res base and therefore steps in the half-res level a
// quarter-res pass reads. The backdrop's own size is not the canvas's, which is
// why this spacing cannot come from textureSize().
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
//
// TRANSPARENT IS A LEGITIMATE ANSWER HERE AND uHasBackdrop DOES NOT PREDICT
// IT. When a pack's buffer allocation fails, the compositor logs that the pack
// renders single-pass, clears the buffers, and the main pass then binds the
// 1x1 transparent fallback to every declared channel, deliberately, so that an
// unbound sampler2D cannot read the running composite. uHasBackdrop is
// unaffected and still reads 1.0, because the backdrop CAPTURE succeeded; only
// the chain that consumes it did not run. A pack that branches on
// uHasBackdrop >= 0.5 and then samples the chain with no second gate therefore
// draws a fully transparent pane instead of reaching its own no-backdrop
// fallback. Every bundled blur-family pack is written that way today. A pack
// that wants to be robust should treat a fully transparent chain result as the
// no-backdrop case too, rather than trusting the flag alone.
vec4 surfaceBlurTexel(vec2 uv) {
    return texture(iChannel6, uv);
}

#endif // PLASMAZONES_SURFACE_BLUR_GLSL
