// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Peek Recede — the show-desktop peek. The windows scene shrinks toward a
// configurable centre and fades out while it darkens, so it recedes into depth
// and leaves the bare desktop behind it. `t` is forward peek progress in [0,1]:
// the FROM texture is the scene with windows, the TO texture is the bare
// desktop. The kwin-effect swaps the two textures for the show-back leg, so
// this shader only ever animates from the windows scene toward the desktop and
// needs no direction or reversal logic. Run by the screen-level
// desktop-transition pass, which binds uFromDesktop and uToDesktop and pushes
// progress as iTime.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    float tt = clamp(t, 0.0, 1.0);
    float e = smoothstep(0.0, 1.0, tt);

    vec2 center = vec2(clamp(p_centerX, 0.0, 1.0), clamp(p_centerY, 0.0, 1.0));

    // The windows layer scales from full size down to p_recede of the screen.
    float minScale = clamp(p_recede, 0.05, 0.95);
    float scale = mix(1.0, minScale, e);

    // Coordinate inside the shrinking windows image. At t = 0 scale is 1 and
    // this is exactly uv, so the layer covers the screen; as it shrinks the
    // content coordinate runs outside [0,1] near the edges and those fragments
    // fall through to the desktop.
    vec2 contentUV = center + (uv - center) / max(scale, 1.0e-3);

    // Soft rectangular mask: 1 inside the receded windows image, 0 outside.
    // Both smoothstep calls keep edge0 < edge1 (undefined otherwise), so the
    // upper bound is written as one minus a rising step rather than reversed
    // edges.
    float soft = 0.01;
    vec2 inside = smoothstep(vec2(-soft), vec2(soft), contentUV)
                * (vec2(1.0) - smoothstep(vec2(1.0 - soft), vec2(1.0 + soft), contentUV));
    float mask = inside.x * inside.y;

    // The windows fade out as they recede and are gone by t = 1.
    float fade = smoothstep(0.55, 1.0, tt);
    float windowsAlpha = mask * (1.0 - fade);
    // Force a full windows cover at the very start so t = 0 is exactly the
    // windows scene with no edge seam from the mask smoothstep.
    windowsAlpha = max(windowsAlpha, 1.0 - smoothstep(0.0, 0.02, tt));

    // Darken the receding windows toward the back so the depth reads.
    float dim = mix(1.0, 1.0 - clamp(p_dim, 0.0, 1.0), e);
    vec3 windows = getFromColor(contentUV).rgb * dim;
    vec3 desktop = getToColor(uv).rgb;

    vec3 col = mix(desktop, windows, windowsAlpha);

    // Two opaque scenes blended stay opaque; the pass draws with blending off
    // and replaces the screen, so alpha is a constant 1.
    return vec4(clamp(col, 0.0, 1.0), 1.0);
#else
    // Desktop transitions are compositor-only; the daemon never runs them.
    // Return transparent so the pack still bakes for the daemon target.
    return vec4(0.0);
#endif
}
