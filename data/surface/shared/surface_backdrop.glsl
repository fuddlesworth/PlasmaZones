// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in BACKDROP module for surface packs that sample the scene BEHIND the
// window (frost / glass / blur families). `#include <surface_backdrop.glsl>` in
// a pack that declares `"needsBackdrop": true`, then read backdropTexel().
//
// The kwin effect captures the scene under the window's (padded) canvas each
// frame and binds it here. A daemon or preview host has no scene to capture and
// binds the desktop wallpaper as a stand-in instead, or nothing at all, in which
// case backdropTexel() is transparent and a pack styles a fallback on the
// uHasBackdrop gate (which lives in the core contract,
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
#else
// Daemon branch: the stand-in for "the scene behind this surface" — the desktop
// wallpaper, where the host supplies one. Shares binding 11 with the overlay
// category's wallpaper sampler, which is the same texture from the same
// resolver, so no new binding point enters the dialect.
//
// The host keeps this binding populated even with nothing to show (a 1x1
// dummy), because a pack's SPIR-V declares the sampler on every host it runs
// on. Whether there is anything real here is uHasBackdrop's job, never the
// binding's presence.
//
// uBackdropRect means something DIFFERENT here, and it is a UBO member rather
// than a loose uniform (see surface_uniforms.glsl). The compositor's answers
// "which texels of this window's own capture are real", because a padded canvas
// can hang off an output and those texels are never blitted. The daemon's
// answers "which part of this shared image lies behind THIS surface", because
// every surface is handed the same desktop-sized wallpaper. Without it each one
// samples the whole desktop squeezed into its own box.
layout(binding = 11) uniform sampler2D uBackdrop;
#endif

// The scene texel BEHIND the surface at `uv` (the same uv space surfaceTexel
// takes). On a host that bound no backdrop this returns transparent, so gate
// styling on uHasBackdrop for an explicit fallback.
//
// A uv outside the surface's own slice is handled differently per runtime, and
// each is right for the data it holds. The compositor CLAMPS into the capture's
// valid sub-rect, because the canvas parts that fall off the output were never
// blitted and clamping is what stops an edge window smearing that cleared
// margin. The daemon does not, because its backdrop is a slice of the whole
// desktop wallpaper: the texels just outside the slice are the real wallpaper
// behind the neighbouring pixels, which is exactly what belongs there, and past
// the wallpaper's own border the sampler is ClampToEdge anyway.
//
// The two also guarantee the transparent-when-unbound result in different
// places: the daemon branch tests uHasBackdrop here, while the compositor
// relies on its host binding a 1x1 transparent fallback to the sampler.
vec4 backdropTexel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    vec2 td = vec2(uv.x, 1.0 - uv.y); // top-down normalized, like surfacePixel
    td = clamp(td, uBackdropRect.xy, uBackdropRect.xy + uBackdropRect.zw);
    return texture(uBackdrop, vec2(td.x, 1.0 - td.y));
#else
    // Transparent when nothing is bound, so a pack that samples without
    // checking uHasBackdrop first degrades to "no backdrop" rather than to the
    // dummy texel. Same shape as the old unconditional vec4(0.0), just now
    // reachable past the gate.
    if (uHasBackdrop < 0.5) {
        return vec4(0.0);
    }
    // Mapped through the surface's slice of the shared image, so a pack
    // refracts the part of the desktop actually behind it at the desktop's own
    // scale, instead of the whole thing squashed into this surface's box. The
    // host supplies (0, 0, 1, 1) when it has no placement to offer, which
    // reduces this to sampling the texture whole.
    //
    // No flip: the daemon delivers a Y-down vTexCoord against Qt-RHI's
    // top-origin texture, and the rect is expressed in that same top-down
    // space, so the wallpaper lands upright the way surfaceTexel does.
    return texture(uBackdrop, uBackdropRect.xy + uv * uBackdropRect.zw);
#endif
}

#endif // PLASMAZONES_SURFACE_BACKDROP_GLSL
