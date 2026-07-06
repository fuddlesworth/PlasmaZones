// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted-glass pack, main pass: the phosphor-shell frosted panel shader
// (examples/phosphor-shell/shaders/frosted_glass.frag) ported onto a REAL
// blurred backdrop. The original faked frosting with a translucent tint
// slab; here the slab is the Gaussian-blurred scene behind the surface
// (buffer 1), and the original's layers ride on top unchanged: the
// multi-octave crystalline Voronoi grain (slow-drifting on iTime), the
// tint, the multiplicative vignette, and the rounded-corner SDF clip.
//
// DAEMON FALLBACK: daemon-hosted surfaces (OSDs / popups) have no scene
// behind them (uHasBackdrop = 0), so the pack renders the ORIGINAL pseudo
// look there — the translucent tint slab with the same grain and vignette.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the frosted backdrop.

#version 450
#include <surface_lib.glsl>
#include <surface_noise.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Animated two-colour gradient, ported from the shell's gradient.frag
// computeGradient(): the direction turns continuously with time and the
// gradient position drifts on two out-of-phase sines, so the colours sweep
// across the pane rather than sitting still.
vec3 gradientColor(vec2 panelUv) {
    float speed = max(p_gradientSpeed, 0.0);
    float rotatedAngle = radians(p_gradientAngle) + iTime * speed;
    vec2 dir = vec2(cos(rotatedAngle), sin(rotatedAngle));
    float t = dot(panelUv - 0.5, dir) + 0.5;
    t += sin(iTime * speed * 2.7) * 0.55 + sin(iTime * speed * 1.7 + 1.57) * 0.35;
    t = smoothstep(0.0, 1.0, t);
    return mix(p_colorA.rgb, p_colorB.rgb, t);
}

// Multi-octave crystalline texture: dominant crystal structure + drifting
// fine detail + micro noise.
float frostedTexture(vec2 p, float time) {
    float crystal = voronoi(p);
    float fine = voronoi(p * 2.3 + vec2(time * 0.1, time * 0.07));
    float micro = vnoise(p * 8.0 + time * 0.2);
    return crystal * 0.6 + fine * 0.3 + micro * 0.1;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    FrameSDF fs = frameSdf(px, p_cornerRadius * uSurfaceScale);
    float mask = frameMask(fs.d);

    // Frame-normalized coordinate for the grain and vignette, so the look
    // scales with the pane like the original's uv did with the panel.
    vec2 fuv = frameUv(px);

    // Crystalline frost variation, centered around zero (both directions —
    // one-sided clamping made the grain read as dark speckles only).
    float frost = frostedTexture(fuv * p_grainScale, iTime * p_grainSpeed);
    float variation = (frost - 0.5) * p_grainAmount;

    // Vignette darkens edges, multiplicative so it never brightens.
    float vignette = clamp(1.0 - length((fuv - 0.5) * vec2(0.3, 1.0)) * p_vignetteStrength, 0.0, 1.0);

    vec3 grad = gradientColor(fuv);
    float gradStrength = clamp(p_gradientStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Real frosting: blurred backdrop tinted by the turning gradient,
        // grained, vignetted.
        vec4 blurred = texture(iChannel1, vTexCoord);
        vec3 color = mix(blurred.rgb, grad * blurred.a, gradStrength);
        color = clamp(color + vec3(variation) * blurred.a, 0.0, 1.0 * max(blurred.a, 0.0001));
        color *= vignette;
        pane = vec4(color, blurred.a) * mask;
    } else {
        // Original pseudo look (the shell TopPanel): a translucent animated
        // gradient slab with the same grain and vignette.
        float slabAlpha = clamp(0.4 + 0.6 * gradStrength, 0.0, 1.0);
        vec3 color = clamp(grad + vec3(variation), 0.0, 1.0) * vignette;
        pane = vec4(color, 1.0) * slabAlpha * mask;
    }

    fragColor = slabComposite(window, pane);
}
