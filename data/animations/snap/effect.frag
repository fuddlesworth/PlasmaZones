// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Snap transition — a Thanos-style snap where the surface shatters into
// 10 randomized layers that fly toward a vanishing point, with asymmetric
// direction and re-form curves. Inspired by liixini/shaders' niri snap
// shader.
//
// The legs are asymmetric — close blows pixels OUT toward
// `target = vec2(1.0, 0.0)` driven by `p` (loop progresses 0→1), open
// snaps pixels BACK FROM target driven by `rp = 1.0 - p` (loop progresses
// 1→0). This is a pIn/pOut pair: the harness feeds forward 0→1 `t` to both
// legs (so each branch's `p` is just `t`) and dispatches the matching body
// by leg direction (`windowFadingIn`).
//
// Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly, and per-instance variation
// comes from `surfaceSeed()` in `<noise.glsl>`.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl is pack-specific, so it stays here.
#include <noise.glsl>

// p_targetX / p_targetY / p_layerSpread / p_layerStagger
// (customParams[0].xyzw) are generated from metadata.json. Both legs share
// the same params. `p_layerSpread` controls the per-layer x-jitter range
// (default 0.16 gives a `-0.08 + lh * 0.16` x-jitter, y scaled to half for
// a 2:1 x:y ratio). The 10-iteration loop bound stays a literal — GLSL
// requires a constant for-loop bound and the matching `floor(... * num_layers)`
// step must agree, so num_layers cannot be a runtime parameter.

// `uv` is vTexCoord; `t` is the forward 0→1 leg progress (the harness applies
// legProgress()); `windowFadingIn` selects the open vs close body. The
// per-layer convergence ease uses `tt` (named to avoid shadowing `t`).
vec4 snapBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── close-leg body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 100.0;

        float num_layers = 10.0;
        float pixel_layer = floor(niriHash(floor(uv * max(iAnchorSize, vec2(1.0))) + seed) * num_layers);

        vec4 inner = vec4(0.0);
        vec2 target = vec2(p_targetX, p_targetY);

        for (int i = 0; i < 10; i++) {
            float layer = float(i);
            float layer_delay = layer * p_layerStagger;
            float layer_p = clamp((p - layer_delay) / (1.0 - layer_delay * 0.5), 0.0, 1.0);

            float tt = layer_p * layer_p;

            float layer_alpha = 1.0 - smoothstep(0.3, 0.85, layer_p);

            float lh = niriHash(vec2(layer + 0.5, seed));
            vec2 layer_target = target + vec2((-0.5 + lh) * p_layerSpread, (-0.5 + lh) * p_layerSpread * 0.5);

            float converge = tt * 0.92;
            vec2 sample_uv = (uv - layer_target * converge) / (1.0 - converge);

            // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
            vec4 color = surfaceColor(sample_uv) * boundaryMask(sample_uv);

            float belongs = step(abs(pixel_layer - layer), 0.5);
            inner += color * belongs * layer_alpha;
        }

        float initial_fade = smoothstep(0.0, 0.05, p);
        inner.a *= mix(1.0, 0.0, initial_fade);
        vec4 base_color = surfaceColor(uv);
        float base_alpha = 1.0 - smoothstep(0.0, 0.1, p);

        result = base_color * base_alpha + inner * (1.0 - base_alpha);
    } else {
        // ── open-leg body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 100.0;
        float rp = 1.0 - p;

        float num_layers = 10.0;
        float pixel_layer = floor(niriHash(floor(uv * max(iAnchorSize, vec2(1.0))) + seed) * num_layers);

        vec4 inner = vec4(0.0);
        vec2 target = vec2(p_targetX, p_targetY);

        for (int i = 0; i < 10; i++) {
            float layer = float(i);
            float layer_delay = layer * p_layerStagger;
            float layer_p = clamp((rp - layer_delay) / (1.0 - layer_delay * 0.5), 0.0, 1.0);

            float tt = layer_p * layer_p;

            float layer_alpha = 1.0 - smoothstep(0.3, 0.85, layer_p);

            float lh = niriHash(vec2(layer + 0.5, seed));
            vec2 layer_target = target + vec2((-0.5 + lh) * p_layerSpread, (-0.5 + lh) * p_layerSpread * 0.5);

            float converge = tt * 0.92;
            vec2 sample_uv = (uv - layer_target * converge) / (1.0 - converge);

            // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
            vec4 color = surfaceColor(sample_uv) * boundaryMask(sample_uv);

            float belongs = step(abs(pixel_layer - layer), 0.5);
            inner += color * belongs * layer_alpha;
        }

        float initial_form = smoothstep(0.0, 0.05, rp);
        inner.a *= mix(1.0, 0.0, initial_form);
        vec4 base_color = surfaceColor(uv);
        float base_alpha = 1.0 - smoothstep(0.0, 0.1, rp);

        result = base_color * base_alpha + inner * (1.0 - base_alpha);
    }
    return result;
}

vec4 pIn(vec2 uv, float t)  { return snapBody(uv, t, true);  }
vec4 pOut(vec2 uv, float t) { return snapBody(uv, t, false); }
