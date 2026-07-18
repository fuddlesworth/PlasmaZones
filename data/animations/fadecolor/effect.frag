// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fade Color transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/fadecolor). Phased
// fade — visibility ramps from `p_colorPhase` onward with smoothstep.
//
// Deviation from niri: the phase DEFAULT is 0.0, not niri's hardcoded
// 0.4. `reveal = smoothstep(phase, 1.0, p)` draws literally nothing
// before p reaches the phase — at 0.4 the first 40% of an open leg is a
// fully invisible window (and the last 40% of a close leg an invisible
// corpse), which an eased progress curve stretches into perceived
// launch lag (the phosphor-peek dead-domain bug). Raising the dial back
// to 0.4 reproduces the niri timing exactly.
//
// Niri's fadecolor ships symmetric close.glsl/open.glsl — bodies are
// identical apart from `p = niri_clamped_progress` vs
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

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_colorPhase (customParams[0].x) is generated from metadata.json — no
// hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the niri OPEN body auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);

    vec4 win = surfaceColor(uv);

    float reveal = smoothstep(p_colorPhase, 1.0, p);
    return win * reveal;
}
