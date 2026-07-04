// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frost pack, buffer pass 0: HORIZONTAL half of a separable 9-tap Gaussian
// over the BACKDROP capture (the scene behind the window), rendered at the
// pack's bufferScale (quarter res). Sampling goes through backdropTexel(),
// which clamps into the capture's valid sub-rect and compiles to transparent
// on the daemon (no scene there) — the main pass falls back to a plain tint
// on uHasBackdrop. Offsets step in canvas UV: p_blurRadius is logical px,
// scaled to device px by uSurfaceScale, normalized by the canvas extent
// (uSurfaceSize), and spread over the kernel's 4-tap reach.

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
    vec2 stepUv = vec2(radiusPx / (4.0 * max(uSurfaceSize.x, 1.0)), 0.0);
    // Classic 9-tap Gaussian weights (sigma ~ radius/3).
    vec4 sum = backdropTexel(vTexCoord) * 0.227027;
    sum += (backdropTexel(vTexCoord + stepUv) + backdropTexel(vTexCoord - stepUv)) * 0.1945946;
    sum += (backdropTexel(vTexCoord + 2.0 * stepUv) + backdropTexel(vTexCoord - 2.0 * stepUv)) * 0.1216216;
    sum += (backdropTexel(vTexCoord + 3.0 * stepUv) + backdropTexel(vTexCoord - 3.0 * stepUv)) * 0.054054;
    sum += (backdropTexel(vTexCoord + 4.0 * stepUv) + backdropTexel(vTexCoord - 4.0 * stepUv)) * 0.016216;
    fragColor = sum;
}
