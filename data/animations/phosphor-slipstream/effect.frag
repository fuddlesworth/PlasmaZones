// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Slipstream — the strip leg of the phosphor-flux / phosphor-bloom /
// phosphor-stream / desktop-phosphor set. The family's motif is STRUCTURED
// light as signal, carried by the brand gradient (cyan #22D3EE → blue
// #3B82F6 → purple #A855F7 → rose #F43F5E): flux races pulses along circuit
// traces, stream pours windows through discrete luminous lanes. Slipstream
// adapts both to the scroll: a set of faint horizontal signal lanes lights
// up across the strip and bright gradient pulses race along them, overtaking
// the content in the direction of travel, while the content's own highlights
// throw comet tails behind the motion and ember sparks drift in the wake.
// Everything scales with scroll velocity and vanishes at settle.
//
// Strip contract (strip_transition.glsl): iTime is SECONDS, not progress;
// intensity keys off iStripMotion, which converges to zero at settle, so
// the settle frame is the identity image.
#include <strip_transition.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Same
// tunable-slot shape as desktop-phosphor: the p_color* defines resolve to
// the customColors pool the strip pass binds.
vec3 fluxGradient(float t) {
    vec3 cyan = length(p_colorCyan.rgb) > 0.01 ? p_colorCyan.rgb : vec3(0.133, 0.827, 0.933);
    vec3 blue = length(p_colorBlue.rgb) > 0.01 ? p_colorBlue.rgb : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose = length(p_colorRose.rgb) > 0.01 ? p_colorRose.rgb : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

float lumaAt(vec2 uv) {
    vec3 c = getStripColor(uv).rgb;
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, 0.02);
    // Signed velocity in output-widths per second; the sign is the direction
    // the drawn content is travelling (the offset decays toward zero).
    float v = iStripMotion.w;
    float speed = abs(v);
    // Lively already on a moderate scroll, saturated on a flick. This factor
    // is what makes the settle frame the identity image.
    float vis = smoothstep(0.02, 0.22, speed) * m;
    if (vis < 1.0e-3) {
        return getStripColor(uv);
    }
    float dir = sign(v);
    vec4 base = getStripColor(uv);

    // ── Signal lanes with racing pulses ──
    // The structural centerpiece, borrowed from phosphor-flux: discrete
    // horizontal lanes across the strip, each a faint glowing trace, with
    // bright gradient comets racing along them FASTER than the content so
    // they visibly overtake the scroll. Lane phase, comet density and comet
    // speed are all hash-seeded per lane, so the field reads as live signal
    // traffic rather than a marching pattern.
    vec3 lanes = vec3(0.0);
    {
        vec2 s = stripUv(uv);
        float laneCount = max(p_lanes, 2.0);
        float laneIdx = floor(s.y * laneCount);
        float laneOff = fract(s.y * laneCount) - 0.5; // -0.5 .. 0.5 within the lane
        vec2 lrnd = hash22(vec2(laneIdx, 7.3));
        // Vertical glow profile of the lane: a soft core with room for the
        // comet bloom.
        float laneProfile = exp(-laneOff * laneOff * 14.0);
        // The faint continuous trace, so the lanes exist as a circuit even
        // between pulses.
        float trace = exp(-laneOff * laneOff * 70.0) * 0.10;
        // Comet track: drifts against the content, speed scaled well above
        // the scroll speed so pulses overtake. Each track cell may carry one
        // comet; density rises with speed.
        float trackX = s.x * (2.2 + 1.6 * lrnd.x) - dir * iTime * (1.0 + 3.5 * speed + 1.5 * lrnd.y);
        float cellIdx = floor(trackX);
        float px = fract(trackX);
        vec2 crnd = hash22(vec2(cellIdx, laneIdx * 13.1 + 3.7));
        float density = clamp(0.30 + 0.55 * speed, 0.0, 0.85);
        float present = step(1.0 - density, crnd.x);
        // Comet shape along the track: a sharp head with a long tapered tail
        // trailing behind the direction of travel.
        float head = 0.2 + 0.6 * crnd.y;
        float dxAlong = (px - head) * dir;
        float body = dxAlong <= 0.0 ? exp(dxAlong * (4.0 + 6.0 * (1.0 - p_trailLength))) : exp(-dxAlong * 45.0);
        vec3 cometColor = fluxGradient(fract(crnd.x * 4.7 + lrnd.y));
        lanes = (cometColor * present * body * laneProfile * 1.6 + fluxGradient(lrnd.x) * trace) * vis * p_intensity;
    }

    // ── Comet trails off the content's own highlights ──
    // Bright content (glints, highlights, hard edges) throws a gradient tail
    // behind the motion: march toward where the content came from and let
    // both raw highlights and luma edges deposit light, cyan at the head
    // reddening down the tail. Gain is deliberately bold; the family is a
    // light show, not a hint.
    const int kTaps = 12;
    float trailSpan = 0.08 + 0.20 * p_trailLength; // uv units at full strength
    float stepLen = trailSpan / float(kTaps);
    vec3 comet = vec3(0.0);
    for (int i = 1; i <= kTaps; ++i) {
        vec2 su = uv + vec2(dir * float(i) * stepLen, 0.0);
        float l = lumaAt(su);
        float highlight = max(l - 0.55, 0.0) * 2.0;
        float edge = abs(lumaAt(su + vec2(stepLen, 0.0)) - lumaAt(su - vec2(stepLen, 0.0))) * 1.5;
        float fall = pow(1.0 - float(i) / float(kTaps + 1), 1.4);
        comet += fluxGradient(float(i) / float(kTaps)) * (highlight + edge) * fall;
    }
    comet *= vis * p_intensity * (5.5 / float(kTaps));

    // ── Ember sparks ──
    // Bigger, brighter and slightly elongated along the motion axis compared
    // to a plain dot, drifting against the travel direction on per-cell
    // phases so they twinkle rather than march.
    vec3 ember = vec3(0.0);
    if (p_embers > 0.01) {
        float aspect = resolutionSafe().x / max(resolutionSafe().y, 1.0);
        vec2 g = uv * vec2(34.0 * aspect, 34.0);
        g.x += dir * iTime * (2.0 + 12.0 * speed);
        vec2 cell = floor(g);
        vec2 rnd = hash22(cell);
        if (rnd.x > 1.0 - 0.4 * p_embers) {
            vec2 sparkPos = 0.15 + 0.7 * hash22(cell + 17.0);
            vec2 d = (g - cell - sparkPos) * vec2(0.55, 1.0); // stretched along x
            float twinkle = 0.6 + 0.4 * sin(iTime * (4.0 + 6.0 * rnd.y) + rnd.y * 6.2831);
            float spark = smoothstep(0.22, 0.0, length(d)) * twinkle;
            ember = fluxGradient(rnd.y) * spark * vis * p_embers * 1.4;
        }
    }

    // ── Directional smear ──
    // A faint 3-tap smear tying the light to the motion; the blur pack
    // exists for the heavy version.
    float smear = clamp(speed * 0.02, 0.0, 0.012) * vis;
    vec3 body = base.rgb;
    if (smear > 1.0e-5) {
        vec3 acc = base.rgb + getStripColor(uv + vec2(dir * smear, 0.0)).rgb
            + getStripColor(uv + vec2(dir * smear * 2.0, 0.0)).rgb;
        body = mix(base.rgb, acc / 3.0, 0.6);
    }

    return vec4(body + lanes + comet + ember, base.a);
}
