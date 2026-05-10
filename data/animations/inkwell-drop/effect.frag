// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Inkwell Drop transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/inkwell-drop). Drop-
// and-ripple — a circular ink front spreads from impact with concentric
// ringed distortion.
//
// Niri's inkwell-drop ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define impactX        customParams[0].x
#define impactY        customParams[0].y
#define frontSpeed     customParams[0].z
#define rippleStrength customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    vec2 impact = vec2(impactX, impactY);
    vec2 c = uv - impact;
    c.x *= iAnchorSize.x / max(iAnchorSize.y, 0.0001);
    float d = length(c);
    float front = p * frontSpeed;
    float ring1 = sin((d - front) * 80.0) * exp(-abs(d - front) * 6.0);
    float ring2 = sin((d - front + 0.08) * 80.0) * exp(-abs(d - front + 0.08) * 8.0) * 0.6;
    float ring3 = sin((d - front + 0.16) * 80.0) * exp(-abs(d - front + 0.16) * 10.0) * 0.4;
    float ripple = (ring1 + ring2 + ring3) * rippleStrength * (1.0 - p * 0.5);
    vec2 dir = (d > 0.001) ? normalize(c) : vec2(0.0);
    vec2 distorted = uv + dir * ripple;

    // Soft inside-mask. The ripple above pushes UVs past [0, 1] at boundary
    // fragments, and `uTexture0` is clamp-to-edge — the typical edge alpha
    // is 0 (window shadow / rounded corners) so samples beyond the surface
    // produce a grey-transparent border. The previous hard clamp pulled the
    // same edge texel for every off-surface fragment, which still smeared
    // the transparent edge. Fade to zero across a tight 0.005-wide band at
    // each edge so the rippled silhouette crops cleanly. Same pattern as
    // morph/plasma-flow.
    vec2 insideLo = smoothstep(vec2(0.0), vec2(0.005), distorted);
    vec2 insideHi = vec2(1.0) - smoothstep(vec2(0.995), vec2(1.0), distorted);
    float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;
    vec4 win = texture(uTexture0, distorted) * mask;

    float reveal = smoothstep(0.05, -0.02, d - front);
    vec4 mixed = win * reveal;

    float in_bounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    fragColor = mixed * in_bounds;
}
