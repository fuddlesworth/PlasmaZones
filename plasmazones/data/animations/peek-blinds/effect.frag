// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Peek Blinds — the show-desktop peek as a set of venetian slats. The screen is
// split into slats that tilt open one after another, so the windows scene
// collapses to thin bands and the desktop shows through the gaps while the
// reveal sweeps across the slats. `t` is peek progress in [0,1]: the FROM
// texture is ALWAYS the scene with windows and the TO texture ALWAYS the bare
// desktop, on both legs. The kwin-effect reverses TIME rather than the textures
// — the hide leg drives t 0 → 1 and the show-back leg drives it 1 → 0 — so this
// shader only ever describes the windows-to-desktop direction and needs no
// reversal logic, and the show leg automatically retraces this motion (the
// slats tilt shut again). Run by the screen-level desktop-transition pass,
// which binds uFromDesktop and uToDesktop and pushes progress as iTime.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float tt = clamp(t, 0.0, 1.0);

    // Stack the slats along one axis. Vertical slats split across x.
    float axis = (p_vertical > 0.5) ? uv.x : uv.y;
    float count = max(p_slatCount, 2.0);

    float slatF = axis * count;
    float slat = floor(slatF);
    float local = fract(slatF);
    float slatNorm = slat / count;

    // Each slat opens a little later than the one before it, so the reveal
    // sweeps across the screen instead of every slat opening at once.
    float spread = 0.35;
    float o = smoothstep(0.0, 1.0, clamp(tt * (1.0 + spread) - slatNorm * spread, 0.0, 1.0));

    // Softness of the slat edge, in local slat units.
    float soft = 0.02 + clamp(p_softness, 0.0, 1.0) * 0.4;

    // The windows band fills the slat when closed and thins to nothing as the
    // slat opens. The threshold runs past both ends of the slat at o = 0 and
    // o = 1 so t = 0 is fully windows and t = 1 is fully desktop with no seam.
    float threshold = mix(1.0 + soft, -soft, o);
    float windowsMask = 1.0 - smoothstep(threshold - soft, threshold + soft, local);

    vec3 windows = getFromColor(uv).rgb;
    vec3 desktop = getToColor(uv).rgb;
    vec3 col = mix(desktop, windows, windowsMask);

    // A thin highlight rides the opening edge of each slat for a little depth.
    // It is gated by o * (1 - o), so it is absent at both endpoints and leaves
    // no residue on the settled frames.
    float edge = 1.0 - smoothstep(0.0, soft * 1.5, abs(local - threshold));
    col += vec3(edge) * o * (1.0 - o) * clamp(p_softness, 0.0, 1.0) * 0.25;

    // Two opaque scenes blended stay opaque; the pass draws with blending off
    // and replaces the screen, so alpha is a constant 1. NO bound on the rgb:
    // the blend is convex and the edge highlight only ever ADDS a non-negative
    // term (edge, o·(1−o) and the softness factor are all ≥ 0), so nothing here
    // can push below the captures. Any clamp or max would only clip legitimate
    // out-of-range capture values (HDR above 1.0, wide-gamut scRGB below 0),
    // including at the exact endpoints. Nothing runs after this return to
    // normalise it: the desktop pass keeps PZ_FINALIZE_COLOR at its identity
    // default, because the capture FBOs already inherit the output's
    // colorDescription and converting again would double-transform (see the
    // kFinalizeColorBlock note in shader_textures.cpp).
    return vec4(col, 1.0);
}
