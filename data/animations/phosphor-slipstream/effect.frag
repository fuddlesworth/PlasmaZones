// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Slipstream — the strip leg of the phosphor-flux / phosphor-bloom /
// phosphor-stream / desktop-phosphor set. The family's motif is light as
// signal: content emits luminous energy when it moves, carried by the brand
// gradient (cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E).
// Here the emitters are the moving columns themselves: horizontal luma edges
// shed gradient trails that stream out behind the direction of travel (cyan
// nearest the content, rose at the tail), ember sparks drift in the wake
// (phosphor-stream's motif), and a faint directional smear ties the light to
// the motion.
//
// Strip contract (strip_transition.glsl): iTime is SECONDS, not progress;
// intensity keys off iStripMotion, which converges to zero at settle, so the
// whole effect fades with the spring and the settle frame is the identity
// image.
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
    // Ramp in from a gentle scroll, saturate on a flick. This factor is what
    // makes the settle frame the identity image.
    float vis = smoothstep(0.03, 0.35, speed) * m;
    if (vis < 1.0e-3) {
        return getStripColor(uv);
    }
    float dir = sign(v);
    vec4 base = getStripColor(uv);

    // ── Gradient trails ──
    // A trail pixel receives light from content AHEAD of it along the motion
    // direction: march that way, and where the luma changes horizontally (a
    // column edge, a glyph, a highlight) deposit gradient light that fades
    // and reddens with distance. Edge-weighting (the luma DERIVATIVE, not
    // the luma) keeps a big white page from glowing wholesale; only its
    // boundaries shed light.
    const int kTaps = 10;
    float trailSpan = 0.05 + 0.13 * p_trailLength; // uv units at full strength
    float stepLen = trailSpan / float(kTaps);
    vec3 glow = vec3(0.0);
    for (int i = 1; i <= kTaps; ++i) {
        float d = float(i) * stepLen;
        vec2 su = uv + vec2(dir * d, 0.0);
        float edge = abs(lumaAt(su + vec2(stepLen, 0.0)) - lumaAt(su - vec2(stepLen, 0.0)));
        float fall = 1.0 - float(i) / float(kTaps + 1);
        glow += fluxGradient(float(i) / float(kTaps)) * edge * fall * fall;
    }
    glow *= vis * p_intensity * (3.2 / float(kTaps));

    // ── Ember sparks ──
    // Sparse hash-seeded sparks drifting against the travel direction, on
    // their own per-cell phase so they twinkle rather than march in step.
    // Cell space is aspect-corrected so sparks stay round.
    vec3 ember = vec3(0.0);
    if (p_embers > 0.01) {
        float aspect = resolutionSafe().x / max(resolutionSafe().y, 1.0);
        vec2 g = uv * vec2(42.0 * aspect, 42.0);
        // The field drifts opposite to travel, faster with speed.
        g.x += dir * iTime * (2.0 + 10.0 * speed);
        vec2 cell = floor(g);
        vec2 rnd = hash22(cell);
        // Only a fraction of cells carry a spark at all.
        if (rnd.x > 1.0 - 0.35 * p_embers) {
            vec2 sparkPos = 0.15 + 0.7 * hash22(cell + 17.0);
            float dist = length(g - cell - sparkPos);
            float twinkle = 0.6 + 0.4 * sin(iTime * (4.0 + 6.0 * rnd.y) + rnd.y * 6.2831);
            float spark = smoothstep(0.16, 0.0, dist) * twinkle;
            ember = fluxGradient(rnd.y) * spark * vis * p_embers * 0.9;
        }
    }

    // ── Directional smear ──
    // A faint 3-tap smear along the motion axis so the light reads as part
    // of the movement instead of an overlay. Deliberately subtle: the blur
    // pack exists for the heavy version.
    float smear = clamp(speed * 0.02, 0.0, 0.012) * vis;
    vec3 body = base.rgb;
    if (smear > 1.0e-5) {
        vec3 acc = base.rgb + getStripColor(uv + vec2(dir * smear, 0.0)).rgb
            + getStripColor(uv + vec2(dir * smear * 2.0, 0.0)).rgb;
        body = mix(base.rgb, acc / 3.0, 0.6);
    }

    return vec4(body + glow + ember, base.a);
}
