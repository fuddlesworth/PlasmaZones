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
// ORIENTATION. vTexCoord addresses this pass's own target and the main pass
// samples iChannel0 with the same vTexCoord it hands to pointerPixel(), so a
// texel written for pointerPixel(vTexCoord) here is read back for the same
// canvas position there, on both runtimes, with no flip of its own.
//
// The stamp is gated on the newest sample being fresh: a resting pointer
// stops laying down glow, so the canvas empties within trailSeconds and the
// host can stop repainting. Left ungated it would refresh the resting spot
// forever and a frozen dot would be left behind when the pass went quiet.

#version 450

#include <pointer_lib.glsl>
#include <pointer_multipass.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kFreshSeconds = 0.05;

void main() {
    float persistence = clamp(customParams[0].x, 0.0, 0.97);
    float radius = max(customParams[0].y, 1.0) * pointerScale();

    float energy = texture(iChannel0, vTexCoord).r * persistence;
    // Below one 8-bit step the canvas is empty in every way that matters, so
    // snap it to zero rather than let the decay asymptote leave a faint
    // residue that never clears.
    if (energy < 1.0 / 255.0) {
        energy = 0.0;
    }

    int count = pointerTrailCount();
    if (count >= 1 && pointerTrailAt(0).z < kFreshSeconds) {
        vec2 px = pointerPixel(vTexCoord);
        float d;
        if (count >= 2) {
            // A capsule along the newest segment, so a fast stroke lays down
            // a continuous line rather than a row of dots.
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
