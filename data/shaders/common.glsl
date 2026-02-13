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
    vec4 customParams[4];
    vec4 customColors[8];
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
    vec2 iChannelResolution[4];
    int iAudioSpectrumSize;  // number of bars; 0 = disabled
};

layout(binding = 1) uniform sampler2D uZoneLabels;


const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// Texture coords to screen coords (Y=0 at top). Uses iResolution.
vec2 fragCoordFromTexCoord(vec2 vTexCoord) {
    return vec2(vTexCoord.x, 1.0 - vTexCoord.y) * iResolution;
}

// Clamp color and apply qt_Opacity for final output.
vec4 clampFragColor(vec4 color) {
    return vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
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

// Pseudo-random: 1D in → 1D out (float → float)
float hash11(float n) {
    return fract(sin(n) * 43758.5453123);
}

// Pseudo-random: 2D in → 1D out (vec2 → float)
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Pseudo-random: 2D in → 2D out (vec2 → vec2, e.g. for particle positions)
// Must use different expressions for x and y; p.x*p.y == p.y*p.x would yield diagonal-only output
vec2 hash22(vec2 p) {
    return fract(sin(vec2(
        dot(p, vec2(127.1, 311.7)),
        dot(p, vec2(269.5, 183.3))
    )) * 43758.5453);
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
    return exp(-d / falloff) * strength;
}

// Color with fallback when unset (length < 0.01)
vec3 colorWithFallback(vec3 color, vec3 fallback) {
    return length(color) >= 0.01 ? color : fallback;
}

#endif // PLASMAZONES_COMMON_GLSL
