// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Phosphor — the official Phosphor brand desktop switch, the
// desktop leg of the phosphor-flux / phosphor-bloom / border-phosphor set.
// Where phosphor-bloom keeps the diagonal light sweep as the per-window
// signature, the desktop switch borrows phosphor-flux's signal-graph motif
// instead: a sparse circuit of nodes lights up across the screen in the
// switch direction, a bright pulse races each trace and its arrival
// activates the destination node, and the incoming desktop floods outward
// from every lit node behind a thin front carrying the brand gradient
// (cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E). `t` is
// forward switch progress in [0,1].
//
// Causality is real, not faked: each graph cell owns an activation time
// (directional projection plus per-cell stagger), every edge's pulse
// departs its earlier-activated endpoint and lands exactly at the later
// endpoint's activation, and that same activation clock starts the cell's
// reveal flood. So the circuit visibly carries the switch.
//
// Colour is a real tunable here: the p_color* slots resolve to the
// customColors pool, which the desktop-transition pass binds at parity with
// the per-window and surface shader contracts (see DesktopTransitionManager).
#include <desktop_transition.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    vec3 cyan   = length(p_colorCyan.rgb)   > 0.01 ? p_colorCyan.rgb   : vec3(0.133, 0.827, 0.933);
    vec3 blue   = length(p_colorBlue.rgb)   > 0.01 ? p_colorBlue.rgb   : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose   = length(p_colorRose.rgb)   > 0.01 ? p_colorRose.rgb   : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// Distance from p to segment ab.
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1.0e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

// Jittered node position for a graph cell, in graph units. Static (no
// orbit): activation times and pulse arrivals are derived from these
// positions, and a transition is too short for drift to read anyway.
vec2 graphNodePos(vec2 cell) {
    return cell + 0.2 + 0.6 * hash22(cell);
}

// Per-cell activation time in [0.02, ~0.77]: a directional ramp (the circuit
// lights up along the switch direction) plus a per-cell stagger so the
// front reads as signal propagation, not a straight wipe. `extent` is the
// graph-space screen extent, so the projection is normalized to [0,1]
// regardless of resolution or density.
//
// The projection is normalized by the direction's L1 extent (desktop-wipe's
// idiom) so proj spans exactly [0,1] corner-to-corner for ANY direction. The
// old fixed * 0.7071 was sized for the diagonal only: an axis-aligned switch
// (the common left/right case under p_followSwitch) compressed proj to
// [0.15, 0.85], so the first node lit at t ≈ 0.12 and the flood was done by
// t ≈ 0.79 — the same dead head/tail phosphor-peek had, which an ease-out
// progress curve stretches into a visible hang on a settled screen. The
// activation window's budget [0.02, 0.77] is sized against the flood: the
// slowest pixel clears at a_max + (0.12 + 1.13) / floodSpeed ≈ 0.98, so the
// reveal uses the whole [0,1] domain and the completion floor stays a
// backstop. ext >= 1 for a unit vector, so no division by zero.
float nodeActivation(vec2 nodePos, vec2 dir, vec2 extent) {
    vec2 nodeUV = nodePos / max(extent, vec2(1.0e-4));
    float ext = abs(dir.x) + abs(dir.y);
    float proj = clamp(dot(nodeUV - 0.5, dir) / ext + 0.5, 0.0, 1.0);
    float stagger = classicHash(floor(nodePos * 7.3));
    return 0.02 + proj * 0.61 + stagger * 0.14;
}

vec4 pTransition(vec2 uv, float t) {
    // ── Graph space: aspect-corrected uv scaled by circuit density, same
    // construction as phosphor-flux's signal-graph layer. ──
    vec2 res = resolutionSafe();
    float aspect = res.x / max(res.y, 1.0);
    float scale = max(p_graphScale, 2.0);
    vec2 extent = vec2(aspect, 1.0) * scale;
    vec2 q = uv * extent;
    vec2 cell = floor(q);

    // Direction: follow the actual switch (iSwitchDelta via switchDirection)
    // when p_followSwitch is on; the configured direction params are the
    // fallback and the forced direction when it is off. The default (1, 1)
    // keeps the brand's top-left-to-bottom-right diagonal.
    vec2 cfg = vec2(p_dirX, p_dirY);
    // Zero-direction guard: fall back to the brand diagonal instead of
    // epsilon-nudging into normalize (tiny opposing components can still
    // cancel a nudge into NaN — same guard idiom as phosphor-peek).
    vec2 rawDir = (p_followSwitch > 0.5) ? switchDirection(cfg) : cfg;
    vec2 dir = dot(rawDir, rawDir) > 1.0e-6 ? normalize(rawDir) : normalize(vec2(1.0, 1.0));

    // Feature sizes in graph units, derived from pixels so traces stay
    // hairline and pulses stay compact at any resolution.
    float pxInGraph = scale / max(res.y, 1.0);
    float lineW  = 1.5 * pxInGraph;
    float nodeR  = 3.0 * pxInGraph;
    float pulseR = 5.0 * pxInGraph;

    // Global envelope for the additive circuit: exactly 0 at both endpoints
    // so the settled desktops carry no residue, full through the middle.
    float env = smoothstep(0.0, 0.06, t) * (1.0 - smoothstep(0.86, 0.97, t));

    // Flood speed in graph units per unit progress. The latest activation is
    // ~0.77 and a pixel's own-cell node is at most ~1.13 units away, so the
    // slowest front clears its reveal smoothstep ((0.12 + 1.13) / 6 ≈ 0.21
    // after activating) at t ≈ 0.98 — deliberately just inside the endpoint,
    // so the reveal spans the whole timeline instead of settling early and
    // hanging (see nodeActivation).
    const float floodSpeed = 6.0;

    // m: signed distance of the furthest-advanced reveal front past this
    // fragment, maximized over the 3x3 node neighbourhood. Positive =
    // incoming desktop has reached this pixel.
    float m = -1.0e3;
    vec3 circuit = vec3(0.0);

    // 3x3 neighbourhood: each visited cell contributes its node, its reveal
    // flood, and its edges toward the right and downward neighbours, so
    // every edge is drawn exactly once and any fragment near a node, edge,
    // or front is covered.
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 cA = cell + vec2(float(dx), float(dy));
            vec2 posA = graphNodePos(cA);
            float aA = nodeActivation(posA, dir, extent);

            // ── Reveal flood: the incoming desktop grows radially out of
            // the node from the moment it activates. ──
            float dNode = length(q - posA);
            m = max(m, (t - aA) * floodSpeed - dNode);

            // ── Node dot: flares as its activation lands, holds a dimmer
            // core while its flood is still local, gone under env at the
            // endpoints. Squared via multiply — pow(x, 2.0) is undefined
            // for x < 0 per the GLSL spec (see phosphor-stream) and this
            // argument is negative before every activation. ──
            float fl = (t - aA) / 0.05;
            float flare = exp(-fl * fl);
            float hold  = smoothstep(aA - 0.02, aA + 0.02, t)
                        * (1.0 - smoothstep(aA + 0.12, aA + 0.30, t));
            float node = exp(-dNode * dNode / (nodeR * nodeR));
            vec3 nodeCol = fluxGradient(clamp(aA * 1.6 - 0.1, 0.0, 1.0));
            circuit += nodeCol * node * (2.2 * flare + 0.5 * hold);

            // ── Edges to the right / downward neighbours, sparsely gated so
            // the mesh reads as circuitry, not a grid. ──
            for (int e = 0; e < 2; e++) {
                vec2 cB = cA + (e == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
                float gate = classicHash(cA * 3.7 + float(e) * 13.1);
                if (gate < 0.45) continue;

                vec2 posB = graphNodePos(cB);
                float aB = nodeActivation(posB, dir, extent);

                // Order the endpoints by activation: the pulse departs the
                // earlier node and arrives exactly at the later node's
                // activation, so signal and reveal share one clock.
                vec2 pFrom = aA <= aB ? posA : posB;
                vec2 pTo   = aA <= aB ? posB : posA;
                float a0 = min(aA, aB);
                float a1 = max(aA, aB);

                // Trace lights when its source activates and fades shortly
                // after the pulse lands.
                float lit = smoothstep(a0 - 0.02, a0 + 0.03, t)
                          * (1.0 - smoothstep(a1 + 0.08, a1 + 0.25, t));
                if (lit <= 0.001) continue;

                // Inverted form rather than swapped arguments: smoothstep
                // is undefined when edge0 >= edge1 per the GLSL spec.
                float dEdge = sdSegment(q, pFrom, pTo);
                float line = 1.0 - smoothstep(lineW * 0.3, lineW, dEdge);
                vec3 edgeCol = fluxGradient(clamp(a0 * 1.6 - 0.1 + gate * 0.2, 0.0, 1.0));
                circuit += edgeCol * line * 0.35 * lit;

                // ── Travelling pulse with a short fading tail. The pulse
                // fades out over a short window after landing instead of
                // cutting — a step() kill at arrival would drop the full
                // gaussian brightness in one frame. ──
                float ph = clamp((t - a0) / max(a1 - a0, 0.04), 0.0, 1.0);
                float travelling = step(a0, t) * (1.0 - smoothstep(a1, a1 + 0.06, t));
                vec2 pulsePos = mix(pFrom, pTo, ph);
                float dPulse = length(q - pulsePos);
                float pulse = exp(-dPulse * dPulse / (pulseR * pulseR));
                vec2 tailPos = mix(pFrom, pTo, max(ph - 0.15, 0.0));
                float dTail = length(q - tailPos);
                pulse += 0.35 * exp(-dTail * dTail / (pulseR * pulseR * 2.0));

                vec3 pulseCol = fluxGradient(clamp(mix(a0, a1, ph) * 1.6 - 0.1, 0.0, 1.0));
                circuit += pulseCol * pulse * 1.4 * travelling;
            }
        }
    }

    // reveal: 0 = still outgoing desktop .. 1 = incoming desktop. The
    // completion floor guarantees a clean t = 1 endpoint even for a pixel
    // pathologically far from every node (the worst-case activation plus
    // travel time lands at ~0.98).
    float reveal = smoothstep(0.0, 0.12, m);
    reveal = max(reveal, smoothstep(0.92, 1.0, t));

    vec3 col = crossFade(uv, reveal).rgb;

    // ── Gradient front: a thin rim of brand light rides the flood boundary,
    // cyan-through-rose by how late the local front is, with a fine
    // per-frame grain so the light shimmers as it passes. iFrame is bound on
    // the desktop pass at contract parity, independent of the eased switch
    // progress. ──
    float front = smoothstep(-0.30, 0.0, m) * (1.0 - smoothstep(0.0, 0.30, m));
    float sparkle = 0.9 + 0.2 * classicHash(floor(uv * res / 2.0)
                                            + floor(float(iFrame) * 0.2));
    vec3 frontCol = fluxGradient(clamp(t * 1.5 - 0.15, 0.0, 1.0));
    // Fade the rim in over the first 5% of the leg: the band's -0.30 lead
    // puts the earliest-activating nodes inside it already at t=0, and an
    // ungated rim pops in on the very first frame against the previously
    // plain desktop. The settle side needs no gate (front is negligible by
    // t=1), and a full env gate would extinguish the rim on the last-landing
    // fronts, so gate the ramp-in only.
    float frontIn = smoothstep(0.0, 0.05, t);
    col += frontCol * front * front * frontIn * clamp(p_glow, 0.0, 2.0) * 0.5 * sparkle;

    // Additive circuit, gated by the global envelope and dimmed where the
    // incoming desktop has already settled so it never lingers as residue.
    col += circuit * env * clamp(p_traceOpacity, 0.0, 1.0)
                  * mix(1.0, 0.25, reveal) * clamp(p_glow * 0.5 + 0.5, 0.0, 1.5);

    // Two opaque desktops blended stay opaque — the pass draws with blending
    // off and replaces the screen, so alpha is a constant 1.
    return vec4(clamp(col, 0.0, 1.0), 1.0);
}
