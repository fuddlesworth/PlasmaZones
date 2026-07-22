// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Peek Recede — the show-desktop peek. The windows scene shrinks toward a
// configurable centre and fades out while it darkens, so it recedes into depth
// and leaves the bare desktop behind it. `t` is peek progress in [0,1]: the FROM
// texture is ALWAYS the scene with windows and the TO texture ALWAYS the bare
// desktop, on both legs. The kwin-effect reverses TIME rather than the textures
// — the hide leg drives t 0 → 1 and the show-back leg drives it 1 → 0 — so this
// shader only ever describes the windows-to-desktop direction and needs no
// reversal logic, and the show leg automatically retraces this motion (the
// windows grow back out of depth). Run by the screen-level desktop-transition
// pass, which binds uFromDesktop and uToDesktop and pushes progress as iTime.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float tt = clamp(t, 0.0, 1.0);
    float e = smoothstep(0.0, 1.0, tt);

    vec2 center = vec2(clamp(p_centerX, 0.0, 1.0), clamp(p_centerY, 0.0, 1.0));

    // The windows layer scales from full size down to p_recede of the screen.
    float minScale = clamp(p_recede, 0.05, 0.95);
    float scale = mix(1.0, minScale, e);

    // Coordinate inside the shrinking windows image. At t = 0 scale is 1 and
    // this is exactly uv, so the layer covers the screen; as it shrinks the
    // content coordinate runs outside [0,1] near the edges and those fragments
    // fall through to the desktop. scale >= 0.05 by the clamp above, so the
    // division is always safe.
    vec2 contentUV = center + (uv - center) / scale;

    // Soft rectangular mask: 1 inside the receded windows image, 0 outside.
    // Both smoothstep calls keep edge0 < edge1 (undefined otherwise), so the
    // upper bound is written as one minus a rising step rather than reversed
    // edges.
    //
    // The feather is divided by scale because the mask is evaluated in CONTENT
    // space, which the shrink expands by 1/scale: a constant content-space
    // feather would come out as `soft * scale` on screen and collapse to
    // sub-pixel at a strong recede (0.5 px at the 0.05 clamp floor on a 1080p
    // output), aliasing the edge into a hard jagged rectangle. Dividing holds
    // the feather at a constant ~0.01 of the screen for every recede depth. At
    // t = 0 scale is 1, so this is the same 0.01 the endpoint reasoning below
    // assumes.
    float soft = 0.01 / scale;
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
    // and replaces the screen, so alpha is a constant 1. NO clamp on the rgb:
    // a pure blend of the two captures cannot exceed their range, and an HDR
    // capture legitimately carries values above 1.0 that a pre-clamp would
    // crush at the endpoints. Nothing runs after this return to normalise it:
    // the desktop pass keeps PZ_FINALIZE_COLOR at its identity default,
    // because the capture FBOs already inherit the output's colorDescription
    // and converting again would double-transform (see the kFinalizeColorBlock
    // note in shader_transitions.cpp). What this returns is written out as-is.
    return vec4(col, 1.0);
}
