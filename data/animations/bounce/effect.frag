// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Bounce transition — the window drops in from above its frame and
// settles with a decaying bounce. Inspired by liixini/shaders' niri
// bounce (https://github.com/liixini/shaders/tree/main/bounce); the
// oscillation is reworked here so the motion reads as an actual bounce.
//
// MOTION MODEL — the window is rigidly translated along its own
// vertical axis by `offset`, the height (measured in window-heights) it
// sits above its resting frame:
//
//   offset(t) = decay(t) * abs(cos(t * PI * bounces))
//
//   * abs(cos(t·PI·bounces)) is the bounce oscillation. It equals 1 at
//     t=0 (window one full height above the frame) and touches 0
//     `bounces` times — each zero is a floor contact, a CUSP where the
//     window's velocity reverses instantly, exactly as a real impact
//     behaves. The arcs between contacts are smooth.
//   * decay(t) = (1 - t)^2 damps the peak heights so each bounce rises
//     lower than the last and the window eases to rest (offset 0) at
//     t=1.
//
// SURFACE EXTENT — this is a `fboExtent: "surface"` shader: the runtime
// sizes the render target to the whole surface so the window can
// genuinely travel above its frame. `anchorRemap` (see anchor_remap.glsl)
// converts each fragment's surface-UV into the window's own [0,1]
// space; the window is then rigidly translated and `boundaryMaskAA`
// (see noise.glsl) antialiases the crop one device pixel wide so the
// daemon's anchor-sized texture leaves no clamp-to-edge halo.
//
// DIRECTION — this is a SYMMETRIC transition, written as a single
// `pTransition`. PlasmaZones flips `t` (the leg's iTime) on reverse
// legs (0→1 on open, 1→0 on close), so the close leg plays this motion
// in reverse automatically — no direction code, no `pIn`/`pOut` split.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). These two includes are pack-specific, so they stay here.
#include <noise.glsl>
#include <anchor_remap.glsl>

// `p_bounces` is generated from metadata.json (the customParams[0].x lane).
//
// `uv` is the whole-surface vTexCoord; `t` is the leg's iTime (runtime-flipped
// on the close leg, which is what makes the symmetric bounce auto-reverse).
vec4 pTransition(vec2 uv, float t) {
    t = clamp(t, 0.0, 1.0);
    const float PI = 3.14159265358979;

    // Map the surface-UV into the window's ("anchor") own [0,1] space.
    // Fragments outside the window map outside [0,1] and are cropped by
    // boundaryMaskAA below, widened by the decoration chain's outer margin
    // so a bouncing window carries its halo instead of clipping it at the
    // frame edge.
    vec2 auv = anchorRemap(uv);

    // Decaying bounce: one window-height above the frame at t=0, settling
    // to 0 at t=1 with `p_bounces` floor contacts in between.
    float decay = (1.0 - t) * (1.0 - t);
    float offset = decay * abs(cos(t * PI * p_bounces));

    // Rigid vertical translate in anchor space: sampling further down the
    // window (+offset) lifts its content above the frame. The offset swings
    // with the bounce and settles to 0 (identity sampling) at rest.
    vec2 sample_uv = auv;
    sample_uv.y = auv.y + offset;
    return surfaceColor(sample_uv) * boundaryMaskAA(sample_uv, surfacePadRel());
}
