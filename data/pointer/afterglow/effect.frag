// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Afterglow pointer shader — the family's multipass example. The trail lives
// on a persistent canvas that buffer.frag keeps from frame to frame (it reads
// its own previous output through `bufferFeedback`, dims it and stamps the
// newest pointer position). This main pass only samples that canvas and
// tints it, shifting from `color` where the glow is fresh to `colorFaded` as
// it dims.
//
// REACH. The host's damage rect is the bounding box of every trail sample
// younger than trailSeconds, inflated by the reach, so the whole live stroke
// is inside it already and the reach only has to cover the brush radius plus
// the stamp's feather. The metadata value leaves room for the largest brush.
//
// TWO CLOCKS. The canvas decays per FRAME (buffer.frag multiplies by
// `persistence` each tick) while the host's liveness is wall-clock: it stops
// repainting once the newest event is trailSeconds old, whatever the frame
// rate was. At a low refresh rate the canvas has not emptied by then and the
// last painted frame would stay on screen as a frozen smear. So the idle cut
// below is the liveness guarantee: coverage is exactly zero once the pointer
// has rested for kIdleCutSeconds, inside trailSeconds, on every refresh
// rate. The per-frame decay is only the visual speed of the fade. buffer.frag
// applies the same cut to the stored energy so a resumed stroke does not
// bring the previous one back, and caps the persistence so the far end of a
// moving stroke is under the floor by the time the damage rect ends behind
// the pointer (afterglow_common.glsl holds both numbers for both stages).
//
// Coverage is also cut to exactly zero a little above the canvas floor so the
// pack returns transparent black everywhere once the canvas has emptied.
//
// The canvas runs at half resolution (metadata bufferScale 0.5): it stores a
// soft glow that this pass samples with linear filtering, so a full-size
// RGBA8 decay pass over the whole output every live frame bought nothing
// visible. Both stages address it with normalised uv, and iResolution is
// the output size in the buffer pass too, so the stamp lands where the
// pointer is at either scale; the smallest brush (4 logical px radius) is
// still two texels of radius there.

#include <pointer_multipass.glsl>
#include "afterglow_common.glsl"

vec4 pPointer(vec2 uv) {
    float energy = texture(iChannel0, uv).r;
    float cover = smoothstep(kCoverageFloor, 0.6, energy) * max(p_intensity, 0.0);
    cover *= 1.0 - smoothstep(kIdleCutStart, kIdleCutSeconds, pointerIdleSeconds());
    if (cover <= 0.0) {
        return vec4(0.0);
    }
    vec4 fresh = p_color;
    vec4 faded = p_colorFaded;
    vec3 rgb = mix(faded.rgb, fresh.rgb, energy);
    float alpha = cover * mix(faded.a, fresh.a, energy);
    return premul(rgb, alpha);
}
