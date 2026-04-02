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
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1), Qt Y-down (Y=0 at top)
    vec4 iDate;         // xyzw = year, month, day, seconds since midnight
    vec4 customParams[8];
    vec4 customColors[16];
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
    vec2 iChannelResolution[4];
    int iAudioSpectrumSize;  // number of bars; 0 = disabled
    int iFlipBufferY;        // 1 = OpenGL (buffer textures need Y-flip when sampling)
    vec2 iTextureResolution[4]; // user texture sizes (bindings 7-10); std140 pads each vec2 to 16 bytes
};

layout(binding = 1) uniform sampler2D uZoneLabels;


const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// Resolution-independent pixel scale factor. Normalizes pixel-space values
// (border width, glow radius, chromatic aberration, etc.) so they occupy the
// same fraction of the screen at any resolution. Reference: 1080p.
// Usage: multiply hardcoded pixel thresholds by pxScale().
float pxScale() { return max(iResolution.y, 1.0) / 1080.0; }

// Compute fragment coordinates from texture coords.
// OpenGL framebuffers are Y-up, Vulkan framebuffers are Y-down.
// iFlipBufferY is 1 for OpenGL, 0 for Vulkan — use it to flip only when needed.
vec2 fragCoordFromTexCoord(vec2 uv) {
    float y = (iFlipBufferY != 0) ? (1.0 - uv.y) : uv.y;
    return vec2(uv.x, y) * iResolution;
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

// Composite pre-rendered zone labels over the current color (premult alpha over).
// labelsTex: sampler at binding 1; uv: fragCoord / max(iResolution, vec2(0.0001)); no Y flip
vec4 compositeLabels(vec4 color, vec2 uv, sampler2D labelsTex) {
    vec4 labels = texture(labelsTex, uv);
    color.rgb = color.rgb * (1.0 - labels.a) + labels.rgb;
    color.a = labels.a + color.a * (1.0 - labels.a);
    return color;
}

// UV for labels texture sampling. Use with compositeLabels(color, labelsUv(fragCoord), labelsTex).
vec2 labelsUv(vec2 fragCoord) {
    vec2 res = max(iResolution, vec2(0.0001));
    return fragCoord / res;
}

// Composite labels at fragCoord using uZoneLabels (binding 1)
vec4 compositeLabelsWithUv(vec4 color, vec2 fragCoord) {
    return compositeLabels(color, labelsUv(fragCoord), uZoneLabels);
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
