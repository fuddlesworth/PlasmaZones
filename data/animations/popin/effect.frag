// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pop-in transition — center-anchored zoom of the rendered surface
// (sampled through iChannel0) with optional overshoot. Previously a
// stub that emitted a flat white mask; now scales the actual surface
// from `scaleFrom` up through `1 + overshoot` and settles at 1.0.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define scaleFrom customParams[0].x
#define overshoot customParams[0].y

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    vec2 uv = vTexCoord;

    // Visibility drives the scale curve. iTime is the per-leg [0,1]
    // progress driven by SurfaceAnimator's shaderTime AnimatedValue.
    // SurfaceAnimator runs iTime 0→1 on show and 1→0 on hide, so the
    // same scale curve naturally produces "scale up + settle" on show
    // and "settle + scale down" on hide.
    float visibility = clamp(iTime, 0.0, 1.0);

    // Two-phase scale curve: visibility 0.0 → 0.7 ramps scaleFrom up
    // to (1 + overshoot); 0.7 → 1.0 settles overshoot back to 1.0.
    // On hide (iTime ticks 1→0) the surface reads from the settled
    // 1.0 through the overshoot peak and down to scaleFrom — a
    // visible "bounce out" reverse of the show-leg arc.
    float scale;
    if (visibility < 0.7) {
        scale = mix(scaleFrom, 1.0 + overshoot, visibility / 0.7);
    } else {
        scale = mix(1.0 + overshoot, 1.0, (visibility - 0.7) / 0.3);
    }

    vec2 center = vec2(0.5);
    // Inverse scale on the sample UV: at scale<1 we sample a smaller
    // region of the texture (so the visual is zoomed-in / smaller in
    // screen space); at scale>1 we sample a larger region (visual is
    // zoomed-out, but with overshoot bouncing past 1 and back).
    vec2 sampleUv = (uv - center) / max(scale, 0.001) + center;

    // Outside the texture's [0,1] range = beyond the surface; output
    // transparent so the pop-in reads as a focused zoom rather than
    // a stretched edge bleed.
    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
        fragColor = vec4(0.0);
        return;
    }
    fragColor = texture(iChannel0, sampleUv);
}
