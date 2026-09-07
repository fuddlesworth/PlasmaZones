// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Trail pointer shader — a luminous ribbon along the trail
// polyline. Each live segment contributes a tapered core (width falls with
// age) plus a wide gaussian halo; coverage is the max over segments so
// overlapping folds of the path do not stack into a bright blob. Colour
// runs through the brand spectrum by age: the head colour at the pointer,
// the brand's blue and purple stops in between, and the tail colour at the
// far end. Speed widens and brightens the ribbon through `speedBoost`.
//
// Everything fades to exactly zero once a sample's age reaches `lifetime`,
// which the metadata's trailSeconds covers at its maximum.

const int kMaxTrail = 32;

vec3 trailColour(float t) {
    const vec3 blue = vec3(0.231, 0.510, 0.965);
    const vec3 purple = vec3(0.659, 0.333, 0.969);
    t = clamp(t, 0.0, 1.0) * 3.0;
    if (t < 1.0) {
        return mix(p_colorA.rgb, blue, t);
    }
    if (t < 2.0) {
        return mix(blue, purple, t - 1.0);
    }
    return mix(purple, p_colorB.rgb, t - 2.0);
}

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float lifetime = max(p_lifetime, 0.05);
    float halfWidth = 0.5 * max(p_width, 0.5) * scale;
    float glowReach = halfWidth * 3.0 + 2.0 * scale;

    // Coarse reject: the halo never reaches beyond this from any segment.
    float cull = glowReach * 3.0;

    float core = 0.0;
    float halo = 0.0;
    float bestAge = 1.0;
    float bestWeight = 0.0;
    for (int i = 0; i < kMaxTrail - 1; ++i) {
        if (i + 1 >= count) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        if (a.z >= lifetime) {
            break;
        }
        vec2 lo = min(a.xy, b.xy) - cull;
        vec2 hi = max(a.xy, b.xy) + cull;
        if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
            continue;
        }

        float t;
        float d = pointerSegmentDistance(px, i, t);
        float age = clamp(mix(a.z, b.z, t) / lifetime, 0.0, 1.0);
        float speed = mix(a.w, b.w, t);
        float boost = 1.0 + p_speedBoost * clamp(speed / 1500.0, 0.0, 1.0);

        // Width tapers with age, both the core and its halo. The fade is
        // quadratic so the far end thins out instead of stopping dead.
        float life = 1.0 - age;
        float fade = life * life;
        float w = halfWidth * boost * life;
        float coreHere = (1.0 - smoothstep(w - 0.75, w + 0.75, d)) * fade;
        float sigma = (w * 3.0 + 2.0 * scale) * boost;
        float haloHere = exp(-(d * d) / (2.0 * sigma * sigma)) * fade * 0.35 * p_glow;

        float weight = max(coreHere, haloHere);
        if (weight > bestWeight) {
            bestWeight = weight;
            bestAge = age;
        }
        core = max(core, coreHere);
        halo = max(halo, haloHere);
    }

    float alpha = clamp(core + halo, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    vec3 rgb = trailColour(bestAge);
    // The halo is additive light: keep its colour but let the core stay
    // solid, so the ribbon reads as a lit line inside a soft glow.
    float paramAlpha = mix(p_colorA.a, p_colorB.a, bestAge);
    return premul(rgb * (1.0 + 0.5 * halo), alpha * paramAlpha);
}
