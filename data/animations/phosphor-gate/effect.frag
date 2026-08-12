// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Gate — the strip leg of the phosphor set. The strip flows through
// two PINNED edge fields, like material through a scanner: an energize gate
// at the arrival edge (content materializes through the family's navy
// silhouette behind a cyan gradient rim, fed by ember sparks) and a
// de-energize gate at the departure edge (peek's multiplicative drain into
// the silhouette, leaving a rose afterglow). The gates never move;
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

// The four gradient stops, resolved ONCE per fragment by resolveStops() below
// and read by every fluxGradient call after it. A gate fragment calls
// fluxGradient up to six times, and the resolve is four length() square roots
// plus four branches, so re-deriving it per call was the pack's largest
// avoidable per-fragment cost.
vec3 gStopCyan;
vec3 gStopBlue;
vec3 gStopPurple;
vec3 gStopRose;

// The family's unset-probe: a customColors slot the profile never set arrives
// as all-zero, and length() > 0.01 is how every phosphor pack tells that
// apart from a chosen colour. The corollary, which is family-wide and
// deliberate, is that a user who picks a NEAR-BLACK colour gets the bright
// default substituted instead. Nothing distinguishes the two at the uniform.
void resolveStops() {
    gStopCyan = length(p_colorCyan.rgb) > 0.01 ? p_colorCyan.rgb : vec3(0.133, 0.827, 0.933);
    gStopBlue = length(p_colorBlue.rgb) > 0.01 ? p_colorBlue.rgb : vec3(0.231, 0.510, 0.965);
    gStopPurple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    gStopRose = length(p_colorRose.rgb) > 0.01 ? p_colorRose.rgb : vec3(0.957, 0.247, 0.369);
}

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Same
// tunable-slot shape as desktop-phosphor.
vec3 fluxGradient(float t) {
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(gStopCyan, gStopBlue, clamp(t, 0.0, 1.0));
    c = mix(c, gStopPurple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, gStopRose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// One edge's gate, applied to `col` in place. q is the edge-distance field
// coordinate (0 at the screen edge, 1 at the gate's inner boundary), A and P
// the arrival/departure role weights (each already folded with amp and the
// work-area mask). Every term carries `kill`, which is EXACTLY 0.0 beyond
// q = 1.2 — that factor is the bit-exact-identity guarantee for the middle
// of the screen.
vec3 applyGate(vec3 col, float lum, vec3 sil, float q, float m, float A, float P, float glow, float dimK,
               float sparkle, float timeSec, float uvY) {
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

    // ── Departure: de-energize with a rose afterglow ──
    // Peek's multiplicative drain, strongest where content is about to
    // leave, sinking the last sliver into the silhouette; scan's trailing
    // glow rotated to X carries rose at the edge grading into purple inward.
    // The trail length is measured in FIELD units (the same q the kill
    // uses), not in absolute screen distance: the gate is hard-killed at
    // q = 1.2, so an absolute length longer than the field could never be
    // seen and the slider would only flatten brightness instead of
    // lengthening the trail.
    //
    // Param renamed persistence → afterglow pre-3.4-release (no configs to
    // migrate): persistence is the one CRT term the set had borrowed, it
    // belongs with phosphor-scan's beam and scanlines, and the family is a
    // plasma one everywhere else.
    col *= 1.0 - fw * P * dimK * 0.7;
    col = mix(col, sil, (1.0 - smoothstep(0.0, 0.45, qm)) * P * 0.6 * kill);
    float tail = 0.18 + clamp(p_afterglow, 0.0, 1.0) * 0.75;
    float ag = exp(-max(q, 0.0) / tail) * kill;
    vec3 tailCol = fluxGradient(clamp(1.0 - qm * 0.35, 0.65, 1.0));
    col += tailCol * ag * P * glow * lumW * 0.35;

    // ── Ember sparks feeding the arrival gate ──
    // Condense's comet embers rotated horizontal: one comet per screen row,
    // drifting inward from the edge on a constant per-row clock (speed
    // shapes only the AMPLITUDE, never the phase), thickest where the rim
    // is consuming them (the locality gate).
    float embers = clamp(p_embers, 0.0, 1.0);
    if (embers > 0.001 && A > 0.01) {
        float rows = max(p_density, 8.0);
        float yRow = uvY * rows; // logical orientation, same space as the uv everywhere else
        float row = floor(yRow);
        float within = yRow - row;
        float seed = niriHash(vec2(row * 7.13 + 0.37, 1.7));
        float ph = fract(seed * 7.0 + timeSec * (0.5 + 0.7 * seed));
        float eq = ph * 1.1; // the comet's own field position, edge → inward
        float dq = qm - eq;
        // Each comet lives entirely inside its own row: the jittered centre is
        // held away from the row boundaries and the lateral falloff is
        // windowed to zero AT those boundaries. Without the window a comet
        // whose centre sat near an edge stayed bright right up to it and then
        // dropped to the neighbouring row's unrelated value, drawing a visible
        // horizontal seam every row.
        float cy = 0.35 + 0.30 * niriHash(vec2(row, 9.1));
        float dy = within - cy;
        float lat = exp(-dy * dy * 10.0) * (1.0 - smoothstep(0.30, 0.5, abs(within - 0.5)));
        float head = exp(-dq * dq * 90.0) * lat;
        float trail = dq < 0.0 ? exp(dq * 5.0) * lat * 0.6 : 0.0;
        float life = smoothstep(0.0, 0.15, ph) * (1.0 - smoothstep(0.75, 1.0, ph));
        float dust = 0.35 + 0.65 * exp(-abs(qm - 0.5) / 0.4);
        col += fluxGradient(clamp(eq * 0.35, 0.0, 0.35)) * (head + trail) * life * dust * A * embers * glow * 1.2
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

    vec4 base = getStripColor(uv); // the ONLY content tap, never displaced

    // The gates sit at the WORK AREA's left and right edges, not the output's.
    // Under a side panel those differ, and anchoring the field to the output
    // would light the band where the panel is and truncate it away from where
    // columns actually enter and leave. The amplitude mask below is work-area
    // relative for the same reason, so the two now agree.
    float sxu = stripUv(uv).x;
    float qL = sxu / D;
    float qR = (1.0 - sxu) / D;
    // Outside BOTH gates every term below is multiplied by applyGate's kill,
    // which is exactly zero past q = 1.2 — the middle of the screen is
    // bit-exact identity by construction. Returning here makes that explicit
    // and skips the fbm meander, the sparkle hash and the silhouette for the
    // majority of the screen, which at a typical depth is most fragments.
    if (qL > 1.2 && qR > 1.2) {
        return base;
    }

    float mask = stripMask(uv, clamp(p_edgeFeather, 0.0, 0.5));
    vec3 col = base.rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));

    resolveStops();
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    vec3 sil = tint * (0.5 + 1.6 * lum); // the family silhouette: navy shaped by the content
    float glow = clamp(p_glow, 0.0, 2.0);
    float dimK = clamp(p_unlitDim, 0.0, 1.0);
    // The family sparkle grain, verbatim. iFrame grows without bound over a
    // session, but a strip pass is reaped at settle and re-armed from zero, so
    // the float(iFrame) here only ever sees one burst of scrolling — nowhere
    // near where the float would start quantising.
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * iResolution / 2.0) + floor(float(iFrame) * 0.2));

    // One fbm meander shared by both gates: the boundary wavers like a live
    // field, drifting slowly and monotonically (never rewinds). fbm's three
    // octaves sum to 0.875, not 1, so normalise before centring or the
    // meander sits biased toward one side.
    float m = (fbm(vec2(uv.y * 3.0 * max(p_grain, 0.2), 0.7 + t * 0.15), 3, 2.0) / 0.875 - 0.5) * 0.35
        * clamp(p_meander, 0.0, 1.0);

    // Left edge, then right edge, unrolled.
    col = applyGate(col, lum, sil, qL, m, arrL * amp * mask, depL * amp * mask, glow, dimK, sparkle, t, uv.y);
    col = applyGate(col, lum, sil, qR, m, arrR * amp * mask, depR * amp * mask, glow, dimK, sparkle, t, uv.y);

    // Replace pass; NO upper clamp on rgb (HDR captures, per the desktop
    // packs' rationale). Alpha is the capture's own, matching the identity
    // early-outs above rather than forcing 1.0 on a differently-composed
    // capture.
    return vec4(col, base.a);
}
