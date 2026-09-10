// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Halo pointer shader — a gaussian glow centred on the pointer. Speed
// lifts the brightness through `speedGain`; idle time first settles the
// glow to `idleDim` with a gentle breath, then fades it to nothing by
// kIdleSeconds so the pass can go quiet.
//
// A press adds a second, softer swell centred on the press point: it grows
// and settles rather than expanding as a ring, which is click-ripple's job.
// The swell carries its own envelope so it still reads when the pointer is
// resting and the idle fade has already taken the base glow down. It is gone
// by kSwellSeconds, which the metadata's trailSeconds covers.
//
// `activationSpeed` gates the base glow through the shared
// pointerActivationGate() so the halo can be made to appear only once the
// pointer is really moving.
// It stacks with `speedGain` rather than duplicating it: the gate decides
// whether the glow is there, speedGain decides how bright it is once it is.
// At the default 0 there is no threshold, which is the behaviour the pack
// shipped with. The press swell is deliberately outside the gate, so a click
// still answers when the pointer is sitting still. There is no `smoothing`
// here: this pack is a glow centred on the pointer, not a path trace, so
// there is no curve for smoothing to act on.

// How long the glow lingers after the pointer stops. Inside the metadata
// trailSeconds so the last live frame is already clear, and sized so one
// whole breath at the rate below fits inside it (2π / 7.4 = 0.85 s), with
// the late fade only taking the last quarter of that breath, so the
// "breathes while you hold still" the pack advertises is on screen.
const float kIdleSeconds = 0.85;
// One full cycle inside the visible window, in radians per second. Nudged
// at use onto a divisor of the iTime wrap (pointerWrapSafeRate) so the
// breath does not jump when iTime wraps.
const float kBreathRate = 7.4;
const float kSwellSeconds = 0.55;
// Speed (logical px/s, scaled at use) at which speedGain is fully applied.
// Deliberately low: the settings preview's simulated pointer peaks near
// 324 px/s.
const float kFullSpeed = 200.0;

vec4 pPointer(vec2 uv) {
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    // The reach the host resolved from `radius`, in device px, read from the
    // uniform so the damage rect and the glow's cut cannot drift.
    float radius = max(pointerReach(), 1.0);
    float sigma = radius * 0.4;
    float intensity = clamp(p_intensity, 0.0, 2.0);

    float alpha = 0.0;

    // Base glow, centred on the pointer.
    float d = length(px - iMouse.xy);
    if (d <= radius) {
        // Gaussian body that reaches zero exactly at the radius, so the pack
        // never paints outside the reach it declares.
        float body = exp(-(d * d) / (2.0 * sigma * sigma));
        body *= pointerReachWindow(d, radius);

        // The filtered speed, not the raw per-event velocity, which reads 0
        // whenever two events share a millisecond and would blink both the
        // gate and the gain.
        float speed = pointerFilteredSpeed();
        float gain = 1.0 + p_speedGain * clamp(speed / (kFullSpeed * scale), 0.0, 1.0);
        float gate = pointerActivationGate(p_activationSpeed);

        // Idle envelope: settle to idleDim over the first third of the window
        // while breathing, then fade out over the rest.
        float idle = pointerIdleSeconds();
        float settle = smoothstep(0.0, kIdleSeconds * 0.35, idle);
        float breath = 1.0 + 0.08 * sin(iTime * pointerWrapSafeRate(kBreathRate / TAU) * TAU);
        float level = mix(1.0, clamp(p_idleDim, 0.0, 1.0) * breath, settle);
        // Fade late, so the breath is a thing you watch rather than something
        // the fade eats.
        float out_ = 1.0 - smoothstep(kIdleSeconds * 0.75, kIdleSeconds, idle);

        alpha += body * intensity * gain * level * out_ * gate;
    }

    // Press swell, centred on the press point and cut at the same radius.
    float sincePress = pointerSincePress();
    if (p_clickSwell > 0.0 && uPointerPress.w > 0.5 && sincePress < kSwellSeconds) {
        float dp = length(px - uPointerPress.xy);
        if (dp <= radius) {
            float ct = sincePress / kSwellSeconds;
            // Rises quickly, then settles back down to nothing.
            float env = ct * exp(1.0 - 4.0 * ct) * (1.0 - ct);
            float swellSigma = sigma * mix(0.7, 1.15, ct);
            float body = exp(-(dp * dp) / (2.0 * swellSigma * swellSigma));
            body *= pointerReachWindow(dp, radius);
            alpha += body * env * intensity * p_clickSwell;
        }
    }

    alpha = clamp(alpha, 0.0, 1.0) * p_color.a;
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha);
}
