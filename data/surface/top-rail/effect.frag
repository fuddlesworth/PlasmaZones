// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Top-rail surface shader. The native KDecoration still owns the title text,
// buttons and hit testing; this pack owns the focus-aware accent pixels at the
// top of the captured frame.

#include <surface_lib.glsl>

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);
    if (surfaceFrameDegenerate()) {
        return tex;
    }

    vec2 px = surfacePixel(uv);
    vec2 local = px - uSurfaceFrameTopLeft;
    float scale = max(uSurfaceScale, 0.001);
    float aa = max(p_edgeSoftness * scale, 0.1);

    float frameWidth = max(uSurfaceFrameSize.x, 1.0);
    float radius = max(p_cornerRadius * scale, 0.0);
    float inset = max(p_railInset * scale, 0.0);
    float left = clamp(radius + inset, 0.0, frameWidth);
    float right = clamp(frameWidth - radius - inset, 0.0, frameWidth);
    float span = max(right - left, 0.001);

    // The native rail starts at the frame's top edge. Keep a small AA band on
    // both sides so a one-pixel rail remains stable at fractional output scale.
    float xMask = smoothstep(left - aa, left + aa, local.x)
        * (1.0 - smoothstep(right - aa, right + aa, local.x));
    float height = mix(float(p_inactiveHeight), float(p_activeHeight), clamp(uSurfaceFocused, 0.0, 1.0))
        * scale;
    float yMask = smoothstep(-aa, aa, local.y)
        * (1.0 - smoothstep(height - aa, height + aa, local.y));
    float coverage = clamp(xMask * yMask, 0.0, 1.0);
    if (coverage <= 0.0) {
        return tex;
    }

    float t = clamp((local.x - left) / span, 0.0, 1.0);
    // A triangular centre weight reproduces the native accent → text → accent
    // gradient while leaving unfocused rails a single accent colour.
    float centreWeight = 1.0 - abs(2.0 * t - 1.0);
    float focus = clamp(uSurfaceFocused, 0.0, 1.0);
    vec4 edgeColor = mix(p_inactiveColor, p_activeColor, focus);
    vec4 centreColor = mix(p_inactiveColor, p_activeCenterColor, focus);
    vec4 rail = mix(edgeColor, centreColor, centreWeight);

    float alpha = coverage * clamp(rail.a, 0.0, 1.0);
    return vec4(rail.rgb * alpha, alpha) + tex * (1.0 - alpha);
}
