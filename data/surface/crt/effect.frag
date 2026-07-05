// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CRT pack, main pass: the Gaussian-blurred backdrop (buffer 1) displayed
// as an old tube monitor — barrel curvature warps the sample coordinate
// in frame space, horizontal scanlines and a vertical RGB phosphor mask
// modulate the picture, a slow bright band rolls upward, a vignette
// darkens the corners, and an optional phosphor tint pushes the whole
// picture toward the classic green or amber terminal look. Same slab
// composite as the blur family.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the tube behind.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pack renders a dark tube slab with the same scanlines, roll and
// vignette over the tint colour.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// px-space (top-down) vector -> canvas UV offset (vTexCoord is Y-up).
vec2 pxToUv(vec2 v) {
    return vec2(v.x, -v.y) / max(uSurfaceSize, vec2(1.0));
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, min(halfSz.x, halfSz.y));
    float d = sdRoundedBox(px - center, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    // Barrel distortion in frame space: c is the frame-centred coordinate in
    // [-1, 1]; pushing samples outward by dot(c, c) bulges the picture. The
    // warped point is converted back to a canvas UV via the px offset.
    vec2 fuv = (px - uSurfaceFrameTopLeft) / max(uSurfaceFrameSize, vec2(1.0));
    vec2 c = fuv * 2.0 - 1.0;
    vec2 warped = c * (1.0 + clamp(p_curvature, 0.0, 0.5) * dot(c, c));
    vec2 warpedPx = ((warped * 0.5 + 0.5) * uSurfaceFrameSize) + uSurfaceFrameTopLeft;
    vec2 uv = vTexCoord + pxToUv(warpedPx - px);

    vec3 rgb;
    float paneA;
    if (uHasBackdrop >= 0.5) {
        vec4 blurred = texture(iChannel1, clamp(uv, 0.0, 1.0));
        rgb = blurred.rgb;
        paneA = blurred.a;
    } else {
        // Original pseudo look for daemon surfaces: a dark powered-on tube in
        // the phosphor colour; the scanline/roll/vignette treatment below
        // still applies, so the slab reads as a CRT rather than flat tint.
        rgb = p_tintColor.rgb * 0.18;
        paneA = 1.0;
    }

    // Phosphor tint: map picture luminance through the tint colour. At 1.0
    // the picture is a monochrome terminal; small values just haze it.
    float luma = paneA > 0.001 ? dot(rgb / paneA, vec3(0.2126, 0.7152, 0.0722)) : 0.0;
    rgb = mix(rgb, p_tintColor.rgb * luma * 1.1 * paneA, clamp(p_tintStrength, 0.0, 1.0));

    // Scanlines every ~2 device px, phosphor stripes every 3, both softened
    // so they read as texture rather than aliasing at fractional scales.
    float scan = 1.0 - clamp(p_scanlines, 0.0, 1.0) * (0.5 + 0.5 * sin(px.y * kTau / (2.0 * max(uSurfaceScale, 0.5))));
    float stripe = mod(floor(px.x / max(uSurfaceScale, 0.5)), 3.0);
    vec3 phosphor = mix(vec3(1.0),
                        vec3(stripe == 0.0 ? 1.2 : 0.9, stripe == 1.0 ? 1.2 : 0.9, stripe == 2.0 ? 1.2 : 0.9),
                        clamp(p_maskStrength, 0.0, 1.0));

    // Rolling band: a soft bright stripe sweeping bottom-to-top, the
    // signature of a filmed CRT. rollSpeed 0 parks it off-screen.
    float roll = 0.0;
    if (p_rollSpeed > 0.001) {
        float band = fract(fuv.y + iTime * p_rollSpeed);
        roll = 0.08 * smoothstep(0.0, 0.35, band) * (1.0 - smoothstep(0.35, 0.7, band));
    }

    // Corner vignette in frame space.
    float vig = 1.0 - 0.35 * pow(length(c) * 0.7071, 3.0);

    rgb = (rgb * scan * phosphor + roll * paneA * (0.5 + 0.5 * p_tintColor.rgb)) * vig;
    vec4 pane = vec4(rgb, paneA) * mask;

    fragColor = window + pane * (1.0 - window.a);
}
