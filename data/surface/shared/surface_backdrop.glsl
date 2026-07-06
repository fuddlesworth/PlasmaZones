// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in BACKDROP module for surface packs that sample the scene BEHIND the
// window (frost / glass / blur families). `#include <surface_backdrop.glsl>` in
// a pack that declares `"needsBackdrop": true`, then read backdropTexel().
//
// Compositor-only capability: the kwin effect captures the scene under the
// window's (padded) canvas each frame and binds it here; the daemon has no
// scene behind its surfaces, so backdropTexel() is transparent there and a pack
// styles a fallback on the uHasBackdrop gate (which lives in the core contract,
// surface_uniforms.glsl, because it is a pinned UBO member on the daemon).
//
// Only the capture SAMPLER lives in this module. The gate scalar (uHasBackdrop)
// stays in surface_uniforms.glsl so a pack can branch on it without pulling in
// the sampler.

#ifndef PLASMAZONES_SURFACE_BACKDROP_GLSL
#define PLASMAZONES_SURFACE_BACKDROP_GLSL

#include <surface_uniforms.glsl>

#ifdef PLASMAZONES_KWIN
// The scene BEHIND the window over the same (padded) canvas as uTexture0.
// uBackdropRect is the VALID sub-rect of the capture in TOP-DOWN normalized
// coords (xy = min, zw = size): canvas parts that fall off the output are never
// blitted, so backdropTexel() clamps samples into this rect.
uniform sampler2D uBackdrop;
uniform vec4 uBackdropRect;
#endif

// The scene texel BEHIND the surface at `uv` (the same uv space surfaceTexel
// takes), clamped into the capture's valid sub-rect so edge windows never smear
// the cleared off-output margin. Compositor-only: the daemon returns transparent
// here, so gate styling on uHasBackdrop for an explicit fallback.
vec4 backdropTexel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    vec2 td = vec2(uv.x, 1.0 - uv.y); // top-down normalized, like surfacePixel
    td = clamp(td, uBackdropRect.xy, uBackdropRect.xy + uBackdropRect.zw);
    return texture(uBackdrop, vec2(td.x, 1.0 - td.y));
#else
    return vec4(0.0);
#endif
}

#endif // PLASMAZONES_SURFACE_BACKDROP_GLSL
