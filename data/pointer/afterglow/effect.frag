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
// REACH is flat rather than tied to the brush radius: the canvas holds glow
// wherever the pointer has recently been, and the host only repaints inside
// the reach of the live samples, so the reach has to cover how far a stroke
// can fade behind the pointer. With the default persistence the canvas is
// under one 8-bit step within about half a second, well inside trailSeconds,
// and the metadata reach covers the ground a stroke covers in that time at
// ordinary hand speed.
//
// Coverage is cut to exactly zero a little above the canvas floor so the
// pack returns transparent black everywhere once the canvas has emptied,
// which is what lets the host go quiet.

#include <pointer_multipass.glsl>

vec4 pPointer(vec2 uv) {
    float energy = texture(iChannel0, uv).r;
    float cover = smoothstep(0.02, 0.6, energy) * max(p_intensity, 0.0);
    if (cover <= 0.0) {
        return vec4(0.0);
    }
    vec4 fresh = p_color;
    vec4 faded = p_colorFaded;
    vec3 rgb = mix(faded.rgb, fresh.rgb, energy);
    float alpha = cover * mix(faded.a, fresh.a, energy);
    return premul(rgb, alpha);
}
