// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor motes surface shader — the Phosphor set's ambience pack: the
// window sheds embers of luminous dust from its own frame (this pack
// declares `paddingParam: moteRange`, so the host inflates the capture
// canvas the same way it does for the glow pack).
//
// Every mote is born ON the frame perimeter — most from the top edge, some
// from the sides, a few from the bottom — detaches along the edge's outward
// normal, then buoyancy takes over and curls it upward through the margin.
// Side-born motes peel off and rise; bottom-born sparks sink briefly and
// die before drifting back over the content. What separates this from a
// particle ring like fireflies:
//   • Emanation — the dust originates from the window itself, so the effect
//     reads as the surface shedding light, not ambient rain around it.
//   • Depth — each mote carries a hashed depth: near motes are larger,
//     brighter and faster, far ones small, dim and slow.
//   • Real trails — the tail is the mote's own path history: the position
//     function is re-evaluated a few steps back in time and stamped with
//     decaying weight and growing blur, so trails curve through the detach
//     and the upward turn the way an ember streak actually does.
// A handful of motes hash into rare bright embers with heavier trails, and
// a fine halo of twinkling micro-dust hugs the frame, thinning with
// distance, so the margin never reads as empty space between a few dots.
//
// Hue burns through the brand accent gradient over each mote's life — born
// cyan #22D3EE at the frame, through blue #3B82F6 and purple #A855F7,
// burning out rose #F43F5E at the end of its climb. Motes fade in at birth
// and burn out before their clock wraps, so respawn is never visible.
// Composites BEHIND the window (over transparency only), so the dust ducks
// under the frame edge instead of crossing the content. Dims when
// unfocused.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#include <surface_noise.glsl>

const int kMaxMotes = 24;
const int kTailTaps = 4; // head + 3 history stamps

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(p_colorCyan.rgb, p_colorBlue.rgb, clamp(t, 0.0, 1.0));
    c = mix(c, p_colorPurple.rgb, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, p_colorRose.rgb, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// Birth point on the frame perimeter and its outward normal, from a single
// hash. Weighted toward the top edge (rising dust sheds mostly upward):
// 40% top, 25% each side, 10% bottom.
void frameBirth(float u, out vec2 spawn, out vec2 normal) {
    vec2 tl = uSurfaceFrameTopLeft;
    vec2 sz = max(uSurfaceFrameSize, vec2(1.0));
    if (u < 0.40) {
        spawn = tl + vec2(sz.x * (u / 0.40), 0.0);
        normal = vec2(0.0, -1.0);
    } else if (u < 0.65) {
        spawn = tl + vec2(0.0, sz.y * ((u - 0.40) / 0.25));
        normal = vec2(-1.0, 0.0);
    } else if (u < 0.90) {
        spawn = tl + vec2(sz.x, sz.y * ((u - 0.65) / 0.25));
        normal = vec2(1.0, 0.0);
    } else {
        spawn = tl + vec2(sz.x * ((u - 0.90) / 0.10), sz.y);
        normal = vec2(0.0, 1.0);
    }
}

// Closed-form emanation path for one mote at (scaled) time t. Stateless on
// purpose: the tail re-evaluates this at earlier times, so the whole streak
// stays consistent frame to frame with no particle history buffer. The
// motion is detach (eases out along the edge normal, fast then settling)
// plus buoyancy (upward, growing as age squared) plus a layered wander, so
// the path bends from "leaving the edge" into "climbing".
vec2 motePath(float h1, float h2, float h3, float t, float detachDist,
              float riseDist, float swayAmp, out float age) {
    float rate = 0.05 + 0.07 * h3;
    age = fract(h2 + t * rate);

    vec2 spawn, normal;
    frameBirth(h1, spawn, normal);

    float detach = detachDist * (1.0 - exp(-age * 4.0)) * (0.6 + 0.4 * h2);
    float rise = riseDist * age * age;
    float wander = sin(age * 7.0 + h3 * TAU) * 0.5
                 + sin(age * 17.0 - t * 0.5 + h1 * TAU) * 0.3;

    return spawn + normal * detach + vec2(swayAmp * wander, -rise);
}

// Fade in at birth, burn out well before the clock wraps — bottom-born
// sparks die before buoyancy would carry them back over the content.
float moteLife(float age) {
    return smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.55, 0.95, age));
}

// Micro-dust: a scrolling grid of tiny twinkling specks forming a halo that
// hugs the frame and thins with distance. One cell lookup per fragment.
vec3 microDust(vec2 px, float t, float amount, float reachPx, out float dustA) {
    dustA = 0.0;
    if (amount <= 0.001) {
        return vec3(0.0);
    }
    // Attenuate by distance outside the frame rect, so the specks halo the
    // window instead of filling the canvas uniformly.
    vec2 halfSz = 0.5 * max(uSurfaceFrameSize, vec2(1.0));
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    vec2 q = abs(px - cen) - halfSz;
    float dOut = length(max(q, vec2(0.0)));
    float halo = exp(-dOut / max(reachPx * 0.9, 1.0));

    float cellPx = 13.0 * max(uSurfaceScale, 0.001);
    // Shift the sample point downward over time: the speck pattern rises.
    vec2 dq = (px + vec2(0.0, t * 26.0 * max(uSurfaceScale, 0.001))) / cellPx;
    vec2 cellId = floor(dq);
    vec3 h = hash23(cellId);

    // Sparse occupancy, per-speck twinkle phase. The twinkle keeps a floor
    // so a speck dims between blinks instead of vanishing — a cubed sine
    // alone left the dust invisible most of every cycle.
    if (h.z < 0.60) {
        return vec3(0.0);
    }
    vec2 speck = cellId + 0.2 + 0.6 * h.xy;
    float d = length(dq - speck);
    float body = exp(-d * d / 0.03);
    float blink = 0.5 + 0.5 * sin(t * (2.0 + 3.0 * h.x) + h.y * TAU);
    float twinkle = 0.35 + 0.65 * blink * blink * blink;

    // Specks near the frame are young (cyan), far ones old (toward purple).
    vec3 col = fluxGradient(clamp(dOut / max(reachPx, 1.0), 0.0, 1.0) * 0.6 + h.x * 0.15);
    dustA = body * twinkle * amount * halo * 0.55;
    return col * dustA;
}

vec4 pSurface(vec2 uv) {
    vec4 window = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return window;
    }

    vec2 px = surfacePixel(uv);

    float reachPx  = max(p_moteRange, 8.0) * max(uSurfaceScale, 0.001);
    float baseSize = max(p_moteSize, 0.5) * max(uSurfaceScale, 0.001);
    float tailLen  = max(p_tailLength, 1.0) * max(uSurfaceScale, 0.001);
    float swayAmp  = max(p_sway, 0.0) * max(uSurfaceScale, 0.001);
    float t = iTime * max(p_riseSpeed, 0.0);
    float count = clamp(p_moteCount, 1.0, float(kMaxMotes));
    float intensity = clamp(p_intensity, 0.0, 2.0);

    // Travel distances: rise most of the captured margin, detach a fraction
    // of it, so the full arc stays inside the canvas the host padded for us.
    float riseDist   = reachPx * 0.85;
    float detachDist = reachPx * 0.28;

    vec3 glow = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < kMaxMotes; ++i) {
        if (float(i) >= count) {
            break;
        }
        // Spawn and depth ride low-discrepancy sequences (golden ratio and
        // the plastic number), NOT hashSin1: consecutive integer seeds
        // cluster badly through the sin hash (motes 4 and 11 land on the
        // SAME perimeter spot, and several of the deepest/biggest motes
        // come out adjacent), which read as the big motes clumping at one
        // place. The sequences guarantee even perimeter and depth coverage
        // at any count; a small hash jitter keeps them from looking dealt.
        float h2 = hashSin1(float(i) + 7.71);
        float h3 = hashSin1(float(i) + 42.9);
        float h1 = fract(0.6180340 * float(i) + 0.31 + (h3 - 0.5) * 0.04);

        // ── Depth: near motes are bigger, brighter, faster. A few motes
        // hash into rare bright embers with heavier trails. ──
        float depth = fract(0.7548777 * float(i) + 0.19);
        float ember = step(0.90, hashSin1(float(i) + 5.5));
        float sizeI   = baseSize * mix(0.55, 1.7, depth) * (1.0 + ember * 0.4);
        float brightI = mix(0.30, 1.0, depth * depth) * (1.0 + ember * 1.1);
        float tMote   = t * mix(0.6, 1.35, depth);

        // Tail spacing in scaled-time: how long the mote takes to cover
        // tailLen along its arc at its own rate, split across the stamps.
        float travelPerT = (riseDist + detachDist) * (0.05 + 0.07 * h3);
        float tailStep = tailLen / max(travelPerT, 1.0) / float(kTailTaps - 1);

        // Cheap cull: the whole streak lives between spawn and head, so a
        // fragment far from that segment's bounding circle can skip it.
        float altHead;
        vec2 headPos = motePath(h1, h2, h3, tMote, detachDist, riseDist, swayAmp, altHead);
        vec2 spawn, normalUnused;
        frameBirth(h1, spawn, normalUnused);
        vec2 mid = (spawn + headPos) * 0.5;
        float bound = length(headPos - spawn) * 0.5 + sizeI * 4.0 + swayAmp;
        if (length(px - mid) > bound) {
            continue;
        }

        // Ember shimmer: a slow coal-glow flutter, not a firefly blink.
        float shimmer = 0.75 + 0.25 * sin(tMote * (3.0 + 2.0 * h1) + h3 * TAU);

        // ── Head + curved trail: stamp the path history with decaying
        // weight and growing blur. Each stamp carries its own age, so hue
        // and the life envelope stay correct along the whole streak. ──
        for (int k = 0; k < kTailTaps; ++k) {
            float ageK;
            vec2 posK = motePath(h1, h2, h3, tMote - float(k) * tailStep,
                                 detachDist, riseDist, swayAmp, ageK);
            float lifeK = moteLife(ageK);
            if (lifeK <= 0.001) {
                continue;
            }

            float sizeK = sizeI * (1.0 + 0.45 * float(k));
            float w = (k == 0) ? 1.0 : (0.5 / float(k)) * (0.55 + ember * 0.5);

            vec2 d = px - posK;
            float body = exp(-dot(d, d) / (2.0 * sizeK * sizeK));

            // Hue burns through the gradient over the mote's life.
            vec3 col = fluxGradient(ageK * 0.9 + h3 * 0.1);
            float contrib = body * w * lifeK * brightI * shimmer;
            glow += col * contrib;
            alpha += contrib;
        }
    }

    // ── Micro-dust halo hugging the frame. ──
    float dustA;
    glow += microDust(px, t, clamp(p_dustAmount, 0.0, 1.0), reachPx, dustA);
    alpha += dustA;

    alpha = clamp(alpha * intensity, 0.0, 1.0);
    glow *= intensity;

    // Focus cue: the dust dims on unfocused surfaces, like the border family.
    float dim = focusDim(0.55);
    vec4 pane = vec4(glow * dim, alpha * dim);

    // Behind-the-window composite: motes light only the transparent margin
    // (and any translucency), never crossing the content.
    return slabComposite(window, pane);
}
