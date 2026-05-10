// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Snap transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/snap). Thanos-style
// snap — the surface shatters into 10 randomized layers that fly toward
// a vanishing point. Asymmetric direction and re-form curves.
//
// Niri's snap ships asymmetric close.glsl/open.glsl — close blows
// pixels OUT toward `target = vec2(1.0, 0.0)` driven by `p` (loop
// progresses 0→1), open snaps pixels BACK FROM target driven by
// `rp = 1.0 - p` (loop progresses 1→0). Branch on iIsReversed;
// PlasmaZones flips iTime on the close leg, so the niri close branch's
// `p = niri_clamped_progress` becomes `p = 1.0 - clamp(iTime, 0.0, 1.0)`
// and open's `p = niri_clamped_progress` becomes
// `p = clamp(iTime, 0.0, 1.0)` (per translation rules).
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. niri's
// `niri_random_seed` is replaced by `surfaceSeed()` from `<noise.glsl>`.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots. Both
// iIsReversed branches share the same params. `layerSpread` controls
// the per-layer x-jitter range (default 0.16 reproduces niri's
// `-0.08 + lh * 0.16`, with the y range scaled to half to preserve
// niri's 2:1 x:y ratio). The 10-iteration loop bound is intentionally
// kept as a literal — GLSL requires the for-loop bound to be a constant
// expression and the matching `floor(... * num_layers)` step needs to
// agree, so `num_layers` cannot be exposed as a runtime parameter.
#define targetX      customParams[0].x
#define targetY      customParams[0].y
#define layerSpread  customParams[0].z
#define layerStagger customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sn_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec4 result;
    if (iIsReversed != 0) {
        // ── niri close.glsl body ──
        float p = 1.0 - clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 100.0;

        float num_layers = 10.0;
        float pixel_layer = floor(sn_hash(floor(uv * max(iAnchorSize, vec2(1.0))) + seed) * num_layers);

        vec4 inner = vec4(0.0);
        vec2 target = vec2(targetX, targetY);

        for (int i = 0; i < 10; i++) {
            float layer = float(i);
            float layer_delay = layer * layerStagger;
            float layer_p = clamp((p - layer_delay) / (1.0 - layer_delay * 0.5), 0.0, 1.0);

            float t = layer_p * layer_p;

            float layer_alpha = 1.0 - smoothstep(0.3, 0.85, layer_p);

            float lh = sn_hash(vec2(layer + 0.5, seed));
            vec2 layer_target = target + vec2((-0.5 + lh) * layerSpread, (-0.5 + lh) * layerSpread * 0.5);

            float converge = t * 0.92;
            vec2 sample_uv = (uv - layer_target * converge) / (1.0 - converge);

            // Boundary mask — at peak `converge=0.92` the divide stretches
            // sample_uv up to 12.5×, easily reaching off-window pixels
            // (rounded corners, shadows) which sample as alpha=0. That makes
            // the layer's contribution vanish exactly where it should be
            // visible. Same soft-mask pattern morph/effect.frag uses to fade
            // out-of-bounds samples to transparent without a hard 1-texel
            // discontinuity.
            vec2 insideLo = smoothstep(vec2(-0.005), vec2(0.0), sample_uv);
            vec2 insideHi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.005), sample_uv);
            float boundsMask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;

            vec4 color = texture(uTexture0, sample_uv) * boundsMask;

            float belongs = step(abs(pixel_layer - layer), 0.5);
            inner += color * belongs * layer_alpha;
        }

        float initial_fade = smoothstep(0.0, 0.05, p);
        inner.a *= mix(1.0, 0.0, initial_fade);
        vec4 base_color = texture(uTexture0, uv);
        float base_alpha = 1.0 - smoothstep(0.0, 0.1, p);

        result = base_color * base_alpha + inner * (1.0 - base_alpha);
    } else {
        // ── niri open.glsl body ──
        float p = clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 100.0;
        float rp = 1.0 - p;

        float num_layers = 10.0;
        float pixel_layer = floor(sn_hash(floor(uv * max(iAnchorSize, vec2(1.0))) + seed) * num_layers);

        vec4 inner = vec4(0.0);
        vec2 target = vec2(targetX, targetY);

        for (int i = 0; i < 10; i++) {
            float layer = float(i);
            float layer_delay = layer * layerStagger;
            float layer_p = clamp((rp - layer_delay) / (1.0 - layer_delay * 0.5), 0.0, 1.0);

            float t = layer_p * layer_p;

            float layer_alpha = 1.0 - smoothstep(0.3, 0.85, layer_p);

            float lh = sn_hash(vec2(layer + 0.5, seed));
            vec2 layer_target = target + vec2((-0.5 + lh) * layerSpread, (-0.5 + lh) * layerSpread * 0.5);

            float converge = t * 0.92;
            vec2 sample_uv = (uv - layer_target * converge) / (1.0 - converge);

            // Boundary mask — see close-leg branch above for rationale.
            // Same divide produces the same UV stretch on the open leg.
            vec2 insideLo = smoothstep(vec2(-0.005), vec2(0.0), sample_uv);
            vec2 insideHi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.005), sample_uv);
            float boundsMask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;

            vec4 color = texture(uTexture0, sample_uv) * boundsMask;

            float belongs = step(abs(pixel_layer - layer), 0.5);
            inner += color * belongs * layer_alpha;
        }

        float initial_form = smoothstep(0.0, 0.05, rp);
        inner.a *= mix(1.0, 0.0, initial_form);
        vec4 base_color = texture(uTexture0, uv);
        float base_alpha = 1.0 - smoothstep(0.0, 0.1, rp);

        result = base_color * base_alpha + inner * (1.0 - base_alpha);
    }
    fragColor = result;
}
