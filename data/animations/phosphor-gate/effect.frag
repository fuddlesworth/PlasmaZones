// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Gate — the strip leg of the phosphor set. The strip flows through
// two PINNED edge fields, like material through a scanner: an energize gate
// at the arrival edge (content materializes through the family's navy
// silhouette behind a cyan gradient rim, fed by ember sparks) and a
// de-energize gate at the departure edge (peek's multiplicative drain into
// the silhouette, leaving a rose persistence tail). The gates never move;
// the CONTENT moves through them, which is the family's causality without a
// progress variable: a column crossing the edge is what gets energized. The
// middle of the screen is bit-exact identity, so the region the user is
// reading is never touched, and there is no sample displacement anywhere.
//
// Direction never appears through sign(): amplitude is a smooth monotone
// function of |velocity| and the arrival/departure roles crossfade through
// a smooth signed direction, so a wheel reversal collapses the gates to
// nothing and re-lights them in the other polarity without a pop
// (phosphor-vortex's velocity-shapes-never-gates lesson).
//
// Strip contract (strip_transition.glsl): iTime is SECONDS, not progress;
// every term keys off iStripMotion, which converges to zero at settle, and
// the early-out below satisfies the settle-frame identity requirement.
#include <strip_transition.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Same
// tunable-slot shape as desktop-phosphor.
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

// One edge's gate, applied to `col` in place. q is the edge-distance field
// coordinate (0 at the screen edge, 1 at the gate's inner boundary), A and P
// the arrival/departure role weights (each already folded with amp and the
// work-area mask). Every term carries `kill`, which is EXACTLY 0.0 beyond
// q = 1.2 — that factor is the bit-exact-identity guarantee for the middle
// of the screen.
vec3 applyGate(vec3 col, float lum, vec3 sil, float q, float m, float A, float P, float glow, float dimK,
               float sparkle, float fieldDepth, float timeSec, float uvY) {
    float kill = 1.0 - smoothstep(1.0, 1.2, q);
    if (kill <= 0.0) {
        return col;
    }
    float qm = q + m;
    float fw = (1.0 - smoothstep(0.0, 1.0, qm)) * kill;
    float lumW = 0.35 + 0.65 * lum;

    // ── Arrival: energize ──
    // Content nearest the edge just arrived and is least materialized: the
    // navy silhouette (shaped by the content's own luminance) gives way to
    // the full image as it travels inward, behind a gradient rim pinned at
    // the reveal midpoint. Cyan leads at the edge, grading into blue.
    float rev = smoothstep(0.10, 0.85, qm);
    col = mix(col, sil, (1.0 - rev) * A * dimK * kill);
    float rimD = (qm - 0.5) / 0.20;
    float rim = exp(-rimD * rimD) * kill; // squared via multiply: rimD is signed
    vec3 gArrCol = fluxGradient(clamp(qm * 0.35, 0.0, 0.35));
    col += gArrCol * rim * A * glow * sparkle * lumW * 0.9;

    // ── Departure: de-energize with a rose persistence tail ──
    // Peek's multiplicative drain, strongest where content is about to
    // leave, sinking the last sliver into the silhouette; scan's afterglow
    // rotated to X carries rose at the edge grading into purple inward. The
    // tail length is a fraction of the SCREEN (absolute uv distance), not of
    // the breathing field, then hard-killed with everything else.
    col *= 1.0 - fw * P * dimK * 0.7;
    col = mix(col, sil, (1.0 - smoothstep(0.0, 0.45, qm)) * P * 0.6 * kill);
    float tail = 0.06 + clamp(p_persistence, 0.0, 1.0) * 0.30;
    float ag = exp(-max(q, 0.0) * fieldDepth / tail) * kill;
    vec3 tailCol = fluxGradient(clamp(1.0 - qm * 0.35, 0.65, 1.0));
    col += tailCol * ag * P * glow * lumW * 0.35;

    // ── Ember sparks feeding the arrival gate ──
    // Condense's comet embers rotated horizontal: one comet per screen row,
    // drifting inward from the edge on a constant per-row clock (speed
    // shapes only the AMPLITUDE, never the phase), thickest where the rim
    // is consuming them (the locality gate).
    if (p_embers > 0.001 && A > 0.01) {
        float rows = max(p_density, 8.0);
        float yRow = uvY * rows; // logical orientation, same space as the uv everywhere else
        float row = floor(yRow);
        float seed = niriHash(vec2(row * 7.13 + 0.37, 1.7));
        float ph = fract(seed * 7.0 + timeSec * (0.5 + 0.7 * seed));
        float eq = ph * 1.1; // the comet's own field position, edge → inward
        float dq = qm - eq;
        float dy = (yRow - row) - (0.2 + 0.6 * niriHash(vec2(row, 9.1)));
        float lat = exp(-dy * dy * 10.0);
        float head = exp(-dq * dq * 90.0) * lat;
        float trail = dq < 0.0 ? exp(dq * 5.0) * lat * 0.6 : 0.0;
        float life = smoothstep(0.0, 0.15, ph) * (1.0 - smoothstep(0.75, 1.0, ph));
        float dust = 0.35 + 0.65 * exp(-abs(qm - 0.5) / 0.4);
        col += fluxGradient(clamp(eq * 0.35, 0.0, 0.35)) * (head + trail) * life * dust * A * p_embers * glow * 1.2
            * kill;
    }
    return col;
}

vec4 pTransition(vec2 uv, float t) { // t = iTime SECONDS, monotonic
    float w = iStripMotion.w;
    float spd = abs(w);
    // Smooth monotone amplitude: 0 at settle, saturating toward 1 with
    // speed. No thresholds anywhere, so user-steered scrolling can never
    // flicker the gates.
    float amp = spd / (spd + max(p_speedRef, 0.05));
    float D = clamp(p_depth, 0.05, 0.22) * amp;
    if (spd < 1.0e-4 || D < 1.0e-4) {
        return getStripColor(uv); // identity contract: the settle frame is untouched
    }

    // Smooth signed direction, |dSign| < 1, 0 at the reversal crossing: the
    // roles crossfade instead of flipping on sign().
    float dSign = w / (spd + 0.08);
    float arrL = clamp(dSign, 0.0, 1.0);  // content travels right → it ARRIVES at the left edge
    float arrR = clamp(-dSign, 0.0, 1.0); // content travels left → it arrives at the right edge
    float depL = arrR;
    float depR = arrL;

    float mask = stripMask(uv, p_edgeFeather);
    vec4 base = getStripColor(uv); // the ONLY content tap, never displaced
    vec3 col = base.rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));

    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    vec3 sil = tint * (0.5 + 1.6 * lum); // the family silhouette: navy shaped by the content
    float glow = clamp(p_glow, 0.0, 2.0);
    float dimK = clamp(p_unlitDim, 0.0, 1.0);
    // The family sparkle grain, verbatim.
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * iResolution / 2.0) + floor(float(iFrame) * 0.2));

    // One fbm meander shared by both gates: the boundary wavers like a live
    // field, drifting slowly and monotonically (never rewinds).
    float m = (fbm(vec2(uv.y * 3.0 * max(p_grain, 0.2), 0.7 + t * 0.15), 3, 2.0) - 0.5) * 0.35
        * clamp(p_meander, 0.0, 1.0);

    float fieldDepth = max(D, 1.0e-4);
    // Left edge, then right edge, unrolled.
    col = applyGate(col, lum, sil, uv.x / fieldDepth, m, arrL * amp * mask, depL * amp * mask, glow, dimK, sparkle,
                    fieldDepth, t, uv.y);
    col = applyGate(col, lum, sil, (1.0 - uv.x) / fieldDepth, m, arrR * amp * mask, depR * amp * mask, glow, dimK,
                    sparkle, fieldDepth, t, uv.y);

    // Opaque replace pass; NO upper clamp on rgb (HDR captures, per the
    // desktop packs' rationale).
    return vec4(col, 1.0);
}
