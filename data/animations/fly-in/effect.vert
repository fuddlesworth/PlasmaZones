// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fly-in vertex shader — translates the surface horizontally on enter so it
// appears to fly in from the screen edge nearest to its current resting
// position. The "closest horizontal edge" decision is made per-frame from
// `iSurfaceScreenPos` (.xy = surface origin on screen, .zw = screen size):
//   • surfaceCenterX = iSurfaceScreenPos.x + iResolution.x * 0.5
//   • leftDist  = surfaceCenterX
//   • rightDist = iSurfaceScreenPos.z - surfaceCenterX
//   • sign = (leftDist < rightDist) ? -1 (fly from left) : +1 (fly from right)
//
// The translation distance is enough to push the surface fully off the
// nearer screen edge at iTime=0 and shrinks linearly to zero at iTime=1.
// This is the canonical "vertex shader exercises iSurfaceScreenPos"
// reference: any fragment shader can keep doing its own thing in parallel.
//
// Coordinate-space split: the daemon RHI path receives `position` already
// in clip space (-1..1) and applies no MVP. The kwin-effect path receives
// `position` in window-frame logical pixels and multiplies by
// `modelViewProjectionMatrix`. iSurfaceScreenPos is in logical-screen
// pixels on both paths, so the translation we apply must be in the right
// units for each. Branched via PLASMAZONES_KWIN, the macro the kwin path
// injects after #version.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
    // texCoord — kwin's Y is up in the FBO, so flip; daemon's Qt scene
    // coords already have Y down. Mirrors `kKwinDefaultVertexSource` /
    // `kDefaultVertexShaderSource` from the C++ side.
#ifdef PLASMAZONES_KWIN
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    vTexCoord = texCoord;
#endif

    // Horizontal translation amount, monotonically reducing 1 → 0 over
    // iTime. Clamp because bouncy curves overshoot iTime past 1 — we
    // already clamp at the C++ source on the daemon side, but a hide
    // leg flipping iTime 1→0 can also dip negative on overshoot.
    float t = clamp(iTime, 0.0, 1.0);
    float remaining = 1.0 - t;

    // Decide direction from surface position on its host screen.
    // Default to "fly from left" when iSurfaceScreenPos hasn't been
    // populated yet (.zw = 0 means the runtime didn't push a screen
    // size — typically the first frame on the daemon path before the
    // window's compositor configure). This keeps the shader visible
    // and recognisable rather than producing a NaN division below.
    float screenW = max(iSurfaceScreenPos.z, 1.0);
    float surfaceCenterX = iSurfaceScreenPos.x + iResolution.x * 0.5;
    float leftDist = surfaceCenterX;
    float rightDist = screenW - surfaceCenterX;
    float dirSign = (leftDist < rightDist) ? -1.0 : 1.0;

    // Distance to push — full clearance off the near edge. For "fly
    // from left", that's surfaceX + iResolution.x worth of leftward
    // travel (so the right edge of the surface clears the screen
    // origin). For "fly from right", surfaceX' = screenW - (surfaceX
    // + iResolution.x), so push that-many pixels rightward.
    float clearancePx = (dirSign < 0.0)
        ? max(iSurfaceScreenPos.x + iResolution.x, 1.0)
        : max(screenW - iSurfaceScreenPos.x, 1.0);
    float offsetPx = dirSign * remaining * clearancePx;

#ifdef PLASMAZONES_KWIN
    // kwin: position is in window-frame logical pixels; add the offset
    // directly, then run through the MVP matrix.
    vec2 shifted = position + vec2(offsetPx, 0.0);
    gl_Position = modelViewProjectionMatrix * vec4(shifted, 0.0, 1.0);
#else
    // daemon: position is in clip space (-1..1). The surface spans
    // iResolution.x logical pixels mapped to a clip-space width of 2.0,
    // so 1 pixel = 2 / iResolution.x clip units. Guard against a zero
    // resolution which would only happen pre-attach.
    float pxToClip = (iResolution.x > 0.0) ? (2.0 / iResolution.x) : 0.0;
    vec2 shifted = position + vec2(offsetPx * pxToClip, 0.0);
    gl_Position = vec4(shifted, 0.0, 1.0);
#endif
}
