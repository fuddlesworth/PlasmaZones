// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Afterglow buffer pass: the persistent canvas. With `bufferFeedback` this
// pass's own previous frame is bound as iChannel0, so each frame it reads the
// canvas back, dims it by `persistence`, and stamps a soft dot along the
// newest trail segment on top. The main pass (effect.frag) only tints what is
// stored here; it never draws the trail itself.
//
// The canvas stores glow ENERGY in .r, 1 where the pointer has just been and
// decaying toward 0, with .a mirroring it so a debugging view of the target
// still reads.
//
// PARAMETERS BY SLOT. A buffer pass gets no `p_<id>` preamble on either
// runtime (the compositor, the preview and the validator all skip it), so the
// pack's scalar parameters are read from their raw customParams lanes in
// declaration order: persistence is the first float declared, radius the
// second.
//
// So the ORDER of the `parameters` array in metadata.json is load-bearing for
// this pack in a way it is not for any single-pass one. Reordering those two
// entries, or inserting another scalar ahead of them, silently swaps what
// this stage reads: nothing fails to build, nothing fails to validate, and
// the decay constant arrives as a radius. Add new scalars AFTER these two.
//
// ORIENTATION. vTexCoord addresses this pass's own target and the main pass
// samples iChannel0 with the same vTexCoord it hands to pointerPixel(), so a
// texel written for pointerPixel(vTexCoord) here is read back for the same
// canvas position there, on both runtimes, with no flip of its own.
//
// The stamp is gated on the pointer having MOVED within kFreshSeconds, read
// from the idle clock rather than the newest sample's age: a host that feeds
// a resting pointer every tick still appends a slot at age zero every
// interval, and gating on that age kept re-stamping the resting spot at full
// energy while the idle clock rose. On the idle clock a resting pointer
// stops laying down glow on both hosts, so the canvas empties within
// trailSeconds and the host can stop repainting.
//
// The decay is per frame, so at a low refresh rate the canvas is not empty
// when the host goes quiet at trailSeconds. The idle envelope below is the
// same wall-clock cut effect.frag applies to its coverage: it takes the
// stored energy to zero over the same window, so the stroke the pointer left
// before a pause is not still on the canvas when the next stroke starts.

#version 450

#include <pointer_lib.glsl>
#include <pointer_multipass.glsl>
#include "afterglow_common.glsl"

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// One frame at the lowest refresh rate the family designs for (20 Hz) plus
// scheduling latency: a frame painted later than this after the last move
// would skip the stamp and never lay that segment down. Rest slots are
// excluded by the idle clock itself, which they do not reset.
const float kFreshSeconds = 0.1;

void main() {
    float persistence = clamp(customParams[0].x, 0.0, kMaxPersistence);
    float radius = max(customParams[0].y, 1.0) * pointerScale();

    float idleCut = 1.0 - smoothstep(kIdleCutStart, kIdleCutSeconds, pointerIdleSeconds());
    float energy = texture(iChannel0, vTexCoord).r * persistence * idleCut;
    // Below one 8-bit step the canvas is empty in every way that matters, so
    // snap it to zero. This only completes the idle cut: while the pointer
    // moves, 8-bit rounding stalls the decay well above one step (see
    // afterglow_common.glsl), and it is the main pass's coverage floor that
    // hides that residue, not this snap.
    if (energy < 1.0 / 255.0) {
        energy = 0.0;
    }

    int count = pointerTrailCount();
    if (count >= 1 && pointerIdleSeconds() < kFreshSeconds) {
        vec2 px = pointerPixel(vTexCoord);
        float d;
        if (count >= 2) {
            // A capsule along the newest segment, so a fast stroke lays down
            // a continuous line rather than a row of dots.
            //
            // A STRAIGHT capsule, and a deliberate exception to the shared
            // curve every other path pack traces. This pack does not stroke a
            // path at all: it stamps the newest span into an accumulation
            // buffer once per frame and lets the buffer decay, so what a
            // viewer sees is the union of hundreds of overlapping stamps
            // rather than one traced line, and a curve through four control
            // points would cost the walk without changing the union. If this
            // ever draws the whole run in one pass, it must come onto
            // pointerCurveDistanceFrom with the rest of the family.
            float t;
            d = pointerSegmentDistance(px, 0, t);
        } else {
            d = length(px - pointerTrailAt(0).xy);
        }
        float stamp = 1.0 - smoothstep(radius * 0.5, radius, d);
        energy = max(energy, stamp);
    }

    fragColor = vec4(energy, 0.0, 0.0, energy);
}
