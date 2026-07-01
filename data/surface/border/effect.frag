// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Border surface shader — rounded corners + window border, the first surface
// pack. Width, corner radius and colours are this pack's own PARAMETERS (not a
// separate host-defined "decoration appearance"): p_borderWidth / p_cornerRadius
// (logical px, scaled to device px by uSurfaceScale) and p_activeColor /
// p_inactiveColor, mixed on the contract's uSurfaceFocused so the focused vs
// unfocused colour is the shader's job. (p_useSystemAccent is consumed host-side
// — when set, the effect fills the active/inactive colour params from the system
// scheme — so the shader just reads the colour params.)
//
// One analytic rounded-rect SDF over the content/frame rect both clips the
// content to the inner rounded rect and lays the border band over the
// background, so a translucent border blends with what is behind the surface.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Identity-decoration state: before a host wires real geometry the frame
    // rect is degenerate (uSurfaceFrameSize == 0). The SDF below would collapse
    // to "edge everywhere" and paint a border over the whole surface, so pass
    // the captured content through untouched until a real frame arrives.
    if (uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0) {
        fragColor = tex;
        return;
    }

    // Fragment's top-down device pixel; the content rect sits at
    // uSurfaceFrameTopLeft..+uSurfaceFrameSize (device px).
    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    // Pack params are logical px — scale to the device-px geometry space.
    float width = p_borderWidth * uSurfaceScale;
    // OUTER radius = content radius + width, so the band sits inside it and the
    // content corner ends one band-width in, at p_cornerRadius.
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(radius, 0.0, min(halfSz.x, halfSz.y));

    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    float edge = smoothstep(-width - aa, -width + aa, d);

    // Focus-mixed border colour (the shader picks active vs inactive).
    vec4 outlineColor = mix(p_inactiveColor, p_activeColor, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied. width <= 0 (no border in the chain's params) leaves the
    // content rounded with no band.
    float ba = edge * insideMask * outlineColor.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(outlineColor.rgb * ba, ba) + contentPx * (1.0 - ba);
}
