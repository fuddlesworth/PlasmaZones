// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pop-in transition shader for PhosphorShell — self-contained variant of
// data/animations/popin/effect.frag. Same algorithm (center-anchored
// scale of an offscreen-captured surface with overshoot bounce), but
// with the UBO declaration inlined so the shader compiles standalone
// without needing the data/animations/shared/animation_uniforms.glsl
// include search path wired up.
//
// Driven by the consumer keyframing iTime: 0.0 → 1.0 on show,
// 1.0 → 0.0 on hide. The two-phase scale curve produces "scale up +
// settle" on the show leg and "settle + scale down" on the hide leg
// from the same source code.
//
// customParams[0]: x=scaleFrom (initial scale, default 0.7)
//                  y=overshoot (peak past 1.0, default 0.08)
// uTexture0 (binding=7): captured QML surface to scale (set via
//                        ShaderBackground.sourceItem)

#version 450

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int appField0;
    int appField1;
    vec4 iMouse;
    vec4 iDate;
    vec4 customParams[8];
    vec4 customColors[16];
};

// Captured source surface — fed by ShaderBackground.sourceItem.
layout(binding = 7) uniform sampler2D uTexture0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    float scaleFrom = customParams[0].x > 0.0 ? customParams[0].x : 0.7;
    float overshoot = customParams[0].y > 0.0 ? customParams[0].y : 0.08;

    vec2 uv = vTexCoord;

    // iTime drives the [0, 1] progress. Two-phase curve: 0..0.7 ramps
    // scaleFrom up to (1 + overshoot); 0.7..1.0 settles overshoot back
    // to 1.0. On the hide leg (iTime ticking 1→0) the curve plays
    // backward — surface reads from the settled 1.0 through the
    // overshoot peak and down to scaleFrom — a natural bounce out.
    float visibility = clamp(iTime, 0.0, 1.0);
    float scale;
    if (visibility < 0.7) {
        scale = mix(scaleFrom, 1.0 + overshoot, visibility / 0.7);
    } else {
        scale = mix(1.0 + overshoot, 1.0, (visibility - 0.7) / 0.3);
    }

    vec2 center = vec2(0.5);
    // Inverse scale on the sample UV so a "scale" of 1.5 visually
    // appears 1.5x larger (we sample a smaller region of the texture).
    vec2 sampleUv = (uv - center) / max(scale, 0.001) + center;

    // Soft edge fade where the inverse-scaled UV briefly leaves [0, 1]
    // during the overshoot phase. A hard cutoff produced visible
    // 1-texel discontinuities on the bounce frames.
    vec2 insideLo = smoothstep(vec2(0.0), vec2(0.005), sampleUv);
    vec2 insideHi = vec2(1.0) - smoothstep(vec2(0.995), vec2(1.0), sampleUv);
    float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;

    // Cross-fade in opacity along the same iTime curve so the popup
    // doesn't snap from invisible to opaque. Below ~0.1 the surface
    // is essentially gone — keeps the shrink leg from leaving a
    // ghost when iTime → 0.
    float opacityCurve = smoothstep(0.0, 0.5, visibility);

    fragColor = texture(uTexture0, sampleUv) * mask * opacityCurve * qt_Opacity;
}
