// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow buffer pass 0 — HORIZONTAL Gaussian blur of the captured window surface.
// Separable blur: this pass blurs in X, buffer1.frag blurs the result in Y, so
// the two cheap 1D passes produce a smooth 2D Gaussian glow source (sampled by
// effect.frag as iChannel1). A single box blur looked blocky; a real Gaussian
// reads as a soft, premium bloom.
//
// This pass sees only uTexture0 (the captured surface, bound to unit 0 by the
// host). The per-frame surface geometry uniforms are NOT pushed to buffer
// passes, so the tap spacing is a fixed UV step (resolution-independent: the
// glow stays a consistent fraction of the window). The pass renders at
// bufferScale, so its own downsample adds to the softening.
//
// Output is premultiplied (surfaceTexel returns premultiplied alpha), so the
// downstream passes composite it directly.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    const float sigma = 2.6;
    const float stepUV = 1.7 / 256.0; // horizontal tap spacing in UV
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float w = exp(-float(i * i) / (2.0 * sigma * sigma));
        acc += surfaceTexel(vTexCoord + vec2(float(i) * stepUV, 0.0)) * w;
        wsum += w;
    }
    fragColor = acc / max(wsum, 1e-4);
}
