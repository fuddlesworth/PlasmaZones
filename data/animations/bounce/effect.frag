// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Bounce transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/bounce). Vertical
// bounce-down reveal — the surface drops in with bouncing oscillation.
//
// Niri's bounce ships symmetric close.glsl/open.glsl — bodies are
// identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required. Note: niri's open body LITERALLY
// starts with `p = 1.0 - niri_clamped_progress` — we keep that body
// verbatim and only translate `niri_clamped_progress` to
// `clamp(iTime, 0.0, 1.0)`, so the resulting line is
// `p = 1.0 - clamp(iTime, 0.0, 1.0)`. The leading `1.0 -` is part
// of the niri body, NOT a port-time inversion.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define bounces customParams[0].x

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = 1.0 - clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;
    float PI = 3.14159265358;

    float time = p;
    float stime = sin(time * PI / 2.0);
    float phase = time * PI * bounces;
    float yy = (abs(cos(phase))) * (1.0 - stime);
    float d = uv.y - yy;

    vec2 sample_uv = uv;
    sample_uv.y = uv.y + (1.0 - yy);
    vec4 win = texture(uTexture0, sample_uv);

    float reveal = step(d, 0.0);
    fragColor = win * reveal;
}
