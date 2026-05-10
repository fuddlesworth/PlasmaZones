// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animated gradient panel shader for PhosphorShell (self-contained).
// Output is pre-multiplied alpha. SDF mask uses hard cutoff to avoid fringe.
//
// Visual: a two-colour gradient whose direction slowly rotates around the
// panel centre while the gradient position simultaneously pulses back and
// forth — produces a constantly-moving "flowing aurora" look without ever
// stopping or jittering. Both motions are derived from `speed` so the
// caller can scale the whole animation with one parameter.
//
// customParams[0]: x=speed y=baseAngle (radians, added to the rotation)
// customParams[1]: x=cornerRadius (pixels, default 12.0)
// customColors[0]: start color (customColor1 in QML)
// customColors[1]: end color (customColor2 in QML)

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

layout(location = 0) out vec4 fragColor;

// SDF for a rounded rectangle centered at origin
float roundedBoxSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) - r;
}

void main() {
    if (iResolution.x <= 0.0 || iResolution.y <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    float speed = customParams[0].x > 0.0 ? customParams[0].x : 0.5;
    float angle = customParams[0].y;
    float radius = customParams[1].x > 0.0 ? customParams[1].x : 12.0;

    // SDF rounded rectangle mask - hard cutoff eliminates fringe
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);

    // Smooth AA edge - premultiplied alpha prevents fringe
    float mask = 1.0 - smoothstep(-1.0, 0.0, dist);

    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    // ─── Animated gradient direction ──────────────────────────────────
    // Rotate the gradient direction around the panel centre. Multiplier
    // tuned so a default speed=1.0 gives a full rotation in ~6 seconds
    // — visibly moving without being seizure-inducing.
    float rotatedAngle = angle + iTime * speed * 1.0;
    vec2 dir = vec2(cos(rotatedAngle), sin(rotatedAngle));

    // ─── Gradient position with double pulse ──────────────────────────
    // Two out-of-phase sine waves at relatively-prime rates produce a
    // continuously-evolving compound motion that never visibly repeats.
    // Amplitudes sized aggressively (sum ~0.9) so the gradient sweeps
    // edge-to-edge — anything subtler is hard to notice on a small
    // panel where the gradient direction itself is also rotating.
    float t = dot(uv - 0.5, dir) + 0.5;
    float wave1 = sin(iTime * speed * 2.7) * 0.55;
    float wave2 = sin(iTime * speed * 1.7 + 1.57) * 0.35;
    t += wave1 + wave2;

    // Smooth hermite clamp for gradient edges
    t = smoothstep(0.0, 1.0, t);

    // Interpolate colors in linear space
    vec4 color = mix(colorA, colorB, t);

    // Apply mask to alpha
    float alpha = color.a * mask;

    // Pre-multiplied alpha output: RGB * alpha before output
    fragColor = vec4(color.rgb * alpha, alpha) * qt_Opacity;
}
