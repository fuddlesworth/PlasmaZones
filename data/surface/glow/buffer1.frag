// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow buffer pass 1 — VERTICAL Gaussian blur of buffer0's output (the
// horizontally-blurred surface, bound here as iChannel0). Together with
// buffer0.frag this is a separable 2D Gaussian: a smooth, wide glow source that
// effect.frag samples as iChannel1.
//
// iChannel0 is buffer0's FBO, written in whatever orientation the runtime's
// buffer chain uses (bottom-origin on the compositor, top-origin on Qt-RHI);
// either way it shares this pass's UV space, and the symmetric ±i Gaussian kernel
// is orientation-independent, so the tap offsets use vTexCoord directly with no
// flip. Same fixed UV step + sigma as buffer0 so the blur is isotropic in UV.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    const float sigma = 2.6;
    const float stepUV = 1.7 / 256.0; // vertical tap spacing in UV
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float w = exp(-float(i * i) / (2.0 * sigma * sigma));
        acc += texture(iChannel0, vTexCoord + vec2(0.0, float(i) * stepUV)) * w;
        wsum += w;
    }
    fragColor = acc / max(wsum, 1e-4);
}
