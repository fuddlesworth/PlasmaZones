// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Directional motion blur on the scrolling strip, driven by scroll velocity.
//
// Strip contract (see strip_transition.glsl): iTime is SECONDS, not progress;
// there is no from/to pair; intensity keys off iStripMotion, which converges
// to zero at settle — the identity contract. The horizontal N-tap smear is
// proportional to the velocity in output-widths per second (iStripMotion.w),
// saturating so a violent fling cannot smear the whole screen, and masked to
// the strip's work area so panels stay sharp.

#include <strip_transition.glsl>

// t is the entry point's iTime pass-through — SECONDS on the strip pass, not
// progress — and this pack keys everything off iStripMotion instead.
vec4 pTransition(vec2 uv, float t) {
    float mask = stripMask(uv, p_edgeFeather);
    // Half-width of the smear kernel in uv units. 0.06 maps one
    // output-width-per-second of scroll speed onto a 6% smear at full
    // strength; the clamp caps the kernel at 3.5% of the output whatever the
    // fling velocity.
    float smear = clamp(abs(iStripMotion.w) * 0.06 * p_strength, 0.0, 0.035) * mask;
    if (smear < 1.0e-5) {
        // Identity at (or near) settle — REQUIRED by the strip contract.
        return getStripColor(uv);
    }
    float dir = sign(iStripMotion.w);
    vec4 acc = vec4(0.0);
    const int kTaps = 9;
    for (int i = 0; i < kTaps; ++i) {
        float t = (float(i) / float(kTaps - 1) - 0.5) * 2.0;
        acc += getStripColor(uv + vec2(dir * t * smear, 0.0));
    }
    vec4 blurred = acc / float(kTaps);
    return mix(getStripColor(uv), blurred, mask);
}
