// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Bounce transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/bounce). The window
// drops in from above its frame with a decaying bouncing oscillation.
//
// Niri's bounce ships symmetric close.glsl/open.glsl — bodies are
// identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we adapt the niri OPEN body and the
// runtime flip auto-mirrors the visual on close. No `iIsReversed`
// branch required. Note: niri's open body LITERALLY starts with
// `p = 1.0 - niri_clamped_progress`; we keep that and only translate
// `niri_clamped_progress` to `clamp(iTime, 0.0, 1.0)`, so the line is
// `p = 1.0 - clamp(iTime, 0.0, 1.0)`. The leading `1.0 -` is part of
// the niri body, NOT a port-time inversion.
//
// SURFACE EXTENT — niri's bounce reads `uv` from `coords_geo`, the
// GEOMETRY box (larger than the window texture), so the window can
// genuinely travel above its frame. This is a `fboExtent: "surface"`
// shader: the runtime sizes the render target to the whole surface
// (the daemon's scene root, or the window's output under the kwin-
// effect's apply() override). `anchorRemap` (see anchor_remap.glsl)
// converts each fragment's surface-UV into the window's own [0,1]
// space; the window is then rigidly translated for the drop and
// `boundaryMask` crops the part still off the frame.
//
// niri's open body also clips the drop with `reveal = step(uv.y - yy,
// 0.0)`. That clip is redundant here — the rigid translate plus
// `boundaryMask` already crop the surface to the moving window — so it
// is dropped. `texture2D` (GLSL ES) is rewritten to `texture` (GLSL
// 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>
#include <anchor_remap.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define bounces customParams[0].x

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body, adapted for the surface-extent model ──
    float p = 1.0 - clamp(iTime, 0.0, 1.0);
    // vTexCoord spans the whole surface; map it into the window's
    // ("anchor") own [0,1] space. Fragments outside the window map
    // outside [0,1] and are cropped by boundaryMask below.
    vec2 uv = anchorRemap(vTexCoord);
    float PI = 3.14159265358;

    float time = p;
    float stime = sin(time * PI / 2.0);
    float phase = time * PI * bounces;
    float yy = (abs(cos(phase))) * (1.0 - stime);

    // Rigid vertical translate in anchor space: the window drops in as
    // one unit from above its frame, its top edge at anchor-y (yy - 1).
    // The offset (1 - yy) swings with the bounce and settles to 0 as
    // `yy` reaches 1 (identity sampling). Because the shader paints the
    // whole surface, the window genuinely travels above the frame
    // rather than being revealed within it. boundaryMask (see
    // noise.glsl) crops the part of the window still off the frame.
    vec2 sample_uv = uv;
    sample_uv.y = uv.y + (1.0 - yy);
    fragColor = surfaceColor(sample_uv) * boundaryMask(sample_uv);
}
