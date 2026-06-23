// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Polka Dots Curtain transition — ported from liixini/shaders niri
// shader (https://github.com/liixini/shaders/tree/main/polka-dots-curtain).
// Polka-dots curtain — reveal threshold scales with distance from
// origin so dots open faster near a corner.
//
// Niri's polka-dots-curtain ships symmetric close.glsl/open.glsl —
// bodies are identical apart from `p = niri_clamped_progress` vs
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
// p_dotCount (customParams[0].x), p_centerX (customParams[0].y),
// p_centerY (customParams[0].z).

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 center = vec2(p_centerX, p_centerY);
    // `p_dotCount` means "dots across the screen": multiplying by
    // iAnchorSize/iSurfaceScreenPos.zw scales the count down to the
    // fraction of the screen the captured surface covers, so dot pitch
    // (in logical pixels) stays the same on popup vs. maximized windows
    // of a given display. Matches niri's reference when surface = screen
    // (the multiplier collapses to 1.0 there). Floors guard against the
    // pre-first-frame (0,0) state of either uniform.
    vec2 dotsAcross = vec2(p_dotCount) * max(iAnchorSize, vec2(1.0))
                                     / max(iSurfaceScreenPos.zw, vec2(1.0));
    float reveal = step(distance(fract(uv * dotsAcross), vec2(0.5, 0.5)), p / max(distance(uv, center), 0.0001));

    return win * reveal;
}
