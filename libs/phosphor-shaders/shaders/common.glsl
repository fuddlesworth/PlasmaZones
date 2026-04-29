// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PhosphorShaders shared shader helpers (GLSL #version 450).
// Declares the base uniform block (672 bytes) and generic utilities.
// Consumer-specific extensions (e.g. zone arrays) are declared in the
// consumer's own common.glsl which #includes this file or redeclares
// a larger UBO block.
//
// Include from effect.frag or vertex shader with:
//   #include <common.glsl>
//
// Bindings: 0 = UBO. Channels (2-5) in multipass.glsl.

#ifndef PHOSPHORSHELL_COMMON_GLSL
#define PHOSPHORSHELL_COMMON_GLSL

layout(std140, binding = 0) uniform ShaderUniforms {
    // ── PhosphorShaders::BaseUniforms (672 bytes) ──────────────────────
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;            // wrapped lo part, always in [0, kShaderTimeWrap). Safe to use directly.
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int appField0;          // consumer-defined (e.g. PlasmaZones: zoneCount)
    int appField1;          // consumer-defined (e.g. PlasmaZones: highlightedCount)
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1), Qt Y-down (Y=0 at top)
    vec4 iDate;         // xyzw = year, month, day, seconds since midnight
    vec4 customParams[8];
    vec4 customColors[16];
    vec2 iChannelResolution[4];
    int iAudioSpectrumSize;  // number of bars; 0 = disabled
    int iFlipBufferY;        // always 1; both OpenGL and Vulkan need Y-flip when sampling buffer textures
    // std140: 8 bytes implicit padding here (int+int=8 -> next vec2 array aligned to 16)
    vec2 iTextureResolution[4]; // user texture sizes (bindings 7-10); std140 pads each vec2 to 16 bytes
    float iTimeHi;       // integer wrap offset (changes once per kShaderTimeWrap seconds)
};

// Shader time-wrap period — must match kShaderTimeWrap in BaseUniforms.h
// iTime is wrapped into [0, K_TIME_WRAP); iTimeHi is the floor-offset. Without
// wrapping, float32 ULP would grow past one frame at ~36h uptime and kill any
// animation driven purely by iTime.
const float K_TIME_WRAP = 1024.0;

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

// Pseudo-random: 1D in -> 1D out (float -> float)
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

// Pseudo-random: 2D in -> 1D out (vec2 -> float)
// Bias by 0x80000000 so negative inputs (e.g. centered UV coordinates) don't
// collide with positive inputs near zero after int->uint truncation.
float hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(p)) + uvec2(2147483648u);
    q = q * uvec2(1597334673u, 3812015801u);
    uint h = (q.x ^ q.y) * 1103515245u + 12345u;
    h = ((h >> 16u) ^ h) * 2654435769u;
    h = (h >> 16u) ^ h;
    return float(h) / 4294967295.0;
}

// Pseudo-random: 2D in -> 2D out (vec2 -> vec2, e.g. for particle positions)
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

#endif // PHOSPHORSHELL_COMMON_GLSL
