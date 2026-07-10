// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Separable-Gaussian buffer-pass helpers shared by every multipass surface
// pack whose buffer passes blur the backdrop (blur / duotone / frosted-glass /
// glass / rain-glass / rippled-glass). The two halves of a 9-tap separable
// Gaussian (sigma ~ radius/3), previously copy-pasted into each pack's
// buffer0.frag / buffer1.frag, live here once and are called by the shared
// gaussian_h.frag / gaussian_v.frag standard passes (builtin:gaussian-h/-v).
//
// BUFFER-PASS CONVENTION: buffer shaders compile WITHOUT the generated p_<id>
// parameter preamble (bakeBufferShaders), so parameters are read by their RAW
// contract slot. `blurRadius` is the first scalar parameter of every blur-family
// pack, so it lives in customParams[0].x (declaration-order auto-slotting; see
// buildParamPreamble). Offsets step in canvas UV: the logical-px radius is
// scaled to device px by uSurfaceScale, normalized by the canvas extent
// (uSurfaceSize), and spread over the kernel's 4-tap reach.

#ifndef PLASMAZONES_SURFACE_BLUR_GLSL
#define PLASMAZONES_SURFACE_BLUR_GLSL

#include <surface_uniforms.glsl>
// This kernel samples the backdrop (pass 0) and iChannel0 (pass 1), so it pulls
// in both opt-in modules — a buffer pass that includes surface_blur.glsl gets
// them transitively and needs no include of its own.
#include <surface_backdrop.glsl>
#include <surface_multipass.glsl>

// 9-tap Gaussian weights (sigma ~ radius/3), summing to ~1.
const float kSurfaceGaussW0 = 0.227027;
const float kSurfaceGaussW1 = 0.1945946;
const float kSurfaceGaussW2 = 0.1216216;
const float kSurfaceGaussW3 = 0.054054;
const float kSurfaceGaussW4 = 0.016216;

// Buffer pass 0: HORIZONTAL half over the BACKDROP capture (through
// backdropTexel(), which clamps into the valid sub-rect and is transparent on
// the daemon). Rendered at the pack's bufferScale.
vec4 surfaceGaussianBackdropH(vec2 uv) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 1.0);
    vec2 stepUv = vec2(radiusPx / (4.0 * max(uSurfaceSize.x, 1.0)), 0.0);
    vec4 sum = backdropTexel(uv) * kSurfaceGaussW0;
    sum += (backdropTexel(uv + stepUv) + backdropTexel(uv - stepUv)) * kSurfaceGaussW1;
    sum += (backdropTexel(uv + 2.0 * stepUv) + backdropTexel(uv - 2.0 * stepUv)) * kSurfaceGaussW2;
    sum += (backdropTexel(uv + 3.0 * stepUv) + backdropTexel(uv - 3.0 * stepUv)) * kSurfaceGaussW3;
    sum += (backdropTexel(uv + 4.0 * stepUv) + backdropTexel(uv - 4.0 * stepUv)) * kSurfaceGaussW4;
    return sum;
}

// Buffer pass 1: VERTICAL half over buffer 0's result (iChannel0, same
// bufferScale resolution). Together the two passes approximate a full 2D
// Gaussian; the main pass samples the result as iChannel1.
vec4 surfaceGaussianChannelV(vec2 uv) {
    float radiusPx = max(customParams[0].x * uSurfaceScale, 1.0);
    vec2 stepUv = vec2(0.0, radiusPx / (4.0 * max(uSurfaceSize.y, 1.0)));
    vec4 sum = texture(iChannel0, uv) * kSurfaceGaussW0;
    sum += (texture(iChannel0, uv + stepUv) + texture(iChannel0, uv - stepUv)) * kSurfaceGaussW1;
    sum += (texture(iChannel0, uv + 2.0 * stepUv) + texture(iChannel0, uv - 2.0 * stepUv)) * kSurfaceGaussW2;
    sum += (texture(iChannel0, uv + 3.0 * stepUv) + texture(iChannel0, uv - 3.0 * stepUv)) * kSurfaceGaussW3;
    sum += (texture(iChannel0, uv + 4.0 * stepUv) + texture(iChannel0, uv - 4.0 * stepUv)) * kSurfaceGaussW4;
    return sum;
}

#endif // PLASMAZONES_SURFACE_BLUR_GLSL
