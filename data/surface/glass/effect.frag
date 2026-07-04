// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glass pack, main pass: a refracting pane over the blurred backdrop
// (buffer 1). The lens model follows kwin-effects-glass: a rounded-rect SDF
// over the frame rect gives a signed distance d; a bevel profile
// (edgeFactor -> concaveFactor) rises from 0 in the pane's interior to 1 at
// the rim; the SDF gradient gives the bevel normal, and the blurred
// backdrop is sampled at per-channel offsets along that normal (red bends
// most, blue least) for a bevelled-lens look with colour fringing. A rim
// light and a tint sit on top, the pane is clipped to the rounded frame
// rect, and the window's own pixels composite over it.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the refracted backdrop.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pane degrades to a faint tint slab with the same corner rounding.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, min(halfSz.x, halfSz.y));
    float d = sdRoundedBox(px - center, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Bevel profile: 0 in the interior, rising over the last edgeWidth
        // px to 1 at the rim, shaped like a concave lens cross-section.
        float edgePx = max(p_edgeWidth * uSurfaceScale, 1.0);
        float edgeFactor = 1.0 - clamp(abs(d) / edgePx, 0.0, 1.0);
        float eased = smoothstep(0.0, 1.0, edgeFactor);
        float concave = 1.0 - sqrt(max(1.0 - eased * eased, 0.0));

        // Bevel normal from the SDF gradient (central differences).
        float h = 1.0;
        vec2 grad = vec2(sdRoundedBox(px - center + vec2(h, 0.0), halfSz, radius)
                             - sdRoundedBox(px - center - vec2(h, 0.0), halfSz, radius),
                         sdRoundedBox(px - center + vec2(0.0, h), halfSz, radius)
                             - sdRoundedBox(px - center - vec2(0.0, h), halfSz, radius));
        vec2 normal = length(grad) > 0.0 ? normalize(grad) : vec2(0.0, 1.0);

        // Per-channel refraction offsets in canvas UV. The reach scales with
        // the bevel depth (concave) and the bevel width, so a wider bevel
        // bends over a longer run. Red bends most, blue least.
        float reachPx = concave * clamp(p_refractionStrength, 0.0, 2.0) * 0.4 * edgePx;
        vec2 offsetUv = normal * (reachPx / max(uSurfaceSize, vec2(1.0)));
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.4;
        // vTexCoord is Y-up on the compositor while the offset is derived in
        // top-down pixel space, so flip its Y before applying.
        vec2 dirUv = vec2(offsetUv.x, -offsetUv.y);
        float r = texture(iChannel1, vTexCoord + dirUv * (1.0 + fringe)).r;
        vec4 g = texture(iChannel1, vTexCoord + dirUv);
        float b = texture(iChannel1, vTexCoord + dirUv * (1.0 - fringe)).b;
        vec3 refracted = vec3(r, g.g, b);

        // Rim light along the outermost band, focus-dimmed like the other
        // decoration packs so the unfocused pane sits back.
        float focusDim = mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));
        float rim = concave * concave * clamp(p_rimStrength, 0.0, 1.0) * focusDim;
        vec3 lit = mix(refracted, p_rimColor.rgb, rim);

        pane = vec4(mix(lit, tint * g.a, tintStrength), g.a) * mask;
    } else {
        pane = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    fragColor = window + pane * (1.0 - window.a);
}
