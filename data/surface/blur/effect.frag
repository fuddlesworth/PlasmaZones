// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frost pack, main pass: composite the window's own pixels OVER the blurred
// backdrop (buffer 1), clipped to the frame rect with rounded corners. The
// frost shows through wherever the window content is translucent, which is
// how blur-behind reads: pair the pack with an opacity rule so the window
// has translucency for the frost to fill. Where the content is opaque the
// frost is fully hidden and this pass is a passthrough.
//
// DAEMON FALLBACK: daemon hosts have no scene behind a surface, so
// uHasBackdrop is 0 there (and backdropTexel() is transparent). The frost
// degrades to a faint premultiplied tint slab at the same corner rounding,
// so previews still communicate the pack's shape.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord);

    // Frost slab mask: the frame rect with rounded corners, 1px AA edge.
    vec2 px = surfacePixel(vTexCoord);
    vec2 frameCenter = uSurfaceFrameTopLeft + 0.5 * uSurfaceFrameSize;
    float radius = p_cornerRadius * uSurfaceScale;
    float d = sdRoundedBox(px - frameCenter, 0.5 * uSurfaceFrameSize, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 frost;
    if (uHasBackdrop >= 0.5) {
        // Blurred backdrop (premultiplied, effectively opaque under the
        // window) mixed toward the tint, as an opaque slab under the window.
        vec4 blurred = texture(iChannel1, vTexCoord);
        frost = vec4(mix(blurred.rgb, tint * blurred.a, tintStrength), blurred.a) * mask;
    } else {
        // No scene behind this surface (daemon hosts): a faint tint slab.
        frost = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    // Window content over the frost (both premultiplied).
    fragColor = window + frost * (1.0 - window.a);
}
