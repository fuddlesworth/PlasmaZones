// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Colour Distance transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/colour-distance).
// Luminance-thresholded reveal — pixels emerge in order of brightness.
//
// Niri's colour-distance ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_power (customParams[0].x) is generated from metadata.json — no
// hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the niri OPEN body auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);

    vec4 win = surfaceColor(uv);

    float colorMag = length(win.rgb);
    float m = step(colorMag, p);
    float reveal = mix(m, 1.0, pow(p, p_power));

    return win * reveal;
}
