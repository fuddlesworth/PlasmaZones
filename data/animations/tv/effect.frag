// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// TV Effect — old-CRT power-off. The window squashes vertically
// toward its centre line, then collapses horizontally toward a
// point, then a brief white-flash fade completes the transition.
// Visually inspired by Burn-My-Windows (tv.frag, Simon Schneegans),
// written natively against our `iTime`/`uTexture0` contract.
//
// ## iTime convention
//
// `progress = easeOutQuad(1 - iTime)` — 0 at visible, 1 at
// fully-collapsed. The internal `easeOutQuad` is the BMW
// signature pacing for this effect (collapse starts fast, slows
// near the end as the line settles); keeping it inside the shader
// preserves the TV character regardless of which animation
// profile the user picks. Linear iTime → eased prog; users
// wanting BMW-exact pacing should pair this with a Linear profile.
//
// ## Three-stage mask
//
// • TB stage (0 → 0.7 of prog): the window collapses from top and
//   bottom edges toward a horizontal mid-line.
// • LR stage (0.6 → 1.0 of prog): the line collapses from left
//   and right edges toward the centre point.
// • Final fade (last 10% of prog): the centre point fades to
//   transparent through the flash colour.
//
// The vertical squash also scales texture lookup so the visible
// content compresses into the line — sampled UV outside [0,1]
// (top/bottom of the squashed strip) is forced transparent.

// metadata.json: color → customColors[0] (p_flashColor — alpha is the
// tint-mix gate so the user can dial in a fully transparent flash to
// disable the colour wash entirely).

const float BLUR_WIDTH = 0.01;
const float TB_TIME    = 0.7;
const float LR_TIME    = 0.4;
const float LR_DELAY   = 0.6;
const float FF_TIME    = 0.1;
const float SCALING    = 0.5;

// easeOutQuad hosted in shared/easing.glsl; the local copy collapsed to it.
#include <easing.glsl>

vec4 pTransition(vec2 uv, float t)
{
    float visibility = clamp(t, 0.0, 1.0);
    float prog       = easeOutQuad(1.0 - visibility);

    // Vertical squash: window content scales down toward a centre
    // line at uv.y=0.5. At prog=1 the texture lookup spans 2x the
    // window height, so the top/bottom halves sample off-window
    // (forced transparent below).
    float scale  = 1.0 / mix(1.0, SCALING, prog) - 1.0;
    vec2 coords  = uv;
    coords.y     = coords.y * (scale + 1.0) - scale * 0.5;

    // Per-stage progress. tb runs 0→1 over the first 70% of prog;
    // lr runs 0→1 over the 60%-100% slice; ff runs 0→1 in the last
    // 10%.
    float tbProg = smoothstep(0.0, 1.0, clamp(prog / TB_TIME, 0.0, 1.0));
    float lrProg = smoothstep(0.0, 1.0, clamp((prog - LR_DELAY) / LR_TIME, 0.0, 1.0));
    float ffProg = smoothstep(0.0, 1.0, clamp((prog - 1.0 + FF_TIME) / FF_TIME, 0.0, 1.0));

    // Top-centre-bottom gradient: 0 at edges, 1 at middle.
    float tb = coords.y * 2.0;
    tb       = tb < 1.0 ? tb : 2.0 - tb;

    // Left-centre-right gradient: 0 at edges, 1 at middle.
    float lr = coords.x * 2.0;
    lr       = lr < 1.0 ? lr : 2.0 - lr;

    // Mask falls off where the per-stage progress front passes the
    // gradient threshold. BLUR_WIDTH gives the "scan-line" softness.
    float tbMask = 1.0 - smoothstep(0.0, 1.0, clamp((tbProg - tb) / BLUR_WIDTH, 0.0, 1.0));
    float lrMask = 1.0 - smoothstep(0.0, 1.0, clamp((lrProg - lr) / BLUR_WIDTH, 0.0, 1.0));
    float ffMask = 1.0 - smoothstep(0.0, 1.0, ffProg);
    float mask   = tbMask * lrMask * ffMask;

    // Sample the squashed coordinate. coords.y can land outside
    // [0,1] for top/bottom of the squashed window — force those
    // transparent so the squash doesn't smear edge texels.
    vec2 inside    = step(vec2(0.0), coords) * step(coords, vec2(1.0));
    float onScreen = inside.x * inside.y;
    vec4 sampled   = surfaceColor(coords) * onScreen;

    // Tint toward the flash colour as the collapse progresses.
    // sampled is pre-multiplied; multiplying the flash colour by
    // sampled.a keeps the tint pre-multiplied-correct so transparent
    // window regions don't acquire a flash-colour halo.
    vec4 flash    = p_flashColor;
    float tintMix = flash.a * smoothstep(0.0, 1.0, prog);
    sampled.rgb   = mix(sampled.rgb, flash.rgb * sampled.a, tintMix);

    return sampled * mask;
}
