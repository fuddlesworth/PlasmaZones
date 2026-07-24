// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Phosphor Flux — the official Phosphor brand overlay.
// Built from the brand identity (phosphor-works.github.io/brand): Φ is the
// symbol for luminous flux, so the look is light flowing through containment.
// Five layers over the deep-navy brand surfaces (#0B1730 → #050916):
//   1. Flux field    — two parallax depths of fbm-displaced ribbons of the
//                      four-stop accent gradient (cyan #22D3EE → blue #3B82F6
//                      → purple #A855F7 → rose #F43F5E) drifting in screen
//                      space, threaded with sharp bright filaments. Screen-
//                      space, so the light is continuous across zones.
//   2. Ember columns — rising comet-tailed dust (the phosphor-motes motif),
//                      hue climbing the gradient with altitude.
//   3. Signal graph  — a sparse node graph with bright pulses travelling the
//                      edges, the PhosphorShader "edit the signal, see the
//                      light" motif. Nodes flash as a pulse arrives.
//   4. Shell strokes — nested rounded-rect insets per zone, the PhosphorShell
//                      containment motif, breathing inward.
//   5. Gradient frame — every zone border samples ONE screen-diagonal brand
//                      gradient (the PlasmaZones mark), with a gleam orbiting
//                      each zone's perimeter (the border-phosphor motif).
// Audio is structural, not just a brightness lift: each ember column rides
// its own spectrum band (the dust IS the spectrum), the flux ribbons warp
// where the spectrum is loud, bass blooms graph nodes into mini plasma
// vortices (the phosphor-vortex arms) and breathes the shells outward, and
// the frame gleams orbit faster and hotter with the overall energy. Treble
// flashes nodes and shimmers the ember heads; mids drift the hue flow.

#include <audio.glsl>

// ═══════════════════════════════════════════════════════════════════════════════
// PARAMETERS
// ═══════════════════════════════════════════════════════════════════════════════

float getSpeed()            { return p_speed >= 0.0 ? p_speed : 1.0; }
float getFluxIntensity()    { return p_fluxIntensity >= 0.0 ? p_fluxIntensity : 0.7; }
float getFluxScale()        { return p_fluxScale >= 0.0 ? p_fluxScale : 1.6; }
float getGraphScale()       { return p_graphScale >= 0.0 ? p_graphScale : 5.0; }
float getGraphOpacity()     { return p_graphOpacity >= 0.0 ? p_graphOpacity : 0.4; }
float getPulseSpeed()       { return p_pulseSpeed >= 0.0 ? p_pulseSpeed : 0.5; }
float getShellCount()       { return p_shellCount >= 0.0 ? p_shellCount : 3.0; }
float getShellSpacing()     { return p_shellSpacing >= 0.0 ? p_shellSpacing : 14.0; }
float getShellOpacity()     { return p_shellOpacity >= 0.0 ? p_shellOpacity : 0.35; }
float getShellPulseSpeed()  { return p_shellPulseSpeed >= 0.0 ? p_shellPulseSpeed : 0.8; }
float getFillOpacity()      { return p_fillOpacity >= 0.0 ? p_fillOpacity : 0.92; }
float getGlowStrength()     { return p_glowStrength >= 0.0 ? p_glowStrength : 1.0; }
float getEmberIntensity()   { return p_emberIntensity >= 0.0 ? p_emberIntensity : 0.6; }
float getEmberDensity()     { return p_emberDensity >= 0.0 ? p_emberDensity : 28.0; }
float getGleamStrength()    { return p_gleamStrength >= 0.0 ? p_gleamStrength : 0.5; }
float getAudioSensitivity() { return p_audioSensitivity >= 0.0 ? p_audioSensitivity : 1.0; }
float getLabelBrightness()  { return p_labelBrightness >= 0.0 ? p_labelBrightness : 1.8; }

// ═══════════════════════════════════════════════════════════════════════════════
// BRAND PALETTE  (phosphor-works.github.io/palette, dark theme)
// ═══════════════════════════════════════════════════════════════════════════════

const vec3 kFluxCyan   = vec3(0.133, 0.827, 0.933);  // #22D3EE
const vec3 kFluxBlue   = vec3(0.231, 0.510, 0.965);  // #3B82F6
const vec3 kFluxPurple = vec3(0.659, 0.333, 0.969);  // #A855F7
const vec3 kFluxRose   = vec3(0.957, 0.247, 0.369);  // #F43F5E
const vec3 kNavy       = vec3(0.043, 0.090, 0.188);  // #0B1730

vec3 getFluxCyan()   { return colorWithFallback(p_colorCyan.rgb,   kFluxCyan); }
vec3 getFluxBlue()   { return colorWithFallback(p_colorBlue.rgb,   kFluxBlue); }
vec3 getFluxPurple() { return colorWithFallback(p_colorPurple.rgb, kFluxPurple); }
vec3 getFluxRose()   { return colorWithFallback(p_colorRose.rgb,   kFluxRose); }
vec3 getBackgroundColor() { return colorWithFallback(p_backgroundColor.rgb, kNavy); }

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(getFluxCyan(), getFluxBlue(), clamp(t, 0.0, 1.0));
    c = mix(c, getFluxPurple(), clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, getFluxRose(), clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// Seamless ping-pong of an unbounded coordinate into [0, 1].
float pingPong(float x) {
    x = fract(x);
    return 1.0 - abs(2.0 * x - 1.0);
}

// Shared screen-diagonal gradient coordinate (0 top-left, 1 bottom-right).
// All zones sample this same axis, forming one gradient frame together.
float screenDiag(vec2 fragCoord) {
    return (fragCoord.x + fragCoord.y) / max(iResolution.x + iResolution.y, 1.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 1: FLUX FIELD  (two parallax depths of luminous ribbons, screen-space)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 fluxField(vec2 screenUV, float t, float bass, float mids, float treble, bool hasAudio) {
    float intensity = getFluxIntensity();
    float scale     = getFluxScale();
    float audioSens = getAudioSensitivity();

    float aspect = iResolution.x / max(iResolution.y, 1.0);
    vec2 q = screenUV * vec2(aspect, 1.0) * scale;

    // Mids gently accelerate the hue flow (kept small so it stays ambient).
    float hueDrift = t * 0.02;
    if (hasAudio) {
        hueDrift += mids * audioSens * 0.06;
    }

    // ── Near depth: displacement field + two ribbon harmonics along the
    // brand diagonal, threaded with a sharp bright filament. With audio the
    // ribbon axis is warped by the spectrum at this column, so the aurora
    // physically ripples where the music is loud, not just brighter. ──
    float n = fbm(q * 1.4 + vec2(t * 0.05, -t * 0.03), 4, 0.55);
    float axis = q.x * 0.85 + q.y * 0.65;
    if (hasAudio) {
        axis += audioBarSmooth(screenUV.x) * audioSens * 1.3;
    }
    float band1  = sin(axis * 2.2 + n * 3.0 + t * 0.22) * 0.5 + 0.5;
    float band2  = sin(axis * 4.6 - n * 2.2 - t * 0.13 + 1.7) * 0.5 + 0.5;
    float thread = sin(axis * 8.5 + n * 4.5 + t * 0.31) * 0.5 + 0.5;
    float ribbonNear = pow(band1, 3.0) * 0.8 + pow(band2, 4.0) * 0.4 + pow(thread, 14.0) * 0.9;
    vec3 colNear = fluxGradient(pingPong(screenUV.x * 0.6 + n * 0.30 + hueDrift));

    // ── Far depth: broader, slower, dimmer ribbons on their own field —
    // the parallax difference is what gives the light a sense of depth. ──
    float n2 = fbm(q * 0.6 - vec2(t * 0.025, t * 0.015) + 3.7, 3, 1.3);
    float bandDeep = sin(axis * 1.2 + n2 * 2.4 - t * 0.09 + 0.8) * 0.5 + 0.5;
    float threadDeep = sin(axis * 5.3 - n2 * 3.1 + t * 0.17 + 2.5) * 0.5 + 0.5;
    float ribbonFar = pow(bandDeep, 4.0) * 0.55 + pow(threadDeep, 10.0) * 0.35;
    vec3 colFar = fluxGradient(pingPong(screenUV.y * 0.5 + n2 * 0.25 - hueDrift + 0.4));

    float lift = 1.0;
    float sparkleAmt = 0.0;
    if (hasAudio) {
        // Bass floods the field with light; treble adds a fine sparkle grain.
        lift += bass * audioSens * 0.9;
        float sparkle = hash21(floor(screenUV * iResolution / 3.0) + floor(t * 6.0));
        sparkleAmt = step(0.96, sparkle) * treble * audioSens * 1.5;
    }

    vec3 field = colNear * ribbonNear + colFar * ribbonFar * 0.6;
    return field * intensity * 0.5 * (lift + sparkleAmt);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 2: EMBER COLUMNS  (rising luminous dust, the phosphor-motes motif —
// and the spectrum made visible: each column is driven by its own band)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 emberRise(vec2 screenUV, float t, float treble, bool hasAudio) {
    float intensity = getEmberIntensity();
    if (intensity <= 0.001) return vec3(0.0);
    float density   = max(getEmberDensity(), 4.0);
    float audioSens = getAudioSensitivity();

    // One ember per column; the fragment only ever needs its own column
    // (the sway stays inside the column width), so this is a single eval.
    float colW = 1.0 / density;
    float col  = floor(screenUV.x / colW);
    float cx   = (col + 0.5) * colW;
    float seed = hash11(col * 7.13 + 0.37);

    // The column's own spectrum band drives it: loud bands shoot bright,
    // fast, long-tailed embers; quiet bands (and no audio at all) fall back
    // to a sparse ambient drift.
    float band = hasAudio ? audioBarSmooth(cx) * audioSens : 0.0;

    // Sparse ambient occupancy; energy wakes more columns up. Occupancy is
    // sampled on a slow (~6.7s) epoch clock; cross-fade into the next epoch
    // over its final stretch so a column that goes vacant dims its ember out
    // instead of cutting it off mid-flight when the epoch ticks.
    float epoch = t * 0.15;
    float occGate = 0.35 + band * 0.6;
    float occNow  = step(hash11(col * 3.71 + floor(epoch)), occGate);
    float occNext = step(hash11(col * 3.71 + floor(epoch) + 1.0), occGate);
    float occupancy = mix(occNow, occNext, smoothstep(0.8, 1.0, fract(epoch)));
    if (occupancy <= 0.001) return vec3(0.0);

    float yUp = 1.0 - screenUV.y;
    float rate = 0.05 + 0.10 * seed + band * 0.35;
    float altitude = fract(seed * 7.0 - t * rate);

    // Motes-style wander, kept inside the column.
    float sway = sin(yUp * 7.0 + seed * TAU + t * 0.6) * colW * 0.30;
    float dx = screenUV.x - (cx + sway);
    float dy = yUp - altitude; // >0 above the head

    // Head plus a comet tail below it; energy stretches the tail.
    float tailLen = 0.05 + band * 0.10;
    float head = exp(-dx * dx / (colW * colW * 0.045)) * exp(-dy * dy / 0.00012);
    float tail = exp(-dx * dx / (colW * colW * 0.02))
               * (dy < 0.0 ? exp(dy / tailLen) : 0.0);

    // Fade in low, burn out at altitude (the motes life envelope), with a
    // treble shimmer on the head.
    float life = smoothstep(0.0, 0.12, altitude) * (1.0 - smoothstep(0.70, 0.95, altitude));
    float shimmer = 1.0;
    if (hasAudio) {
        shimmer += treble * audioSens * 0.8 * hash11(col + floor(t * 8.0));
    }

    // Hue climbs the gradient with altitude — cyan low, rose at burnout.
    vec3 c = fluxGradient(altitude * 0.85 + seed * 0.15);
    float bright = (0.25 + band * 1.6) * intensity;
    return c * (head * shimmer + tail * 0.5) * life * bright * occupancy;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 3: SIGNAL GRAPH  (node graph with pulses travelling the edges —
// the PhosphorShader "edit the signal, see the light" motif)
// ═══════════════════════════════════════════════════════════════════════════════

// Jittered node position for a graph cell, with a slow per-node orbit so the
// mesh feels alive rather than pinned.
vec2 graphNodePos(vec2 cell, float t) {
    vec2 h = hash22(cell);
    vec2 base = cell + 0.2 + 0.6 * h;
    return base + 0.05 * vec2(sin(t * 0.3 + h.x * TAU), cos(t * 0.26 + h.y * TAU));
}

vec3 signalGraph(vec2 screenUV, float t, float diag,
                 float bass, float treble, bool hasAudio) {
    float opacity = getGraphOpacity();
    if (opacity <= 0.001) return vec3(0.0);

    // Floor guards the gaussian denominators below (nodeR/pulseR derive
    // from scale): the p_x >= 0 sentinel check doesn't range-clamp, so a
    // hand-edited graphScale of 0 would otherwise divide by zero. Same
    // defence emberRise applies to its density.
    float scale     = max(getGraphScale(), 1.0);
    float pulseSpd  = getPulseSpeed();
    float audioSens = getAudioSensitivity();

    float aspect = iResolution.x / max(iResolution.y, 1.0);
    vec2 q = screenUV * vec2(aspect, 1.0) * scale;
    vec2 cell = floor(q);

    // Feature sizes in graph units, derived from pixels so lines stay hairline
    // and pulses stay compact at any resolution.
    float pxInGraph = scale / max(iResolution.y, 1.0);
    float lineW  = 1.5 * pxInGraph;
    float nodeR  = 2.5 * pxInGraph;
    float pulseR = 4.0 * pxInGraph;

    float pulseLift = hasAudio ? 1.0 + bass * audioSens * 1.4 : 1.0;

    vec3 result = vec3(0.0);

    // 3x3 neighbourhood: each visited cell contributes its node plus its
    // edges toward the right and downward neighbours, so every edge is drawn
    // exactly once and any fragment near a node or edge is covered.
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 cA = cell + vec2(float(dx), float(dy));
            vec2 posA = graphNodePos(cA, t);

            // ── Node dot, with a treble-gated random flash. ──
            float dNode = length(q - posA);
            float node = exp(-dNode * dNode / (nodeR * nodeR));
            float nodeBright = 0.5;
            if (hasAudio && treble > 0.2) {
                float flash = hash21(cA + vec2(floor(t * 4.0), 11.3));
                nodeBright += step(0.9, flash) * treble * audioSens * 1.2;
            }
            vec3 nodeCol = fluxGradient(pingPong(diag * 1.5 + hash21(cA) * 0.3));
            result += nodeCol * node * nodeBright;

            // ── Bass bloom: on a strong low hit, gated nodes flare into a
            // mini plasma vortex — the phosphor-vortex arm math at cell
            // scale, spinning while it lasts. ──
            if (hasAudio && bass > 0.45) {
                float bloomGate = step(0.55, hash21(cA + vec2(3.3, 9.1)));
                if (bloomGate > 0.0) {
                    vec2 nrel = q - posA;
                    float nr = length(nrel) * 3.0;
                    float nAng = atan(nrel.y, nrel.x);
                    float arm = sin(nAng * 3.0 + nr * 4.0 - t * 3.0 + hash21(cA) * TAU);
                    float bloom = exp(-nr * nr) * pow(0.5 + 0.5 * arm, 3.0);
                    result += nodeCol * bloom * (bass - 0.45) * audioSens * 2.4;
                }
            }

            // ── Edges to the right / downward neighbours, sparsely gated so
            // the mesh reads as circuitry, not a grid. ──
            for (int e = 0; e < 2; e++) {
                vec2 cB = cA + (e == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
                float gate = hash21(cA * 3.7 + float(e) * 13.1);
                if (gate < 0.45) continue;

                vec2 posB = graphNodePos(cB, t);
                float dEdge = sdSegment(q, posA, posB);
                float line = smoothstep(lineW, lineW * 0.3, dEdge);
                vec3 edgeCol = fluxGradient(pingPong(diag * 1.5 + gate * 0.25));
                result += edgeCol * line * 0.14;

                // ── Travelling pulse: one bright signal per active edge on
                // its own clock. The pulse leaves a short fading tail and
                // splashes the destination node as it arrives. ──
                float ph = fract(t * pulseSpd * (0.4 + 0.6 * gate) + gate * 7.0);
                vec2 pulsePos = mix(posA, posB, ph);
                float dPulse = length(q - pulsePos);
                float pulse = exp(-dPulse * dPulse / (pulseR * pulseR));
                // Tail: a dimmer echo trailing the head along the edge.
                vec2 tailPos = mix(posA, posB, max(ph - 0.12, 0.0));
                float dTail = length(q - tailPos);
                pulse += 0.35 * exp(-dTail * dTail / (pulseR * pulseR * 2.0));
                // Arrival splash on the destination node.
                float splash = smoothstep(0.85, 1.0, ph);
                pulse += splash * exp(-pow(length(q - posB), 2.0) / (nodeR * nodeR * 4.0));

                vec3 pulseCol = fluxGradient(pingPong(diag * 1.5 + gate * 0.25 + ph * 0.2));
                result += pulseCol * pulse * 0.9 * pulseLift;
            }
        }
    }

    return result * opacity;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 4: CONTAINMENT SHELLS  (nested rounded-rect strokes, PhosphorShell motif)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 shellStrokes(float d, float diag, float t, vec2 rectSize,
                  float bass, bool hasAudio) {
    float count   = clamp(getShellCount(), 0.0, 4.0);
    // zoneLen(), not pxScale(): the shells are insets measured against `d`, the
    // device-px zone SDF, so a 1080p-relative step would drift away from the
    // rounded corner it nests inside as the display scale changes.
    float spacing = zoneLen(getShellSpacing());
    float alpha   = getShellOpacity();
    float pulseSpd = getShellPulseSpeed();

    // Bass pushes the whole containment outward — the shells visibly
    // breathe with the low end instead of only glowing.
    if (hasAudio) {
        spacing *= 1.0 + bass * getAudioSensitivity() * 0.18;
    }

    // Never inset past the zone midline (small zones drop outer shells).
    float maxInset = min(rectSize.x, rectSize.y) * 0.42;

    vec3 result = vec3(0.0);
    float falloff = 1.0;
    for (int k = 0; k < 4; k++) {
        if (float(k) >= count) break;
        float inset = float(k + 1) * spacing;
        if (inset > maxInset) break;

        // Stroke of the inset rounded rect: |d + inset| ≈ 0.
        float stroke = 1.0 - smoothstep(zoneLen(0.8), zoneLen(2.2), abs(d + inset));

        // Containment breath: a slow pulse travelling inward shell by shell.
        float breath = 0.65 + 0.35 * sin(t * pulseSpd - float(k) * 1.2);

        // Each shell sits a step further along the brand gradient.
        vec3 col = fluxGradient(pingPong(diag + 0.07 * float(k + 1)));

        result += col * stroke * alpha * falloff * breath;
        falloff *= 0.62;
    }
    // Dormant dimming is owned by the caller's whole-fx vitality scale —
    // scaling here too would double-apply and leave dormant shells darker
    // than the sibling layers.
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZONE RENDERING
// ═══════════════════════════════════════════════════════════════════════════════

vec4 renderFluxZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                    vec4 params, bool isHighlighted,
                    float bass, float mids, float treble, float overall, bool hasAudio)
{
    float vitality = zoneVitality(isHighlighted);
    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);
    float borderWidth  = zoneBorderWidth(params.y);

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = zoneShape.center;  // already computed by zoneSdf()
    vec2 p        = fragCoord - center;

    float d = zoneShape.d;

    float t    = iTime * getSpeed();
    float diag = screenDiag(fragCoord);
    vec2 screenUV = fragCoord / max(iResolution, vec2(1.0));

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        // Brand surface: navy at the top of the screen sinking toward void.
        vec3 navy = getBackgroundColor();
        vec3 deep = navy * 0.42;  // ≈ #050916 from the default navy
        vec3 baseColor = mix(navy, deep, smoothstep(0.0, 1.0, screenUV.y));
        // The pack's own fillOpacity is the sole fill alpha, catalog-wide.
        // The zone's activeOpacity arrives in fillColor.a, but only four packs
        // ever multiplied it in, so it was inert in the other 23 and the split
        // just made the same setting behave differently per pack.
        float bgAlpha = getFillOpacity();

        vec3 fx = vec3(0.0);

        // Layer 1: luminous flux field (continuous across zones).
        fx += fluxField(screenUV, t, bass, mids, treble, hasAudio);

        // Layer 2: rising ember columns (spectrum-driven when audio plays).
        fx += emberRise(screenUV, t, treble, hasAudio);

        // Layer 3: signal graph with travelling pulses and bass blooms.
        fx += signalGraph(screenUV, t, diag, bass, treble, hasAudio);

        // Layer 4: nested containment shells, breathing with the low end.
        fx += shellStrokes(d, diag, t, rectSize, bass, hasAudio);

        // Vitality: dormant zones carry the light dimly.
        fx *= vitalityScale(0.5, 1.0, vitality);

        result.rgb = baseColor + fx;
        result.a = bgAlpha;
        result.rgb = vitalityDesaturate(result.rgb, vitality);
    }

    // Layer 5: the shared gradient frame. A gentle luminance wave slides
    // along the diagonal, and a gleam orbits each zone's perimeter (the
    // border-phosphor motif) — faster and hotter as the music energy rises.
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 edgeColor = borderColor.rgb;
        if (length(edgeColor) < 0.01) {
            edgeColor = fluxGradient(diag);
        }
        float wave = 0.85 + 0.30 * sin(diag * TAU * 2.0 - t * 0.9);
        edgeColor *= wave * vitalityScale(0.9, 1.35, vitality);

        float gleamStr = getGleamStrength();
        if (gleamStr > 0.001) {
            // Frame-normalised perimeter angle (the framePerimeter
            // approximation: uniform speed per side on non-square zones).
            float u = atan(p.y / max(rectSize.y * 0.5, 1.0),
                           p.x / max(rectSize.x * 0.5, 1.0)) / TAU;
            float energy = hasAudio ? overall * getAudioSensitivity() : 0.0;
            float phase = fract(u - t * 0.10 * (1.0 + energy * 2.0)
                                + hash21(rectPos) * 0.7);
            float ringDist = min(phase, 1.0 - phase);
            float gleam = exp(-ringDist * ringDist * 900.0)
                        * gleamStr * (0.6 + energy * 1.2);
            edgeColor = mix(edgeColor, vec3(1.0), clamp(gleam, 0.0, 0.85));
        }

        edgeColor = vitalityDesaturate(edgeColor, vitality);
        result.rgb = mix(result.rgb, edgeColor, border * vitalityScale(0.65, 0.95, vitality));
        // borderColor.a straight, no 0.85 floor. The floor meant lowering a
    // zone's opacity could not fade its frame below 85%, so the setting
    // stopped working partway down its range.
    result.a = max(result.a, border * borderColor.a);
    }

    return result;
}

// Outer glow only — separate pass (same reasoning as aretha-shell: glow alpha
// from one zone must not darken an adjacent zone's fill during blendOver).
vec4 fluxZoneGlow(vec2 fragCoord, vec4 rect, vec4 params, bool isHighlighted) {
    float vitality = zoneVitality(isHighlighted);
    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);

    float d = zoneShape.d;

    float glowExtent = zoneLen(vitalityScale(12.0, 30.0, vitality));
    if (d > 0.0 && d < glowExtent) {
        float glowSize = zoneLen(vitalityScale(4.0, 9.0, vitality));
        float glowStr  = vitalityScale(0.10, 0.35, vitality) * getGlowStrength();
        float glow = expGlow(d, glowSize, glowStr);
        vec3 glowColor = vitalityDesaturate(fluxGradient(screenDiag(fragCoord)), vitality);
        return vec4(glowColor * glow, glow * vitalityScale(0.22, 0.55, vitality));
    }
    return vec4(0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LABEL COMPOSITE  (soft gradient halo around the zone labels)
// ═══════════════════════════════════════════════════════════════════════════════

vec4 compositeFluxLabels(vec4 color, vec2 fragCoord) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 haloColor = fluxGradient(screenDiag(fragCoord));

    // Gaussian halo around the glyphs, tinted by the local gradient.
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * 2.0).a * w;
        }
    }
    halo /= 16.5;
    float outline = halo * (1.0 - labels.a);

    if (outline > 0.01) {
        color.rgb += haloColor * outline * 0.55;
        color.a = max(color.a, outline * 0.45);
    }

    if (labels.a > 0.01) {
        // The glyph core carries the local gradient colour itself, lifted
        // toward white, so labels read as lit glass over the dark surface.
        vec3 core = color.rgb * getLabelBrightness();
        core += haloColor * 0.55 + vec3(0.18);
        color.rgb = mix(color.rgb, core, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

vec4 pImage(vec2 fragCoord) {
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        return vec4(0.0);
    }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    // Pass 1: zone fills + gradient frame.
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderFluxZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    // Pass 2: outer glows (additive, after all fills are composited).
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 glow = fluxZoneGlow(fragCoord, rect, zoneParams[i], isHighlighted);
        if (glow.a > 0.0) {
            color.rgb += glow.rgb;
            color.a = max(color.a, glow.a);
        }
    }

    if (p_showLabels > 0.5)
        color = compositeFluxLabels(color, fragCoord);
    return color;
}
