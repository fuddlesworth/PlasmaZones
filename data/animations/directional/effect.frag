// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Directional transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/directional). Linear
// directional slide — surface moves out of frame along a fixed direction.
//
// Niri's directional ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// Niri uniform shims (`niri_tex` → `uTexture0`; `niri_geo_to_tex` →
// identity mat3; `niri_random_seed` → `niri_random_seed_value()`) are
// provided by `<niri_compat.glsl>`. `texture2D` is rewritten to
// `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <niri_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// The niri body uses `sign(dir)`, so only the sign of dirX/dirY drives
// the slide direction — magnitude is irrelevant. Pick the sign you want
// on each axis (negative, zero, or positive); the slide picks one of
// nine cardinal/diagonal directions accordingly.
#define dirX customParams[0].x
#define dirY customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;
    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    vec2 dir = vec2(dirX, dirY);
    // Defend against (0, 0) which would silently break the leg
    // (`sign((0,0)) = (0,0)` → `q = uv` → `inside = 1` always → reveal
    // = 0 forever). Fall back to niri's hardcoded vertical-up default
    // when the user-supplied direction has no axis component.
    if (length(dir) < 1e-6) {
        dir = vec2(0.0, 1.0);
    }
    vec2 q = uv + p * sign(dir);
    float inside = step(0.0, q.y) * step(q.y, 1.0) * step(0.0, q.x) * step(q.x, 1.0);

    fragColor = win * (1.0 - inside);
}
