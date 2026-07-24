// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlasmaZones shared shader helpers (GLSL #version 450).
// Include from effect.frag or zone.vert with:
//   #include <common.glsl>   (from global shaders dir)
//   #include "common.glsl"   (from current shader dir if copied locally)
//
// Bindings 0-1: UBO and labels. Channels (2-5) in multipass.glsl.

#ifndef PLASMAZONES_COMMON_GLSL
#define PLASMAZONES_COMMON_GLSL

layout(std140, binding = 0) uniform ZoneUniforms {
    // ── Base uniforms (PhosphorShaders::BaseUniforms, 672 bytes) ───────
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;            // wrapped lo part, always in [0, kShaderTimeWrap). Safe to use directly.
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1), Qt Y-down (Y=0 at top)
    vec4 iDate;         // xyzw = year, month, day, seconds since midnight
    vec4 customParams[8];
    vec4 customColors[16];
    vec2 iChannelResolution[4];
    int iAudioSpectrumSize;  // number of bars; 0 = disabled
    int iFlipBufferY;        // always 1; both OpenGL and Vulkan need Y-flip when sampling buffer textures
    // std140: 8 bytes implicit padding here (int+int=8 → next vec2 array aligned to 16)
    vec2 iTextureResolution[4]; // user texture sizes (bindings 7-10); std140 pads each vec2 to 16 bytes
    float iTimeHi;       // integer wrap offset (changes once per kShaderTimeWrap seconds)
    // ── Zone extension (after BaseUniforms) ──────────────────────────
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
    // Logical-to-device scale: multiply a length expressed in LOGICAL px (the
    // units the settings UI and zoneParams use) by this to reach the device-px
    // space iResolution / vFragCoord / zoneRectPos are in. The overlay
    // counterpart of the decoration contract's uSurfaceScale
    // (data/surface/shared/surface_uniforms.glsl). std140 places this float
    // immediately after the zoneParams array; the block pads to 16 from here.
    float uZoneScale;
};

// Per-zone context handed to a `vec4 pZone(ZoneCtx z)` entry function (T1.4).
// When a pack defines pZone instead of main(), the harness generates the
// dispatch loop: for each visible zone it fills one ZoneCtx and accumulates the
// returned colors with blendOver(), then clampFragColor()s the result. The full
// zoneRects[]/iTime/audio globals stay readable inside pZone, so continuous-
// field and cross-zone effects remain expressible. Unused by packs that keep
// their own main().
struct ZoneCtx {
    int   index;         // zone i (0 .. zoneCount-1)
    vec2  fragCoord;     // screen-space pixel (the vFragCoord the loop passes in)
    vec4  rect;          // zoneRects[i]
    vec4  fillColor;     // zoneFillColors[i]
    vec4  borderColor;   // zoneBorderColors[i]
    vec4  params;        // zoneParams[i] (x=borderRadius, y=borderWidth, z=highlight flag, …)
                         // x/y are LOGICAL px — pass them through zoneSdf() /
                         // zoneBorderWidth() rather than using them raw, so they
                         // reach device px the way decoration packs do.
    bool  isHighlighted; // zoneParams[i].z > 0.5
};

// Shader time-wrap period — must match kShaderTimeWrap in zoneshadercommon.h
// iTime is wrapped into [0, K_TIME_WRAP); iTimeHi is the floor-offset. Without
// wrapping, float32 ULP would grow past one frame at ~36h uptime and kill any
// animation driven purely by iTime.
const float K_TIME_WRAP = 1024.0;

layout(binding = 1) uniform sampler2D uZoneLabels;


const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// ─── Time helpers (continuous phase across iTimeHi wrap) ────────────────────
// iTime wraps at K_TIME_WRAP seconds to preserve float32 precision. For usage
// patterns like `fbm(pos + time * speed)` or `fract(time * speed)` the wrap is
// visible once per period but harmless — just use iTime directly. For sin/cos
// phase animations that must not discontinuity, use the timeSin/timeCos
// helpers below: they compose phase from (iTimeHi, iTime) via angle-addition,
// with the large-phase term reduced mod TAU so float32 quantization of
// `speed * iTimeHi` never leaks into the result.
float timeSin(float speed) {
    float phaseHi = speed * iTimeHi;
    float phaseLo = speed * iTime;
    float phaseHiMod = phaseHi - floor(phaseHi / TAU) * TAU;
    return sin(phaseHiMod) * cos(phaseLo) + cos(phaseHiMod) * sin(phaseLo);
}
float timeSin(float speed, float offset) {
    float phaseHi = speed * iTimeHi + offset;
    float phaseLo = speed * iTime;
    float phaseHiMod = phaseHi - floor(phaseHi / TAU) * TAU;
    return sin(phaseHiMod) * cos(phaseLo) + cos(phaseHiMod) * sin(phaseLo);
}
float timeCos(float speed) {
    float phaseHi = speed * iTimeHi;
    float phaseLo = speed * iTime;
    float phaseHiMod = phaseHi - floor(phaseHi / TAU) * TAU;
    return cos(phaseHiMod) * cos(phaseLo) - sin(phaseHiMod) * sin(phaseLo);
}
float timeCos(float speed, float offset) {
    float phaseHi = speed * iTimeHi + offset;
    float phaseLo = speed * iTime;
    float phaseHiMod = phaseHi - floor(phaseHi / TAU) * TAU;
    return cos(phaseHiMod) * cos(phaseLo) - sin(phaseHiMod) * sin(phaseLo);
}

// Resolution-independent pixel scale factor. Normalizes pixel-space values
// (border width, glow radius, chromatic aberration, etc.) so they occupy the
// same fraction of the screen at any resolution. Reference: 1080p.
// Usage: multiply hardcoded pixel thresholds by pxScale().
float pxScale() { return max(iResolution.y, 1.0) / 1080.0; }

// Compute fragment coordinates from texture coords.
// Y is always flipped: both OpenGL (Y-up FBO) and Vulkan (negative-height viewport)
// store buffer data requiring a Y-flip when sampling. iFlipBufferY is always 1.
vec2 fragCoordFromTexCoord(vec2 uv) {
    return vec2(uv.x, 1.0 - uv.y) * iResolution;
}

// Clamp color, apply qt_Opacity, and premultiply alpha for final output.
// Qt Quick and Wayland compositors expect premultiplied alpha (rgb * alpha).
vec4 clampFragColor(vec4 color) {
    float a = clamp(color.a, 0.0, 1.0) * qt_Opacity;
    return vec4(clamp(color.rgb, 0.0, 1.0) * a, a);
}

// Zone rect helpers: rect.xy/zw are normalized 0-1; multiply by iResolution for pixels.
vec2 zoneRectPos(vec4 rect) { return rect.xy * iResolution; }
vec2 zoneRectSize(vec4 rect) { return rect.zw * iResolution; }

// Local UV within zone (0-1). Safe division; use for zone-relative sampling.
vec2 zoneLocalUV(vec2 fragCoord, vec2 rectPos, vec2 rectSize) {
    return (fragCoord - rectPos) / max(rectSize, vec2(0.001));
}

// Signed distance to rounded box (half-extents b, corner radius r)
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Zone frame geometry + rounded-rect signed distance — the overlay counterpart
// of the decoration side's frameSdf() (data/surface/shared/surface_lib.glsl).
// One call replaces the centre / half-size / radius / SDF idiom every overlay
// pack repeated inline, and fixes the three ways those inline copies diverged
// from the decoration packs:
//
//   * SCALE. `radiusLogical` is the pack's LOGICAL-px radius (zoneParams[i].x
//     is the user's configured value in logical px). It is multiplied by
//     uZoneScale to reach the device-px space the rect and fragCoord live in,
//     exactly as decoration packs multiply by uSurfaceScale. The inline copies
//     skipped this, so on a 2x display an overlay corner rounded half as hard
//     as a decoration corner set to the same number.
//   * CLAMP. The radius is clamped to half the smaller side. sdRoundedBox()
//     with r greater than a half-extent inverts the inset box and collapses
//     the zone to a sliver rather than rounding it. The settings UI caps the
//     radius at 50 logical px, so reaching this needs a zone under 100 logical
//     px on its smaller side, or a per-zone borderRadius from layout JSON,
//     which is unbounded. frameSdf() has always clamped; the inline copies
//     never did.
//   * NO FLOOR. The inline copies each applied their own `max(params.x, N)`
//     with N of 4, 6, or 8 depending on the pack, so a configured radius of 0
//     still rounded, by a different amount per pack. The configured value is
//     now honoured exactly, including 0 for square corners.
//
// `fragCoord` is the device-px fragment (ZoneCtx::fragCoord / vFragCoord),
// `rect` the normalised zoneRects[i].
struct ZoneSDF {
    vec2 center;     // zone centre, device px
    vec2 halfSize;   // zone half-extents, device px
    float radius;    // scaled + clamped corner radius, device px
    float d;         // signed distance; negative inside the zone
};
ZoneSDF zoneSdf(vec2 fragCoord, vec4 rect, float radiusLogical) {
    ZoneSDF z;
    z.halfSize = zoneRectSize(rect) * 0.5;
    z.center = zoneRectPos(rect) + z.halfSize;
    z.radius = clamp(radiusLogical * uZoneScale, 0.0, min(z.halfSize.x, z.halfSize.y));
    z.d = sdRoundedBox(fragCoord - z.center, z.halfSize, z.radius);
    return z;
}
// Border width in device px: the pack's logical-px width (zoneParams[i].y)
// scaled by uZoneScale, so a border keeps its physical thickness across
// output scales the same way a decoration pack's width does.
//
// A width the user actually asked for is floored at one device pixel, so a
// thin border never falls below a pixel and flickers out on a fractional
// scale. A width of exactly 0 is not a thin border, it is the user turning
// the border off, and it returns 0. Without that split the floor would do to
// the border what the per-pack `max(params.x, N)` floors did to the corner
// radius: make the configured value unreachable at one end of its range.
// borderWidthMin() is 0, so 0 is a legal setting and has to mean something.
float zoneBorderWidth(float widthLogical) {
    return widthLogical <= 0.0 ? 0.0 : max(widthLogical * uZoneScale, 1.0);
}

// Any other length a pack expresses in logical px, converted to the device-px
// space the SDF and fragCoord live in. Use this for glow radii, edge-fade
// distances, inner-glow falloffs, and every other hard-coded pixel constant
// that sits beside a zoneSdf() radius or a zoneBorderWidth().
//
// It matters that these agree. zoneSdf() and zoneBorderWidth() make the corner
// radius and the border track the display scale; a neighbouring constant left
// in raw device px does not, so on a 2x display the border doubles in physical
// thickness while its glow stays put and the proportions the pack was tuned at
// drift apart. Before those two helpers existed every length was uniformly
// wrong in the same direction, which hid the mismatch — making two of them
// right is what exposes it.
//
// Note this is NOT pxScale(). pxScale() is 1080p-relative and answers "what
// fraction of the screen is this?", which is the right question for a
// full-screen background pattern. zoneLen() answers "how big is this on the
// user's display?", which is the right question for anything measured against
// a zone edge.
float zoneLen(float logicalPx) {
    return logicalPx * uZoneScale;
}

// 2D rotation matrix. mat2 is column-major, so p * rot(a) rotates by +a,
// while rot(a) * p applies the transpose (rotation by -a). The *drift fbm
// keeps its historical matrix-first rot(a) * uv form; the pass-shader flow
// warps (e.g. nexus-cascade) use the p * rot(a) form.
mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c);
}

// Signed distance from point p to the segment a→b.
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Pseudo-random: integer-based hashes that produce identical results on
// SPIR-V (Vulkan) and GLSL (OpenGL). The sin()-based hashes produce different
// values on different shader compilers due to transcendental function precision
// and FMA fusion, causing visible artifacts in multipass/feedback rendering
// where sub-ULP differences accumulate across passes.

// Pseudo-random: 1D in → 1D out (float → float)
// All arithmetic is unsigned to avoid signed overflow (undefined on SPIR-V).
// Bias by 0x80000000 to map negative floats to distinct hash values near zero.
float hash11(float n) {
    uint h = uint(int(n)) + 2147483648u;
    h = h * 747796405u + 2891336453u;
    h = ((h >> 16u) ^ h) * 2654435769u;
    h = ((h >> 16u) ^ h) * 2654435769u;
    h = (h >> 16u) ^ h;
    return float(h) / 4294967295.0;
}

// Pseudo-random: 2D in → 1D out (vec2 → float)
// Bias by 0x80000000 so negative inputs (e.g. centered UV coordinates) don't
// collide with positive inputs near zero after int→uint truncation.
float hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(p)) + uvec2(2147483648u);
    q = q * uvec2(1597334673u, 3812015801u);
    uint h = (q.x ^ q.y) * 1103515245u + 12345u;
    h = ((h >> 16u) ^ h) * 2654435769u;
    h = (h >> 16u) ^ h;
    return float(h) / 4294967295.0;
}

// Pseudo-random: 2D in → 2D out (vec2 → vec2, e.g. for particle positions)
// Must use different expressions for x and y; p.x*p.y == p.y*p.x would yield diagonal-only output
vec2 hash22(vec2 p) {
    uvec2 q = uvec2(ivec2(p)) + uvec2(2147483648u);
    q = q * uvec2(1597334673u, 3812015801u);
    uint h1 = (q.x ^ q.y) * 1103515245u + 12345u;
    uint h2 = (q.x * 2654435769u) ^ (q.y * 2246822519u);
    h1 = ((h1 >> 16u) ^ h1) * 2654435769u;
    h2 = ((h2 >> 16u) ^ h2) * 2654435769u;
    return vec2(float(h1 >> 16u), float(h2 >> 16u)) / 65535.0;
}

// UV for sampling the zone-labels texture (uZoneLabels, binding 1). Packs sample
// it themselves and blend the premultiplied result over their color.
vec2 labelsUv(vec2 fragCoord) {
    vec2 res = max(iResolution, vec2(0.0001));
    return fragCoord / res;
}

// Premultiplied alpha over blend: result = src over dst
vec4 blendOver(vec4 dst, vec4 src) {
    float srcA = src.a;
    float dstA = dst.a;
    float outA = srcA + dstA * (1.0 - srcA);
    if (outA <= 0.0) return dst;
    vec3 outRgb = (src.rgb * srcA + dst.rgb * dstA * (1.0 - srcA)) / outA;
    return vec4(outRgb, outA);
}

// Soft border factor from SDF distance d (0 at edge, 1 inside border)
float softBorder(float d, float borderWidth) {
    float borderDist = abs(d);
    return 1.0 - smoothstep(0.0, borderWidth, borderDist);
}

// Exponential falloff glow (e.g. outer glow: d > 0 outside zone)
float expGlow(float d, float falloff, float strength) {
    return exp(-d / max(falloff, 0.001)) * strength;
}

// Color with fallback when unset (length < 0.01)
vec3 colorWithFallback(vec3 color, vec3 fallback) {
    return length(color) >= 0.01 ? color : fallback;
}

// Perceptual luminance (ITU-R BT.601)
float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// ─── Value noise ─────────────────────────────────────────────────────────────

// 1D value noise with hermite interpolation
float noise1D(float x) {
    float i = floor(x);
    float f = x - i; // explicit fract: guaranteed consistent with floor()
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

// 2D value noise with hermite interpolation
float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i; // explicit fract: guaranteed consistent with floor()
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Seamless angular noise: sample 2D noise on a circle to avoid atan() seam
float angularNoise(float angle, float freq, float seed) {
    vec2 circlePos = vec2(cos(angle), sin(angle)) * freq;
    return noise2D(circlePos + seed);
}

// Rotated fractal Brownian motion over value noise. gain is the per-octave
// amplitude decay; the gain-less overload uses the 0.55 shared by the drift
// packs. Loop caps at 8 octaves so a bad `octaves` can't run unbounded.
float fbm(vec2 uv, int octaves, float rotAngle, float gain) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 m = rot(rotAngle);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise2D(uv);
        uv = m * uv * 2.0 + vec2(180.0);
        amplitude *= gain;
    }
    return value;
}
float fbm(vec2 uv, int octaves, float rotAngle) {
    return fbm(uv, octaves, rotAngle, 0.55);
}

// ─── Palettes ────────────────────────────────────────────────────────────────

// Tri-stop hue cycle: three colors around a loop; fract(t) picks the position.
vec3 triStopPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else               return mix(accent, primary, (t - 0.66) * 3.0);
}

// Inigo Quilez cosine palette: a + b * cos(TAU * (c * t + d)).
// Currently single-consumer (cosmic-flow); prismata's copy was dead code.
vec3 iqPalette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

// ─── Highlight / vitality helpers ────────────────────────────────────────

// Standard vitality factor for highlight vs dormant state.
// Returns 1.0 when highlighted, 0.3 (subdued) otherwise.
float zoneVitality(bool isHighlighted) {
    return isHighlighted ? 1.0 : 0.3;
}

// Desaturate toward grayscale proportional to dormancy (1=full color, 0=gray).
vec3 vitalityDesaturate(vec3 col, float vitality) {
    float lum = luminance(col);
    return mix(vec3(lum), col, 0.4 + 0.6 * vitality);
}

// Interpolate a parameter between dormant and active values.
float vitalityScale(float dormant, float alive, float vitality) {
    return mix(dormant, alive, vitality);
}

#endif // PLASMAZONES_COMMON_GLSL
