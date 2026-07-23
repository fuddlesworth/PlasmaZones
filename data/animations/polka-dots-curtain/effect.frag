// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Polka Dots Curtain transition — a polka-dots curtain where the reveal
// threshold scales with distance from the origin, so dots open faster near
// a corner. Inspired by liixini/shaders' niri polka-dots-curtain shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// metadata.json declaration order → customParams[0] sub-slots:
// p_dotCount (customParams[0].x), p_centerX (customParams[0].y),
// p_centerY (customParams[0].z).

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 center = vec2(p_centerX, p_centerY);
    // `p_dotCount` means "dots across the screen": multiplying by
    // iAnchorSize/iSurfaceScreenPos.zw scales the count down to the
    // fraction of the screen the captured surface covers, so dot pitch
    // (in logical pixels) stays the same on popup vs. maximized windows
    // of a given display. The multiplier collapses to 1.0 when the
    // surface fills the screen. Floors guard against the
    // pre-first-frame (0,0) state of either uniform.
    vec2 dotsAcross = vec2(p_dotCount) * max(iAnchorSize, vec2(1.0))
                                     / max(iSurfaceScreenPos.zw, vec2(1.0));
    float reveal = step(distance(fract(uv * dotsAcross), vec2(0.5, 0.5)), p / max(distance(uv, center), 0.0001));

    return win * reveal;
}
