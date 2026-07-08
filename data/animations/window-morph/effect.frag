// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
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
// Surface-extent shader: apply() lays an output-spanning quad, so vTexCoord
// spans the host output and iResolution is the output size. iFromRect/iToRect
// are pushed in GLOBAL logical-screen pixels (window frame geometry), so a
// fragment's global screen position is reconstructed from the OUTPUT origin:
//   outputOrigin = iSurfaceScreenPos.xy - iAnchorPosInFbo
// iSurfaceScreenPos.xy is the window/surface origin and iAnchorPosInFbo is the
// window's top-left offset within that output, so their difference is the
// output's global top-left. screenPx = outputOrigin + vTexCoord * iResolution.
//
// The old-content snapshot (uOldWindow) is captured at the window's CURRENT
// (post-moveResize) expanded geometry, so iAnchorRectInTexture — the new
// frame's sub-rect within the new expanded texture — maps card-space uv into
// it correctly, exactly as it does for the live uTexture0.

#ifdef PLASMAZONES_KWIN
// Geometry-morph endpoints (logical-screen px, x/y/w/h). Default-block
// uniforms pushed by the kwin-effect paint pipeline. The old-content snapshot
// (uOldWindow) comes from the shared old_content.glsl include below.
uniform vec4 iFromRect;
uniform vec4 iToRect;
#endif

#include <anchor_remap.glsl>

// uOldWindow + oldColor(): the shared captured-old-frame sampler.
#include <old_content.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    t = clamp(t, 0.0, 1.0);

    // Fragment's global logical-screen position. Reconstruct from the OUTPUT
    // origin (iSurfaceScreenPos.xy is the window origin; subtracting the
    // window's in-output offset iAnchorPosInFbo yields the output's top-left).
    vec2 outputOrigin = iSurfaceScreenPos.xy - iAnchorPosInFbo;
    vec2 screenPx = outputOrigin + uv * max(iResolution, vec2(1.0));

    // Interpolated rect (old -> new), then normalise the fragment into it.
    vec4 rect = mix(iFromRect, iToRect, t);
    vec2 ruv = (screenPx - rect.xy) / max(rect.zw, vec2(1.0));

    // Outside the morphing rect: nothing to draw. Small feather to avoid a
    // hard edge as the rect sweeps.
    vec2 fw = max(fwidth(ruv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, ruv), smoothstep(vec2(0.0), fw, 1.0 - ruv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 oldC = oldColor(ruv);           // captured old content, native aspect
    vec4 newC = surfaceColor(ruv);       // live new content, native aspect

    // Cross-fade old -> new across the morph. Inputs are premultiplied
    // (KWin FBO storage); a straight mix of premultiplied colours is correct.
    return mix(oldC, newC, t) * mask;
#else
    // Daemon path: morph is compositor-only. Render the surface unchanged so
    // the shader bakes for the daemon target and is harmless if ever run.
    return surfaceColor(anchorRemap(uv));
#endif
}
