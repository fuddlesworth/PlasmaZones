// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Polar Function transition — a polar petal reveal where an n-fold cosine
// radial mask grows outward with progress. Inspired by liixini/shaders'
// niri polar-function shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// metadata.json declaration order → customParams[0] sub-slots:
// p_segmentsParam (customParams[0].x).

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    int segments = int(p_segmentsParam);
    float angle = atan(uv.y - 0.5, uv.x - 0.5);
    float radius = (cos(float(segments) * angle) + 4.0) / 4.0;
    float difference = length(uv - vec2(0.5, 0.5));
    float reveal = step(difference, radius * p);

    return win * reveal;
}
