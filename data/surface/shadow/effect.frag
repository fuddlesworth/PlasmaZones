// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Drop-shadow surface shader — the glow pack's analytic rounded-rect SDF
// falloff, recast as a proper window shadow: dark instead of coloured,
// displaced by a configurable offset so it reads as light coming from
// above, and only mildly focus-dimmed (a real shadow does not vanish when
// the window loses focus, it just softens). The content passes through
// byte-for-byte; the shadow lives purely in the transparent margin.
//
// CAPTURE MARGIN: metadata declares `"paddingParam": "shadowSize"`, so the
// compositor host inflates the capture canvas by the resolved size. The
// offset displaces the shadow within that margin; the texture-edge feather
// below keeps a large offset from cutting off in a hard rectangle at the
// canvas boundary. Static (no iTime).

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 base = surfaceTexel(vTexCoord);

    // Degenerate frame guard — mirrors glow/effect.frag: an unwired frame
    // rect would bleed shadow over the whole surface for a transient frame.
    if (surfaceFrameDegenerate()) {
        fragColor = base;
        return;
    }

    // Rounded-rect SDF over the frame rect DISPLACED by the cast offset:
    // evaluating the fragment against the shifted frame moves the whole
    // shadow body down/right, the classic dropped look.
    vec2 offset = vec2(p_offsetX, p_offsetY) * uSurfaceScale;
    vec2 p = surfacePixel(vTexCoord) - offset;
    FrameSDF fs = frameSdf(p, p_cornerRadius * uSurfaceScale);
    float d = fs.d;

    // Gaussian-profile falloff outward from the (shifted) edge — same
    // exp(-4t²) profile as the glow pack, brightest against the window and
    // effectively gone by the reach distance.
    float reach = max(p_shadowSize * uSurfaceScale, 1.0);
    float t = max(d, 0.0) / reach;
    float body = exp(-4.0 * t * t);

    // Never clip at the capture boundary: feather to zero just inside the
    // texture edge (evaluated at the REAL fragment position, not the shifted
    // one) so an offset pushing the shadow toward the canvas edge fades out
    // instead of ending in a hard rectangle.
    vec2 rp = surfacePixel(vTexCoord);
    float edgeDist = min(min(rp.x, rp.y), min(uSurfaceSize.x - rp.x, uSurfaceSize.y - rp.y));
    body *= smoothstep(0.0, min(0.35 * reach, 12.0 * uSurfaceScale), edgeDist);

    // Outer-only: confined to where the surface is transparent, so content
    // and the AA rim pass through untouched. Mild focus softening — a real
    // shadow persists on unfocused windows, it just lightens a little.
    body *= (1.0 - base.a);
    body *= p_shadowStrength * focusDim(0.65);

    // Premultiplied over: the dark veil fills the margin under its own
    // alpha; with the default black colour the rgb term contributes nothing
    // and the shadow reads as pure darkening of whatever is behind.
    float sa = clamp(body * p_shadowColor.a, 0.0, 1.0);
    fragColor = marginComposite(base, p_shadowColor.rgb, sa);
}
