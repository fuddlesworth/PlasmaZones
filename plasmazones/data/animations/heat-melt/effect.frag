// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Heat Melt transition — a heat-driven melt where simplex noise carves
// the surface from a bottom-right corner with radial falloff. Inspired by
// liixini/shaders' niri heat-melt shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

#include <noise.glsl>
vec3 hm_mod289_3(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 hm_mod289_2(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 hm_permute(vec3 x) { return hm_mod289_3(((x * 34.0) + 1.0) * x); }
float hm_snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = hm_mod289_2(i);
    vec3 q = hm_permute(hm_permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m; m = m * m;
    vec3 x = 2.0 * fract(q * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 center = vec2(p_meltOriginX, p_meltOriginY);
    // `meltNoiseScale` means "noise cycles across the screen WIDTH":
    // multiplying by iAnchorSize.x/iSurfaceScreenPos.z scales the cycle
    // count to the fraction of the screen this surface covers, so
    // melt-front wobble pixel size stays constant across popup vs.
    // maximized windows. The multiplier is 1.0 when the surface fills
    // the screen. Floor guards against the pre-first-
    // frame iSurfaceScreenPos = (0,0,0,0) state.
    float perScreenScaleX = p_meltNoiseScale * max(iAnchorSize.x, 1.0)
                                           / max(iSurfaceScreenPos.z, 1.0);
    float dist = distance(center, uv) - p * exp(hm_snoise(vec2(uv.x * perScreenScaleX, 0.0)) * p_meltAggressiveness);
    float r = p - classicHash(vec2(uv.x, 0.1));
    float reveal = (dist <= r) ? 1.0 : (p * p * p);

    return win * reveal;
}
