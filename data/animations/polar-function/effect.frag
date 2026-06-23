// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Polar Function transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/polar-function). Polar
// petal reveal — n-fold cosine radial mask grows outward with progress.
//
// Niri's polar-function ships symmetric close.glsl/open.glsl — bodies
// are identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// with `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)`
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// metadata.json declaration order → customParams[0] sub-slots:
// p_segmentsParam (customParams[0].x).

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    int segments = int(p_segmentsParam);
    float angle = atan(uv.y - 0.5, uv.x - 0.5);
    float radius = (cos(float(segments) * angle) + 4.0) / 4.0;
    float difference = length(uv - vec2(0.5, 0.5));
    float reveal = step(difference, radius * p);

    return win * reveal;
}
