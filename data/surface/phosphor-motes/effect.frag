// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor motes surface shader — the Phosphor set's ambience pack: the
// window sheds embers of luminous dust from its own frame (this pack
// declares `paddingParam: moteRange`, so the host inflates the capture
// canvas the same way it does for the glow pack).
//
// Every mote is born ON the frame perimeter (uniform density per pixel of
// edge) and travels OUTWARD along its birth edge's normal for its whole
// life — dust born on the left spreads left, bottom dust spreads down —
// so the window sheds light evenly in every direction instead of all of
// it drifting up. What separates this from a particle ring like fireflies:
//   • Emanation — the dust originates from the window itself and radiates
//     away from it, so the effect reads as the surface shedding light, not
//     ambient rain around it.
//   • Depth — each mote carries a hashed depth: near motes are larger,
//     brighter and faster, far ones small, dim and slow.
//   • Real trails — the tail is the mote's own path history: the position
//     function is re-evaluated a few steps back in time and stamped with
//     decaying weight and growing blur, so the streak trails back toward
//     the frame edge the mote came from.
// A handful of motes hash into rare bright embers with heavier trails, and
// a fine halo of twinkling micro-dust hugs the frame, thinning with
// distance, so the margin never reads as empty space between a few dots.
//
// Hue burns through the brand accent gradient over each mote's life — born
// cyan #22D3EE at the frame, through blue #3B82F6 and purple #A855F7,
// burning out rose #F43F5E at the end of its drift. Motes fade in at birth
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
// hash. The hash maps to arc length, so spawn density is uniform per pixel
// of perimeter — a wide window sheds more dust from its long edges, and no
// edge is favoured (the dust radiates evenly all around).
void frameBirth(float u, out vec2 spawn, out vec2 normal) {
    vec2 tl = uSurfaceFrameTopLeft;
    vec2 sz = max(uSurfaceFrameSize, vec2(1.0));
    float s = u * 2.0 * (sz.x + sz.y);
    if (s < sz.x) {
        spawn = tl + vec2(s, 0.0);
        normal = vec2(0.0, -1.0);
    } else if (s < sz.x + sz.y) {
        spawn = tl + vec2(sz.x, s - sz.x);
        normal = vec2(1.0, 0.0);
    } else if (s < 2.0 * sz.x + sz.y) {
        spawn = tl + vec2(s - sz.x - sz.y, sz.y);
        normal = vec2(0.0, 1.0);
    } else {
        spawn = tl + vec2(0.0, s - 2.0 * sz.x - sz.y);
        normal = vec2(-1.0, 0.0);
    }
}

// Closed-form emanation path for one mote at (scaled) time t. Stateless on
// purpose: the tail re-evaluates this at earlier times, so the whole streak
// stays consistent frame to frame with no particle history buffer. The
// motion is purely RADIAL: outward along the birth edge's normal for the
// mote's whole life — a fast detach easing into a glide — while a layered
// wander slides it along the edge tangent. No shared drift direction.
vec2 motePath(float h1, float h2, float h3, float t, float reachPx,
              float swayAmp, out float age) {
    float rate = 0.05 + 0.07 * h3;
    age = fract(h2 + t * rate);

    vec2 spawn, normal;
    frameBirth(h1, spawn, normal);

    float travel = reachPx * (1.0 - pow(1.0 - age, 1.8)) * (0.75 + 0.25 * h2);
    vec2 tangent = vec2(-normal.y, normal.x);
    float wander = sin(age * 7.0 + h3 * TAU) * 0.5
                 + sin(age * 17.0 - t * 0.5 + h1 * TAU) * 0.3;

    // THE SWAY IS BOUNDED TOO, not just the travel. `travel` is held to the reach
    // handed in, which the caller already discounts to 0.9 of the captured margin
    // so the streak stays inside it, but the TANGENTIAL term had no such bound: the
    // declared ranges put sway at up to 48 logical px against a moteRange as low as
    // 16, so a mote born near a corner was carried along the edge, round it, and
    // out of the padded canvas. The shipped defaults do not reach that, the
    // declared ranges do. Capped so the diagonal magnitude stays under the reach:
    // sqrt(0.9^2 + 0.405^2) is about 0.99 of it.
    float sway = clamp(swayAmp * wander, -0.45 * reachPx, 0.45 * reachPx);
    return spawn + normal * travel + tangent * sway;
}

// Fade in at birth, burn out well before the clock wraps, so a respawn at
// the frame edge is never visible.
float moteLife(float age) {
    return smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.55, 0.95, age));
}

// Micro-dust: a grid of tiny twinkling specks forming a halo that hugs the
// frame, thins with distance, and STREAMS OUTWARD with the motes. The field
// is built in emanation coordinates — s along the nearest frame edge, v the
// outward distance — and the pattern scrolls in v, so specks everywhere
// march away from their own edge (a plain screen-space scroll read as the
// whole canvas rising from the bottom of the padded FBO). One cell lookup
// per fragment.
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
    // Inside the frame there is no outward direction (dOut collapses to 0
    // and the whole interior would share one scrolling row), so the dust
    // field lives strictly in the margin.
    if (dOut < 0.5) {
        return vec3(0.0);
    }
    float halo = exp(-dOut / max(reachPx * 0.9, 1.0));

    float cellPx = 13.0 * max(uSurfaceScale, 0.001);
    // Emanation coordinates: s runs along the frame's perimeter and v runs
    // outward, scrolled by the mote clock so the specks drift away from the frame
    // at every edge, corners included.
    //
    // s IS AN ANGLE ABOUT THE FRAME CENTRE, not the nearest-point sum it used to
    // be. That sum was `clamp(px, ...)` projected as bp.x + bp.y, and it broke at
    // the corners in two ways at once. For a fragment diagonally past a corner
    // BOTH clamp components saturate, so the sum is a single constant over the
    // whole quarter-plane fan: every other factor in the dust alpha is
    // radius-only, so a cell that lit up painted a uniform quarter-RING about
    // half a cell thick, expanding outward and blinking as a whole, instead of a
    // speck. And because it was a SUM, two fragments mirrored across a corner's
    // 45-degree line landed in the same cell, so the top dust field was a mirror
    // copy of the left one. The angle is continuous and distinct in both cases.
    //
    // A WHOLE NUMBER OF CELLS around the loop, and the cell index taken modulo
    // that, so the grid WRAPS. atan's seam lies on the -x axis, at the left
    // edge's midpoint, and without the wrap the cell either side of it would be a
    // different cell with a different speck and a different blink, which is a
    // visible one-cell seam. The count comes from the rect's perimeter so a cell
    // is about cellPx of edge, which keeps the density in device px.
    //
    // The angle does not equalise cell size the way true arc length would, so
    // density varies a little between a long edge's middle and its corners. For
    // random dust that reads as variation rather than as an artefact;
    // framePerimeter's own doc draws the same distinction for dashes.
    float loopCells = max(floor(4.0 * (halfSz.x + halfSz.y) / cellPx + 0.5), 1.0);
    float sCell = fract(framePerimeter(px, cen, halfSz) + 0.5) * loopCells;
    float v = dOut - t * 22.0 * max(uSurfaceScale, 0.001);
    vec2 dq = vec2(sCell, v / cellPx);
    vec2 cellId = vec2(mod(floor(dq.x), loopCells), floor(dq.y));
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
    // TAPERED TO NOTHING BEFORE THE BOUNDARY. The halo above is an exponential,
    // still about a third of peak where dOut reaches reachPx and not below 0.05
    // until about 2.4 times it, so on a host that pads by exactly moteRange the
    // twinkling field was truncated in a hard rectangle at a third of its
    // strength, with specks sliced mid-body. The mote heads have a radial travel
    // limit for this reason; the dust had none.
    dustA = body * twinkle * amount * halo * 0.55 * (1.0 - smoothstep(0.75, 1.0, dOut / max(reachPx, 1.0)));
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
    float t = iTime * max(p_driftSpeed, 0.0);
    float count = clamp(p_moteCount, 1.0, float(kMaxMotes));
    float intensity = clamp(p_intensity, 0.0, 2.0);

    // Travel distance: drift most of the captured margin, so the streak
    // (head plus blurred tail) stays inside the canvas the host padded.
    float driftDist = reachPx * 0.9;

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
        // tailLen along its drift at its own rate, split across the stamps.
        float travelPerT = driftDist * (0.05 + 0.07 * h3);
        float tailStep = tailLen / max(travelPerT, 1.0) / float(kTailTaps - 1);

        // Cheap cull: the whole streak lives between spawn and head, so a
        // fragment far from that segment's bounding circle can skip it. The
        // slack must cover the LARGEST tail stamp, not the head: tail taps
        // blur out to sizeI * (1 + 0.45 * (kTailTaps - 1)), and three sigmas
        // of that keeps the cut below visibility even for a zero-sway bright
        // ember (a head-sized slack clipped the outermost tail stamp at
        // ~0.24 of its peak).
        float altHead;
        vec2 headPos = motePath(h1, h2, h3, tMote, driftDist, swayAmp, altHead);
        vec2 spawn, normalUnused;
        frameBirth(h1, spawn, normalUnused);
        vec2 mid = (spawn + headPos) * 0.5;
        float maxTapR = sizeI * (1.0 + 0.45 * float(kTailTaps - 1));
        float bound = length(headPos - spawn) * 0.5 + maxTapR * 3.0 + swayAmp;
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
                                 driftDist, swayAmp, ageK);
            // A tap evaluated before the clock's fract wrap belongs to the
            // PREVIOUS mote instance (its age jumps HIGHER than the head's)
            // — skip it rather than stamping a disconnected ember far out
            // on the previous drift.
            if (ageK > altHead + 1.0e-4) {
                continue;
            }
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

    // BOTH sides of the premultiplied pair, not just alpha. glow and alpha are
    // accumulated together above (glow += col * contrib beside alpha += contrib,
    // and every fluxGradient channel is <= 1, so glow <= alpha holds through the
    // accumulation). Clamping only alpha and scaling glow bare breaks that at the
    // first mote overlap where the sum passes 1: alpha saturates while glow keeps
    // climbing, and the pack ships rgb > a. Clamp glow to the clamped alpha, which
    // preserves hue where nothing overflowed and only binds where it did.
    alpha = clamp(alpha * intensity, 0.0, 1.0);
    glow = min(glow * intensity, vec3(alpha));

    // Focus cue: the dust dims on unfocused surfaces, like the border family.
    float dim = focusDim(0.55);
    vec4 pane = vec4(glow * dim, alpha * dim);

    // Behind-the-window composite: motes light only the transparent margin
    // (and any translucency), never crossing the content.
    return slabComposite(window, pane);
}
