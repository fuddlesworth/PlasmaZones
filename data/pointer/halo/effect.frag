// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Halo pointer shader — a gaussian glow centred on the pointer. Speed
// lifts the brightness through `speedGain`; idle time first settles the
// glow to `idleDim` with a gentle breath, then fades it to nothing by the
// end of the metadata's trailSeconds so the pass can go quiet.

const float kTrailSeconds = 0.6;

vec4 pPointer(vec2 uv) {
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float radius = max(p_radius, 1.0) * scale;

    float d = length(px - iMouse.xy);
    if (d > radius) {
        return vec4(0.0);
    }

    // Gaussian body that reaches zero exactly at the radius, so the pack
    // never paints outside the reach it declares.
    float sigma = radius * 0.4;
    float body = exp(-(d * d) / (2.0 * sigma * sigma));
    float edge = 1.0 - smoothstep(radius * 0.8, radius, d);
    body *= edge;

    float speed = uPointerVelocity.z;
    float gain = 1.0 + p_speedGain * clamp(speed / 1200.0, 0.0, 1.0);

    // Idle envelope: settle to idleDim over the first third of the window
    // while breathing, then fade out over the rest.
    float idle = pointerIdleSeconds();
    float settle = smoothstep(0.0, kTrailSeconds * 0.35, idle);
    float breath = 1.0 + 0.08 * sin(iTime * 4.0);
    float level = mix(1.0, clamp(p_idleDim, 0.0, 1.0) * breath, settle);
    float out_ = 1.0 - smoothstep(kTrailSeconds * 0.5, kTrailSeconds, idle);

    float alpha = body * clamp(p_intensity, 0.0, 2.0) * gain * level * out_;
    alpha = clamp(alpha, 0.0, 1.0) * p_color.a;
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha);
}
