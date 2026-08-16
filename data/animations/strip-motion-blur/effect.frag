// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Motion blur on the scrolling strip, driven by scroll velocity.
//
// Strip contract (see strip_transition.glsl): iTime is SECONDS, not progress;
// there is no from/to pair; intensity keys off iStripMotion, which converges
// to zero at settle — the identity contract. The N-tap smear runs along the
// strip's travel axis and is proportional to the velocity in output-extents
// along the travel axis per second (iStripMotion.w),
// saturating so a violent fling cannot smear the whole screen, and masked to
// the strip's work area so panels stay sharp. The kernel is symmetric, so
// scroll direction changes nothing: a smear reads the same either way.

#include <strip_transition.glsl>

// t is the entry point's iTime pass-through — SECONDS on the strip pass, not
// progress — and this pack keys everything off iStripMotion instead.
vec4 pTransition(vec2 uv, float t) {
    vec4 base = getStripColor(uv);
    float mask = stripMask(uv, p_edgeFeather);
    // Half-width of the smear kernel in uv units. The 3.5% clamp is what
    // binds in practice: it is reached at roughly 0.6 output-extents along the
    // travel axis per second at full strength, and a wheel fling runs well
    // past that. The
    // 0.06 slope only shapes the ramp below that speed.
    //
    // The work-area mask scales the kernel itself rather than blending the
    // result afterwards. One application, so the feather band is linear in
    // the mask, and a fragment outside the strip falls straight through the
    // identity early-out below instead of running nine taps to be discarded.
    float smear = clamp(abs(iStripMotion.w) * 0.06 * p_strength, 0.0, 0.035) * mask;
    // Die out before the taps can cross the screen edge, where the clamped
    // capture would smear the last pixel column (see stripEdgeFade).
    smear *= stripEdgeFade(uv, 0.07);
    if (smear < 1.0e-5) {
        // Identity at (or near) settle — REQUIRED by the strip contract.
        return base;
    }
    const int kTaps = 9;
    const int kCenter = kTaps / 2;
    vec4 acc = vec4(0.0);
    for (int i = 0; i < kTaps; ++i) {
        if (i == kCenter) {
            acc += base; // the centre tap IS uv; reuse the hoisted fetch
            continue;
        }
        float tap = (float(i) / float(kTaps - 1) - 0.5) * 2.0;
        acc += getStripColor(uv + stripAxisOffset(tap * smear));
    }
    return acc / float(kTaps);
}
