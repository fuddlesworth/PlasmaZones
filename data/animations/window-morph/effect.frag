// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window-morph fragment shader — shader-driven geometry move/resize.
//
// The window jumps to its destination instantly (moveResize); this shader
// animates the visual transition by interpolating the drawn rect from the
// OLD frame (iFromRect) to the NEW frame (iToRect) by iTime, cross-fading a
// snapshot of the old content (uOldWindow) out as the live new content
// (uTexture0, via surfaceColor) fades in. Both are sampled at the SAME
// normalised rect coordinate, so each is shown at its own native aspect —
// no non-uniform stretch like the C++ setXScale/setYScale path it replaces.
//
// Surface-extent shader: vTexCoord spans the output, iResolution is the
// output size, and iSurfaceScreenPos.xy is the output origin in global
// logical-screen pixels — the same space iFromRect/iToRect are pushed in
// (window frame geometry). So a fragment's global screen position is
// `iSurfaceScreenPos.xy + vTexCoord * iResolution`.
//
// NOTE (first cut): the exact coordinate mapping (iResolution vs output,
// iSurfaceScreenPos meaning, uOldWindow card sub-rect vs new) is pending
// on-hardware validation — the morph runs compositor-side only and can't be
// verified headlessly.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#ifdef PLASMAZONES_KWIN
// Geometry-morph endpoints (logical-screen px, x/y/w/h) + old-content
// snapshot. Default-block uniforms pushed by the kwin-effect paint pipeline.
uniform vec4 iFromRect;
uniform vec4 iToRect;
uniform sampler2D uOldWindow;
#endif

#include <anchor_remap.glsl>

#ifdef PLASMAZONES_KWIN
// Sample the captured OLD content at card-space uv, mirroring surfaceColor's
// iAnchorRectInTexture fold + KWin Y-up flip + iWindowOpacity multiply so old
// and new align AND a SetOpacity window rule dims both equally through the
// morph (surfaceColor folds iWindowOpacity for the new content; match it here).
vec4 oldColor(vec2 uv) {
    vec2 t = iAnchorRectInTexture.xy + uv * iAnchorRectInTexture.zw;
    return texture(uOldWindow, vec2(t.x, 1.0 - t.y)) * iWindowOpacity;
}
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    float t = clamp(iTime, 0.0, 1.0);

    // Fragment's global logical-screen position.
    vec2 screenPx = iSurfaceScreenPos.xy + vTexCoord * max(iResolution, vec2(1.0));

    // Interpolated rect (old -> new), then normalise the fragment into it.
    vec4 rect = mix(iFromRect, iToRect, t);
    vec2 uv = (screenPx - rect.xy) / max(rect.zw, vec2(1.0));

    // Outside the morphing rect: nothing to draw. Small feather to avoid a
    // hard edge as the rect sweeps.
    vec2 fw = max(fwidth(uv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, uv), smoothstep(vec2(0.0), fw, 1.0 - uv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 oldC = oldColor(uv);            // captured old content, native aspect
    vec4 newC = surfaceColor(uv);        // live new content, native aspect

    // Cross-fade old -> new across the morph. Inputs are premultiplied
    // (KWin FBO storage); a straight mix of premultiplied colours is correct.
    fragColor = mix(oldC, newC, t) * mask;
#else
    // Daemon path: morph is compositor-only. Render the surface unchanged so
    // the shader bakes for the daemon target and is harmless if ever run.
    fragColor = surfaceColor(anchorRemap(vTexCoord));
#endif
}
