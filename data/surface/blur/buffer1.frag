// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frost pack, buffer pass 1: VERTICAL half of the separable Gaussian, over
// buffer 0's horizontal result (iChannel0, same bufferScale resolution).
// Together the two passes approximate a full 2D Gaussian at a fraction of
// the tap count; the main pass samples this buffer as iChannel1.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // BUFFER-PASS CONVENTION: buffer shaders compile WITHOUT the generated
    // p_<id> parameter preamble on the daemon path (bakeBufferShaders), so
    // parameters are read by their RAW contract slot. blurRadius is this
    // pack's first scalar parameter, so it lives in customParams[0].x
    // (declaration-order auto-slotting; see buildParamPreamble).
    float radiusPx = max(customParams[0].x * uSurfaceScale, 1.0);
    vec2 stepUv = vec2(0.0, radiusPx / (4.0 * max(uSurfaceSize.y, 1.0)));
    vec4 sum = texture(iChannel0, vTexCoord) * 0.227027;
    sum += (texture(iChannel0, vTexCoord + stepUv) + texture(iChannel0, vTexCoord - stepUv)) * 0.1945946;
    sum += (texture(iChannel0, vTexCoord + 2.0 * stepUv) + texture(iChannel0, vTexCoord - 2.0 * stepUv)) * 0.1216216;
    sum += (texture(iChannel0, vTexCoord + 3.0 * stepUv) + texture(iChannel0, vTexCoord - 3.0 * stepUv)) * 0.054054;
    sum += (texture(iChannel0, vTexCoord + 4.0 * stepUv) + texture(iChannel0, vTexCoord - 4.0 * stepUv)) * 0.016216;
    fragColor = sum;
}
