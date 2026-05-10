// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fade transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/fade). Gentle scale-
// and-fade transition. Open eases scale 0.95→1.0 with smoothstep(0,0.8)
// fade-in; close eases scale 1.0→0.95 with smoothstep(1.0,0.2)
// fade-out.
//
// Niri's fade ships GENUINELY ASYMMETRIC close.glsl/open.glsl — open
// uses `mix(0.95, 1.0, p)` for scale and `smoothstep(0.0, 0.8, p)` for
// alpha, while close uses `mix(1.0, 0.95, p)` for scale and
// `smoothstep(1.0, 0.2, p)` for alpha. These are different curves,
// not a simple time-reversal, so the iTime flip alone can't express
// both legs. We branch on `iIsReversed` to select the correct body.
//
// Per the contract, on the close leg PlasmaZones flips iTime so it
// runs 1→0; the niri close.glsl reads `niri_clamped_progress`
// directly (no inversion in the source), so its translation in the
// reversed branch becomes `(1.0 - clamp(iTime, 0.0, 1.0))` — that
// recovers the ABSOLUTE leg progress in [0,1] running 0→1 across the
// close leg's wall-clock time, which is what the niri close.glsl body
// was authored to consume.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define scaleAmount  customParams[0].x
#define revealStart  customParams[0].y
#define revealEnd    customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 result;
    if (iIsReversed != 0) {
        // ── niri close.glsl body ──
        // close-leg p = niri_clamped_progress; iTime is flipped to
        // 1→0 on the close leg, so absolute leg progress in [0,1] is
        // (1.0 - clamp(iTime, 0.0, 1.0)).
        float p = 1.0 - clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0, 1.0 - scaleAmount, p);
        vec2 scaled_uv = (uv - center) / scale + center;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = texture(uTexture0, scaled_uv) * boundaryMask(scaled_uv);

        float alpha = smoothstep(1.0 - revealStart, 1.0 - revealEnd, p);

        result = color * alpha;
    } else {
        // ── niri open.glsl body ──
        // open-leg p = niri_clamped_progress; iTime runs 0→1 forward.
        float p = clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0 - scaleAmount, 1.0, p);
        vec2 scaled_uv = (uv - center) / scale + center;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = texture(uTexture0, scaled_uv) * boundaryMask(scaled_uv);

        float alpha = smoothstep(revealStart, revealEnd, p);

        result = color * alpha;
    }
    fragColor = result;
}
