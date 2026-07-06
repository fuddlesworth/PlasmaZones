// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Duotone pack, main pass: the Gaussian-blurred backdrop (buffer 1)
// collapsed to luminance and remapped onto a two-colour gradient — the
// concert-poster look. A contrast exponent shapes the split between the
// shadow and highlight colours. Same slab composite as the blur family:
// the pane shows through wherever the window itself is translucent.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the duotone backdrop.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pack renders a still shadow-to-highlight gradient slab with the
// same corner rounding.

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    FrameSDF fs = frameSdf(px, p_cornerRadius * uSurfaceScale);
    float mask = frameMask(fs.d);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        vec4 blurred = texture(iChannel1, vTexCoord);
        // Un-premultiply before taking luminance so a translucent backdrop
        // region doesn't read darker than it is, then re-weight the mapped
        // colour by the capture's own alpha to stay premultiplied.
        float luma = blurred.a > 0.001 ? dot(blurred.rgb / blurred.a, vec3(0.2126, 0.7152, 0.0722)) : 0.0;
        luma = pow(clamp(luma, 0.0, 1.0), max(p_contrast, 0.05));
        vec3 mapped = mix(p_colorA.rgb, p_colorB.rgb, luma);
        pane = vec4(mapped * blurred.a, blurred.a) * mask;
    } else {
        // Original pseudo look for daemon surfaces: a vertical
        // shadow-to-highlight gradient slab at modest alpha.
        vec2 fuv = frameUv(px);
        vec3 grad = mix(p_colorA.rgb, p_colorB.rgb, smoothstep(0.0, 1.0, 1.0 - fuv.y));
        pane = vec4(grad, 1.0) * 0.4 * mask;
    }

    fragColor = slabComposite(window, pane);
}
