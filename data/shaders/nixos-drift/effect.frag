// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NIXOS DRIFT - Fragment Shader (Twilight Snowflake -- Multi-Instance)
//
// NixOS snowflake logo rendered as 6-fold SDF geometry with two-tone
// arm gradients matching the official SVG, drifting through a hexagonal
// circuit-trace lattice.
//
// Logo geometry: 9-vertex polygon arm rotated 6x for full snowflake.
// Two-tone coloring: alternating light/dark arms from SVG gradients.
//   Dark arms (#415E9A -> #5277C3)  Light arms (#699AD7 -> #7EBAE4)
//
// Logo effects (12 layers, all unique):
//   Frost crystallization halo (ice dendrites growing from edges),
//   hex-shaped bass shockwave rings, interior hex micro-lattice,
//   convergence spiral vortex, crystal refraction beams (6-axis prism),
//   per-arm aurora ripples, edge energy lines, arm tip glow beacons,
//   data packet traces along polygon edges, checksum verification rings
//   (segmented arcs with differential rotation + moire),
//   treble-triggered arm flash to white.
//
// Background: Voronoi crystal field + hex circuit traces
// Ambient: derivation tree DAG (branching build-propagation lines),
//          constellation network on Lissajous orbits
// Border: sequential hex cell lighting with traveling activation wave
//
// Audio reactivity:
//   Bass  = shockwaves + dendrite growth + build propagation + vortex +
//           refraction speed + convergence glow + constellation scatter
//   Mids  = aurora ripples + lattice breathe + gradient shift + spiral
//   Treble = checksum bit-flips + arm flash + data packets + dot glow

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// -- NixOS palette constants (all from official SVG gradients) ----
// Dark gradient:  #415E9A -> #5277C3
// Light gradient: #699AD7 -> #7EBAE4
const vec3 NIX_TWILIGHT = vec3(0.255, 0.369, 0.604);   // #415E9A -- darkest (dark grad start)
const vec3 NIX_DEEP     = vec3(0.322, 0.467, 0.765);   // #5277C3 -- dark gradient end
const vec3 NIX_SKY      = vec3(0.412, 0.604, 0.843);   // #699AD7 -- light gradient start
const vec3 NIX_GLOW     = vec3(0.494, 0.729, 0.894);   // #7EBAE4 -- light gradient mid


// -- SDF primitives -----------------------------------------------

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}


// -- Palette function ---------------------------------------------

vec3 nixPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else               return mix(accent, primary, (t - 0.66) * 3.0);
}


// =================================================================
//  NIXOS SNOWFLAKE LOGO -- SDF GEOMETRY (6-fold symmetric)
// =================================================================
//
//  One arm polygon extracted from the official NixOS SVG,
//  rotated 6x (every 60 degrees) to form the full snowflake.
//  Signed distance is negative inside, positive outside.

const vec2 LOGO_CENTER = vec2(0.50, 0.50);

const int ARM_N = 9;
const vec2 ARM[9] = vec2[9](
    vec2(-0.148147,  0.008202),
    vec2( 0.037049,  0.329007),
    vec2(-0.048060,  0.329805),
    vec2(-0.097503,  0.243617),
    vec2(-0.147299,  0.329345),
    vec2(-0.189586,  0.329328),
    vec2(-0.211245,  0.291910),
    vec2(-0.140301,  0.169923),
    vec2(-0.190662,  0.082285)
);

// AABB for a single arm (computed from vertices)
const vec2 ARM_AABB_LO = vec2(-0.212,  0.008);
const vec2 ARM_AABB_HI = vec2( 0.038,  0.330);


// -- Signed distance to single arm polygon (IQ winding-number) --
float sdPolygonArm(vec2 p) {
    // AABB early-out
    vec2 dLo = ARM_AABB_LO - p;
    vec2 dHi = p - ARM_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) {
        return sqrt(boxDist2);
    }

    float d = dot(p - ARM[0], p - ARM[0]);
    float s = 1.0;
    for (int i = 0, j = ARM_N - 1; i < ARM_N; j = i, i++) {
        vec2 e = ARM[j] - ARM[i];
        vec2 w = p - ARM[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= ARM[i].y,
                            p.y <  ARM[j].y,
                            e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    return s * sqrt(d);
}


// -- Rotate a 2D point by angle ----------------------------------
vec2 rot2D(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}


// -- Full snowflake SDF with facet tracking -----------------------
// Returns: .x = signed distance, .y = closest arm index (0-5)
vec2 snowflakeSDF(vec2 p) {
    float bestDist = 1e9;
    float bestArm = 0.0;

    // Quick radial early-out: snowflake fits in ~0.38 radius
    float r = length(p);
    if (r > 0.55) return vec2(r - 0.38, 0.0);

    for (int i = 0; i < 6; i++) {
        float angle = float(i) * TAU / 6.0;
        vec2 rp = rot2D(p, -angle);
        float d = sdPolygonArm(rp);
        if (d < bestDist) {
            bestDist = d;
            bestArm = float(i);
        }
    }
    return vec2(bestDist, bestArm);
}


// =================================================================
//  EFFECT 1: HEX CIRCUIT TRACES (background)
// =================================================================
//  Hexagonal grid with animated data pulses traveling along edges.
//  Fundamentally different from sine-wave lines or diamond grids.

vec3 hexCircuitTraces(vec2 uv, float time, float bassEnv, float midsEnv, float trebleEnv,
                      vec3 palPrimary, vec3 palSecondary, vec3 palAccent) {
    vec3 col = vec3(0.0);
    float scale = 5.0;
    vec2 p = uv * scale;

    // Hex grid using axial coordinates
    const vec2 s = vec2(1.0, 1.7320508);  // (1, sqrt(3))
    vec2 a = mod(p, s) - s * 0.5;
    vec2 b = mod(p - s * 0.5, s) - s * 0.5;

    // Determine which hex cell we are in
    bool useA = dot(a, a) < dot(b, b);
    vec2 gv = useA ? a : b;
    vec2 id = p - gv;

    // Distance to nearest hex edge (for grid lines)
    // Hex edge distance: max of 3 axis projections
    float hexEdge = max(abs(gv.x),
                        abs(gv.x * 0.5 + gv.y * 0.866025));
    hexEdge = max(hexEdge, abs(-gv.x * 0.5 + gv.y * 0.866025));
    float hexRadius = 0.5;
    float edgeDist = hexRadius - hexEdge;

    // Faint hex grid lines
    float gridLine = smoothstep(0.04, 0.0, edgeDist) * 0.12;
    col += palPrimary * 0.3 * gridLine;

    // Compute 3 hex edge axes for trace routing
    // Each hex has 3 edge directions: 0, 60, 120 degrees
    float traceSpeed = 0.4 + bassEnv * 1.2;
    vec3 traceTint = vec3(0.0);

    for (int axis = 0; axis < 3; axis++) {
        float axAngle = float(axis) * PI / 3.0;
        vec2 axDir = vec2(cos(axAngle), sin(axAngle));

        // Project gv onto this axis to get distance to edge along it
        float proj = dot(gv, axDir);
        float perpDist = abs(dot(gv, vec2(-axDir.y, axDir.x)));

        // Distance to this hex edge pair
        float edgeD = abs(abs(proj) - hexRadius * 0.5);
        float onEdge = smoothstep(0.06, 0.0, edgeD) * smoothstep(0.06, 0.0, perpDist);

        // Generate traveling pulses along this edge
        // Each edge gets a unique hash from cell id + axis
        float edgeHash = hash21(id + float(axis) * 73.0);
        // Pulse position travels along the edge
        float pulsePhase = fract(time * traceSpeed * (0.5 + edgeHash * 0.5) + edgeHash * TAU);
        float pulsePosOnEdge = pulsePhase * 2.0 - 1.0;  // -1 to 1
        float pulseProj = proj / (hexRadius * 0.5 + 0.001);  // normalize to -1..1

        // Pulse brightness: bright head, fading tail
        float pulseDist = abs(pulseProj - pulsePosOnEdge);
        float tailLen = 0.5 + bassEnv * 0.6;
        float pulseHead = smoothstep(tailLen, 0.0, pulseDist);
        // Only the head is bright; trail fades
        float headBright = smoothstep(0.15, 0.0, pulseDist);
        float pulse = (pulseHead * 0.4 + headBright * 0.6);

        // Treble triggers additional flash pulses
        float trebleFlash = trebleEnv * step(0.8, hash21(id + floor(time * 0.8) + float(axis) * 31.0));
        pulse += trebleFlash * 0.8;

        // Color: mids shift through palette
        float colorT = edgeHash + midsEnv * 0.3 + time * 0.05;
        vec3 pColor = nixPalette(colorT, palPrimary, palSecondary, palAccent);

        // Combine: trace is visible where we are near an edge AND pulse is passing
        float traceMask = onEdge * pulse;
        traceTint += pColor * traceMask;
    }

    // Bass brightens all traces globally
    float bassBright = 1.0 + bassEnv * 1.0;
    col += traceTint * bassBright * 0.8;

    // Subtle cell center glow on random cells (ambient warmth)
    float centerDist = length(gv);
    float cellGlow = smoothstep(0.3, 0.0, centerDist) * 0.03;
    float cellHash = hash21(id);
    col += nixPalette(cellHash + time * 0.02, palPrimary, palSecondary, palAccent) * cellGlow;

    return col;
}


// =================================================================
//  EFFECT 2: CONSTELLATION NETWORK (particles)
// =================================================================
//  Sparse bright dots on orbital Lissajous paths with connecting
//  lines between nearby dots. NOT grid-based, NOT falling.

const int CONSTELLATION_COUNT = 20;

vec3 constellationNetwork(vec2 uv, float time, float bassEnv, float midsEnv, float trebleEnv,
                          float particleStr, vec3 palAccent, vec3 palGlow) {
    vec3 col = vec3(0.0);

    // Precompute dot positions (Lissajous orbits with unique seeds)
    vec2 dots[20];
    for (int i = 0; i < CONSTELLATION_COUNT; i++) {
        float fi = float(i);
        float h1 = hash21(vec2(fi * 7.13, 3.91));
        float h2 = hash21(vec2(fi * 11.37, 17.53));
        float h3 = hash21(vec2(fi * 5.79, 23.11));

        // Lissajous curve parameters -- each dot has a unique orbit
        float freqX = 0.15 + h1 * 0.2;
        float freqY = 0.12 + h2 * 0.18;
        float phaseX = h1 * TAU;
        float phaseY = h2 * TAU;
        float ampX = 0.3 + h3 * 0.15;
        float ampY = 0.25 + h1 * 0.15;

        // Bass scatters dots slightly outward from center
        float scatter = 1.0 + bassEnv * 0.3;

        vec2 pos = vec2(
            0.5 + timeSin(freqX, phaseX) * ampX * scatter,
            0.5 + timeCos(freqY, phaseY) * ampY * scatter
        );
        dots[i] = pos;
    }

    // Render dots and connections
    float connectionThreshold = 0.18 + midsEnv * 0.10;

    for (int i = 0; i < CONSTELLATION_COUNT; i++) {
        vec2 dPos = dots[i];
        float dist = length(uv - dPos);
        float fi = float(i);

        // Dot glow: treble makes dots brighter
        float dotBright = 0.6 + bassEnv * 0.3 + trebleEnv * 0.4 * step(0.75, hash21(vec2(fi, floor(time * 0.6))));
        float dotRadius = 0.006 + bassEnv * 0.002;
        float dotMask = smoothstep(dotRadius, dotRadius * 0.1, dist);
        float dotHalo = exp(-dist * 60.0) * (0.15 + bassEnv * 0.15);

        float h = hash21(vec2(fi * 3.17, 9.31));
        vec3 dotColor = mix(palAccent, palGlow, h);
        col += dotColor * (dotMask + dotHalo) * dotBright * particleStr;

        // Connection lines to other nearby dots
        for (int j = i + 1; j < CONSTELLATION_COUNT; j++) {
            vec2 dPos2 = dots[j];
            float dotDist = length(dPos - dPos2);

            if (dotDist < connectionThreshold) {
                // Line segment distance
                float lineDist = sdSegment(uv, dPos, dPos2);
                float lineWidth = 0.002;
                float lineMask = smoothstep(lineWidth, lineWidth * 0.15, lineDist);

                // Fade based on distance between dots (closer = brighter)
                float proximity = 1.0 - dotDist / connectionThreshold;
                proximity *= proximity;

                // Subtle pulse along the line
                float linePhase = fract(time * 0.3 + hash21(vec2(fi, float(j))) * TAU);
                float linePulse = 0.5 + 0.5 * sin(linePhase * TAU);

                col += palAccent * 0.25 * lineMask * proximity * linePulse * particleStr;
            }
        }
    }

    return col;
}


// =================================================================
//  CRYSTALLINE REFRACTION BEAMS (inside snowflake)
// =================================================================
//  Light rays refracting through the ice-crystal structure of the
//  snowflake, bouncing at 60-degree hexagonal angles. Outgoing and
//  reflected pulses create a living prism effect unique to NixOS.

vec3 crystalRefraction(vec2 p, float time, float bassEnv, float midsEnv,
                        vec3 palAccent, vec3 palGlow) {
    vec3 col = vec3(0.0);
    float r = length(p);

    for (int i = 0; i < 6; i++) {
        float beamAngle = float(i) * TAU / 6.0;
        vec2 beamDir = vec2(cos(beamAngle), sin(beamAngle));
        vec2 beamPerp = vec2(-beamDir.y, beamDir.x);

        float along = dot(p, beamDir);
        float perp = abs(dot(p, beamPerp));

        // Beam narrows toward tip, widens at center
        float beamWidth = 0.006 + r * 0.018;
        float beam = smoothstep(beamWidth, beamWidth * 0.15, perp);

        // Outgoing pulse
        float pulseSpeed = 1.5 + bassEnv * 0.8;
        float phase1 = fract(time * pulseSpeed * 0.3 + float(i) * 0.167);
        float pos1 = phase1 * 0.7 - 0.05;
        float pulse1 = exp(-(along - pos1) * (along - pos1) * 200.0);

        // Reflected pulse (inward)
        float phase2 = fract(-time * pulseSpeed * 0.2 + float(i) * 0.167 + 0.5);
        float pos2 = phase2 * 0.7 - 0.05;
        float pulse2 = exp(-(along - pos2) * (along - pos2) * 300.0) * 0.6;

        // Prismatic color shift per beam
        vec3 beamCol = mix(palAccent, palGlow,
                           0.5 + 0.5 * sin(float(i) / 6.0 * TAU + time * 0.5));

        col += beamCol * beam * (pulse1 + pulse2) * (0.4 + midsEnv * 0.5);
    }

    return col;
}


// =================================================================
//  DATA PACKET TRACES (along arm polygon edges)
// =================================================================
//  Small bright dots that travel along the snowflake arm outlines
//  like data packets in a circuit. Follows the actual SDF polygon
//  vertices -- unique to the 6-fold NixOS geometry.

float armEdgeTrace(vec2 p, float time, int armIdx, float trebleEnv, float bassEnv) {
    float totalGlow = 0.0;
    float armAngle = float(armIdx) * TAU / 6.0;

    for (int pkt = 0; pkt < 3; pkt++) {
        float seed = float(armIdx) * 7.0 + float(pkt) * 13.0;
        float h = hash21(vec2(seed, 3.17));
        float speed = 0.8 + h * 1.2 + trebleEnv * 0.08;
        float phase = fract(time * speed * 0.15 + h);

        // Interpolate along the ARM polygon edges
        float pathT = phase * float(ARM_N);
        int segIdx = int(pathT) % ARM_N;
        int nextIdx = (segIdx + 1) % ARM_N;
        float segFract = fract(pathT);

        vec2 v0 = rot2D(ARM[segIdx], armAngle);
        vec2 v1 = rot2D(ARM[nextIdx], armAngle);
        vec2 packetPos = mix(v0, v1, segFract);

        float dist = length(p - packetPos);

        // Bright point + bloom
        float point = exp(-dist * dist * 8000.0) * 1.2;
        float bloom = exp(-dist * 60.0) * 0.3;

        totalGlow += (point + bloom) * (0.5 + bassEnv * 0.3);
    }

    return totalGlow;
}


// =================================================================
//  INTERIOR HEX MICRO-LATTICE (ice crystal structure)
// =================================================================
//  Tiny hexagonal grid visible inside the snowflake, giving it an
//  ice-crystal internal structure. None of the other shaders have
//  interior geometric patterning like this.

float hexMicroLattice(vec2 p, float time, float midsEnv) {
    float scale = 45.0;
    vec2 hp = p * scale;

    const vec2 hs = vec2(1.0, 1.7320508);
    vec2 ha = mod(hp, hs) - hs * 0.5;
    vec2 hb = mod(hp - hs * 0.5, hs) - hs * 0.5;
    vec2 hgv = dot(ha, ha) < dot(hb, hb) ? ha : hb;

    float hexE = max(abs(hgv.x), abs(hgv.x * 0.5 + hgv.y * 0.866025));
    hexE = max(hexE, abs(-hgv.x * 0.5 + hgv.y * 0.866025));

    float lines = smoothstep(0.5, 0.42, hexE) * 0.15;
    lines *= 0.6 + 0.4 * sin(time * 0.8 + length(p) * 20.0 + midsEnv * 2.0);

    return lines;
}


// =================================================================
//  CONVERGENCE SPIRAL VORTEX (center energy)
// =================================================================
//  Three luminous spiral arms flowing toward the snowflake center,
//  creating a vortex effect that pulls energy inward.

float convergenceVortex(vec2 p, float time, float bassEnv, float midsEnv) {
    float r = length(p);
    float angle = atan(p.y, p.x);

    float spiral = 0.0;
    for (int i = 0; i < 3; i++) {
        float phase = float(i) * TAU / 3.0;
        float spiralAngle = angle - r * 15.0 - time * 1.5 - phase;
        float arm = pow(0.5 + 0.5 * cos(spiralAngle), 8.0);
        float radialMask = smoothstep(0.0, 0.08, r) * smoothstep(0.35, 0.15, r);
        spiral += arm * radialMask;
    }

    return spiral * (0.3 + bassEnv * 0.5 + midsEnv * 0.3);
}


// =================================================================
//  PER-INSTANCE UV COMPUTATION
// =================================================================

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            timeSin(0.13) * 0.015 + timeSin(0.29) * 0.008,
            timeCos(0.19) * 0.012 + timeCos(0.11) * 0.006
        );
        uv -= drift;
        // Gentle rotation
        float rotAng = timeSin(0.12) * 0.04;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + timeSin(0.6) * 0.02;
        float springT = fract(time * 1.2);
        float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + LOGO_CENTER;
        return uv;
    }

    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.06 + float(idx) * 0.021;
    float f2 = 0.04 + float(idx) * 0.017;
    vec2 mdrift = vec2(
        timeSin(f1, h1 * TAU) * roam + timeSin(f1 * 2.1, h3 * TAU) * roam * 0.3,
        timeCos(f2, h2 * TAU) * roam * 0.9 + timeCos(f2 * 1.6, h4 * TAU) * roam * 0.25
    );
    uv -= mdrift;

    // Per-instance rotation -- snowflake can rotate freely (6-fold symmetric)
    float rotAng = timeSin(0.1 + float(idx) * 0.025, h4 * TAU) * 0.08;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + timeSin(0.5 + float(idx) * 0.11, h1 * TAU) * 0.02;
    float springT = fract(time * 1.2 + h2);
    float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + LOGO_CENTER;
    return uv;
}


// =================================================================
//  MAIN ZONE RENDER
// =================================================================

vec4 renderNixosZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                     bool isHighlighted, float bass, float mids, float treble,
                     bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    // -- Read customParams slots (must match metadata.json) ------
    // Slots 0-3: customParams[0].xyzw
    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.08;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.15;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    // Slot 3 unused

    // Slots 4-7: customParams[1].xyzw
    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 5.0;
    float gridStrength  = customParams[1].y >= 0.0 ? customParams[1].y : 0.25;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.75;
    // Slot 7 unused

    // Slots 8-11: customParams[2].xyzw
    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    // Slots 12-15: customParams[3].xyzw
    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.4;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.45;
    // Slot 15 unused

    // Slots 20-23: customParams[5].xyzw
    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.3;
    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.45;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.8;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    // Slots 24-28: customParams[6].xyzw + customParams[7].x
    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 3.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;
    // Slots 27-28 unused

    // Slot 30: customParams[7].z, Slot 31: customParams[7].w
    float logoSpin      = customParams[7].z >= 0.0 ? customParams[7].z : 0.15;
    float idleStrength  = customParams[7].w >= 0.0 ? customParams[7].w : 0.6;

    // -- Zone geometry --------------------------------------------
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    // -- Palette from customColors --------------------------------
    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, NIX_TWILIGHT);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, NIX_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, NIX_SKY);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, NIX_GLOW);

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * idleStrength;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    // -- Audio envelopes ------------------------------------------
    float bassEnv   = hasAudio ? smoothstep(0.01, 0.18, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.01, 0.25, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.02, 0.30, treble) * audioReact : 0.0;

    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;
    centeredUV.x *= aspect;

    vec4 result = vec4(0.0);

    // =============================================================
    //  INTERIOR FILL
    // =============================================================

    if (d < 0.0) {
        // -- Background: Voronoi crystal field + hex circuit overlay --
        // Animated Voronoi cells with color-phased interiors, bright
        // fracture lines at cell edges, and glowing vertex nodes.
        // No other drift shader uses Voronoi — unique to NixOS.

        // Base: deep twilight gradient
        float skyT = globalUV.y;
        vec3 col = mix(palPrimary * 0.3, palSecondary * 0.35, skyT) * brightness;

        // -- Voronoi crystal field --
        {
            float vScale = 3.0 + gridScale * 0.3;
            vec2 vUV = centeredUV * vScale + flowDir * time * flowSpeed * 0.2;
            vec2 vCell = floor(vUV);
            vec2 vFract = fract(vUV);

            // Find two closest Voronoi cell centers for edge detection
            float d1 = 1e9, d2 = 1e9;
            vec2 closestId = vec2(0.0);
            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    vec2 neighbor = vec2(float(ox), float(oy));
                    vec2 nId = vCell + neighbor;
                    // Animated cell centers: slow drift
                    vec2 point = hash22(nId) * 0.8 + 0.1;
                    point += 0.12 * sin(time * 0.3 + hash22(nId + 100.0) * TAU);
                    float dist = length(vFract - neighbor - point);
                    if (dist < d1) {
                        d2 = d1;
                        d1 = dist;
                        closestId = nId;
                    } else if (dist < d2) {
                        d2 = dist;
                    }
                }
            }

            // Cell edge = where d1 ≈ d2 (Voronoi ridge)
            float edge = d2 - d1;
            float ridgeLine = smoothstep(0.08, 0.01, edge) * 0.6;

            // Cell interior color: each cell has a unique hue-phase
            float cellHash = hash21(closestId);
            float cellPhase = cellHash + time * 0.04 + midsEnv * 0.4;
            vec3 cellCol = nixPalette(cellPhase, palPrimary, palSecondary, palAccent);

            // Cell brightness — subtle base, ALL cells react to bass
            float cellBright = 0.08 + 0.06 * sin(cellHash * TAU * 5.0 + time * 0.7);
            // Every cell pulses with bass; some cells flash brighter
            cellBright += bassEnv * 0.15;
            cellBright += bassEnv * 0.3 * step(0.6, cellHash);
            // Treble sparks random cells
            cellBright += trebleEnv * 0.4 * step(0.85, hash21(closestId + floor(time * 0.7)));

            col += cellCol * cellBright * brightness;

            // Fracture lines at Voronoi ridges — bass makes them flare
            vec3 ridgeCol = mix(palAccent, palGlow, 0.5 + 0.5 * sin(cellHash * 10.0 + time));
            col += ridgeCol * ridgeLine * (0.3 + bassEnv * 1.2);

            // Vertex glow: bright spots where 3+ cells meet
            float vertexGlow = exp(-d1 * d1 * 80.0) * 0.12;
            vertexGlow *= smoothstep(0.06, 0.02, edge);
            col += palGlow * vertexGlow * (0.4 + bassEnv * 0.6 + trebleEnv * 1.2);
        }

        // Hex circuit traces layered on top of Voronoi field
        col += hexCircuitTraces(centeredUV + time * speed * 0.1, time, bassEnv, midsEnv, trebleEnv,
                                palPrimary, palSecondary, palAccent) * gridStrength * 1.5;

        // -- Multi-instance logo rendering ------------------------
        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            // Wide bounding check
            if (iLogoUV.x < -0.4 || iLogoUV.x > 1.4 ||
                iLogoUV.y < -0.4 || iLogoUV.y > 1.4) continue;

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            // Logo-local coordinates (relative to LOGO_CENTER)
            vec2 logoP = iLogoUV - LOGO_CENTER;
            float logoR = length(logoP);

            // Circular vignette for the logo area
            float logoVignette = 1.0 - smoothstep(0.35, 0.55, logoR);

            // Snowflake can spin freely (it is 6-fold symmetric)
            float wobble = time * logoSpin * 0.5 + float(li) * 1.047;
            vec2 rotP = rot2D(logoP, wobble);

            float ringBreath = 1.0 + midsEnv * 0.3;

            // Compute SDF with arm tracking
            vec2 sdfResult = snowflakeSDF(rotP / ringBreath);
            float fDist = sdfResult.x * ringBreath;
            int closestArm = int(sdfResult.y);

            // Skip if too far from logo (optimization)
            if (fDist > 0.15) continue;

            vec3 logoCol = vec3(0.0);
            // Outer effects accumulate separately — they must NOT go
            // through Reinhard tonemap, otherwise they get crushed to
            // ~0.02-0.05 and become invisible against the Voronoi.
            vec3 outerCol = vec3(0.0);

            // -- FROST CRYSTALLIZATION HALO -----------------------
            // Ice dendrites branching outward from the snowflake edges.
            if (fDist > 0.0 && fDist < 0.12) {
                float frostAngle = atan(rotP.y, rotP.x);
                for (int di = 0; di < 6; di++) {
                    float dAngle = float(di) * TAU / 6.0;
                    float angleDiff = abs(mod(frostAngle - dAngle + PI, TAU) - PI);
                    float dendWidth = (0.15 - fDist * 0.8) * (1.0 + bassEnv * 0.7);
                    dendWidth = max(dendWidth, 0.02);
                    float dendrite = smoothstep(dendWidth, dendWidth * 0.2, angleDiff);
                    float growPhase = fract(time * 0.2 + float(di) * 0.167 + float(li) * 0.5);
                    float growFront = growPhase * 0.12;
                    float growMask = smoothstep(growFront + 0.01, growFront - 0.005, fDist);
                    float secAngle1 = abs(mod(frostAngle - dAngle - 0.5 + PI, TAU) - PI);
                    float secAngle2 = abs(mod(frostAngle - dAngle + 0.5 + PI, TAU) - PI);
                    float secDend = smoothstep(dendWidth * 0.5, dendWidth * 0.1, secAngle1)
                                  + smoothstep(dendWidth * 0.5, dendWidth * 0.1, secAngle2);
                    secDend *= smoothstep(0.03, 0.06, fDist);
                    float dBright = (dendrite + secDend * 0.4) * growMask;
                    vec3 dCol = mix(palGlow, palAccent, fDist / 0.12);
                    outerCol += dCol * dBright * 0.5 * instIntensity * depthFactor;
                }
            }

            // -- PER-INSTANCE BASS SHOCKWAVE RING -----------------
            float iShockPhase = fract(time * 0.6 + float(li) * 0.137);
            float iShockStr = bassEnv * (1.0 - iShockPhase) * logoPulse;
            if (iShockStr > 0.01) {
                float iShockRadius = iShockPhase * 0.5;
                float shockDist = abs(logoR - iShockRadius);
                float shockMask = smoothstep(0.06, 0.0, shockDist) * iShockStr;
                float shockAngle = atan(logoP.y, logoP.x);
                float hexMod = 0.8 + 0.2 * cos(shockAngle * 3.0);
                shockMask *= hexMod;
                vec3 shockCol = nixPalette(iShockRadius * 3.0 + time * 0.2 + float(li),
                                            palGlow, palAccent, palSecondary);
                outerCol += shockCol * shockMask * 0.45 * depthFactor;
            }

            // -- DUAL GRADIENT FILL + INTERIOR EFFECTS ------------
            if (fDist < 0.0) {
                // Arm-length gradient
                float armAngle = float(closestArm) * TAU / 6.0;
                vec2 armAxis = vec2(cos(armAngle), sin(armAngle));
                float armProj = dot(rotP, armAxis);
                float armT = clamp(armProj / 0.33, 0.0, 1.0);

                // Two-tone: even arms light, odd arms dark
                bool isLightArm = (closestArm % 2 == 0);
                vec3 armTip, armBase;
                if (isLightArm) {
                    armTip = palAccent;
                    armBase = palGlow;
                } else {
                    armTip = palPrimary;
                    armBase = palSecondary;
                }

                float gradT = clamp(armT + midsEnv * 0.2, 0.0, 1.0);
                vec3 innerCol = mix(armBase, armTip, gradT);

                // -- HEX MICRO-LATTICE (ice crystal structure) ----
                float lattice = hexMicroLattice(rotP, time, midsEnv);
                vec3 latticeCol = mix(palAccent, palGlow, 0.5);
                innerCol += latticeCol * lattice * (0.6 + bassEnv * 0.3);

                // -- CONVERGENCE SPIRAL VORTEX --------------------
                float vortex = convergenceVortex(rotP, time, bassEnv, midsEnv);
                vec3 vortexCol = mix(palSecondary, palGlow, 0.6);
                innerCol += vortexCol * vortex * 0.5;

                // -- CRYSTAL REFRACTION BEAMS ---------------------
                innerCol += crystalRefraction(rotP, time + float(li) * 3.0,
                                               bassEnv, midsEnv,
                                               palAccent, palGlow) * 0.7;

                // -- CONVERGENCE GLOW (center junction) -----------
                float centerGlow = exp(-logoR * logoR * 80.0) * 0.7;
                centerGlow *= 1.0 + bassEnv * logoPulse * 1.0;
                innerCol += palGlow * centerGlow;

                // -- PER-ARM AURORA RIPPLES -----------------------
                // Color wave traveling along each arm with phase offset
                float armPhase = float(closestArm) * 1.047;
                float auroraWave = sin(armT * 8.0 - time * 2.5 + armPhase);
                auroraWave = auroraWave * 0.5 + 0.5;
                float auroraStr = auroraWave * 0.35 * (0.5 + midsEnv * 0.8);
                vec3 auroraCol = nixPalette(armT + time * 0.1 + armPhase * 0.3,
                                             palPrimary, palAccent, palGlow);
                innerCol += auroraCol * auroraStr;

                // -- EDGE ENERGY LINES with flow ------------------
                float interiorEdge = clamp(-fDist / 0.04, 0.0, 1.0);
                float edgeLine = pow(1.0 - interiorEdge, 4.0) * 0.5;
                float edgePulse = 0.7 + 0.3 * sin(armT * 12.0 - time * 3.0 + armPhase);
                innerCol += palGlow * edgeLine * edgePulse * (1.0 + bassEnv * 0.8);

                // -- ARM TIP GLOW BEACONS -------------------------
                // Each arm tip independently pulses with bright energy
                float tipGlow = smoothstep(0.2, 0.33, armT);
                float tipPulse = 0.5 + 0.5 * sin(time * 3.0 + armPhase * 2.0 + bassEnv * 4.0);
                innerCol += palGlow * tipGlow * tipPulse * 0.5 * (1.0 + trebleEnv * 0.8);

                // Bass pulse on fill
                innerCol *= 1.0 + bassEnv * logoPulse * 0.5;

                // Treble flicker: random arm flash to white
                int flickerArm = int(mod(floor(time * 10.0 + float(li) * 3.7), 6.0));
                if (closestArm == flickerArm && trebleEnv > 0.15) {
                    innerCol = mix(innerCol, vec3(1.0) * brightness * 2.0, trebleEnv * 0.35);
                }

                // Anti-aliased edge
                float aa = smoothstep(0.002, -0.002, fDist);
                logoCol = mix(logoCol, innerCol * instIntensity, aa);
            }

            // -- DATA PACKET TRACES along arm outlines ------------
            if (fDist > -0.02 && fDist < 0.04) {
                for (int ai = 0; ai < 6; ai++) {
                    float traceGlow = armEdgeTrace(rotP, time + float(li) * 5.0,
                                                    ai, trebleEnv, bassEnv);
                    float traceMask = smoothstep(0.04, -0.005, fDist);
                    vec3 traceCol = nixPalette(float(ai) / 6.0 + time * 0.1,
                                                palAccent, palGlow, palSecondary);
                    logoCol += traceCol * traceGlow * traceMask * 0.5 * depthFactor;
                }
            }

            // -- CHECKSUM VERIFICATION RINGS -----------------------
            // Concentric segmented arcs around the snowflake, like a
            // circular hash visualization. Each ring rotates at a different
            // speed creating moire interference. Bass triggers a "verified"
            // sweep. Unique to NixOS — no other shader has this.
            if (fDist > 0.0 && fDist < 0.1) {
                float ringCount = 4.0;
                float segsPerRing = 12.0;
                float ringIdx = floor(fDist / 0.1 * ringCount);
                float ringFract = fract(fDist / 0.1 * ringCount);

                // Ring body (gap between rings)
                float ringBody = smoothstep(0.15, 0.25, ringFract)
                               * smoothstep(0.95, 0.85, ringFract);

                // Each ring rotates at different speed (inner faster)
                float gAngle = atan(rotP.y, rotP.x);
                float rotSpeed = (1.0 / (1.0 + ringIdx)) * 0.3;
                float rotAngle = gAngle + time * rotSpeed * (mod(ringIdx, 2.0) < 0.5 ? 1.0 : -1.0);

                // Segment index within ring
                float segAngle = rotAngle / TAU * segsPerRing;
                float segIdx = floor(segAngle);
                float segFract = fract(segAngle);

                // Gap between segments
                float segBody = smoothstep(0.05, 0.12, segFract)
                              * smoothstep(0.95, 0.88, segFract);

                // Each segment colored by hash — barcode pattern
                float segHash = hash21(vec2(ringIdx, segIdx + float(li) * 7.0));
                vec3 segCol = nixPalette(segHash + time * 0.03,
                                          palPrimary, palAccent, palGlow);

                // Bass verification sweep: ring lights up green-white in sequence
                float verifyFront = fract(time * 0.4 + float(li) * 0.2);
                float isVerified = smoothstep(0.05, 0.0,
                    abs(ringIdx / ringCount - verifyFront)) * bassEnv * logoPulse;
                segCol = mix(segCol, palGlow * 2.0, isVerified * 0.6);

                // Treble: random segment "bit flip" flash
                float flipSeg = floor(mod(floor(time * 1.5), segsPerRing));
                float flipRing = floor(mod(floor(time * 0.8 + float(li)), ringCount));
                if (segIdx == flipSeg && ringIdx == flipRing && trebleEnv > 0.1) {
                    segCol = mix(segCol, vec3(1.0), trebleEnv * 0.6);
                }

                float ringMask = ringBody * segBody * 0.35;
                outerCol += segCol * ringMask * instIntensity * depthFactor;
            }

            // -- Reinhard tonemap on INTERIOR only ------------------
            // Outer effects (outerCol) skip tonemap so they stay bright
            // enough to read against the Voronoi background.
            logoCol = logoCol / (1.0 + logoCol);

            // -- Composite: two-pass layering ----------------------
            float fillAlpha = smoothstep(0.005, -0.005, fDist);

            // Pass 1: inside snowflake — opaque fill replaces background
            col = mix(col, logoCol, fillAlpha);

            // Pass 2: outside snowflake — dim Voronoi, then add raw
            // outer effects (frost dendrites, checksum rings, shockwave)
            // at full brightness so they're clearly visible.
            float outerMask = (1.0 - fillAlpha) * logoVignette;
            float proximityDim = smoothstep(0.14, 0.0, fDist) * 0.6;
            col *= 1.0 - proximityDim * outerMask;
            col += outerCol * outerMask;

        } // end logo instance loop

        // -- Constellation network particles ----------------------
        col += constellationNetwork(globalUV, time, bassEnv, midsEnv, trebleEnv,
                                     particleStr, palAccent, palGlow) * 1.5;

        // -- DERIVATION TREE (dependency DAG) ---------------------
        // Branching tree lines growing upward — like nix-build propagating
        // through a dependency graph. Each "generation" branches at hash-
        // determined angles. Build wave propagates root→leaf on bass.
        // Unique: no other shader has hierarchical tree topology.
        {
            vec3 treeColor = vec3(0.0);
            float buildFront = fract(time * 0.15 + bassEnv * 0.3);

            // 4 root stems at different X positions
            for (int root = 0; root < 4; root++) {
                float rootX = -0.6 + float(root) * 0.4 + timeSin(0.05, float(root)) * 0.05;
                vec2 stemBase = vec2(rootX, -1.0) * noiseScale;
                stemBase.x *= aspect;

                // Trace 3 generations of branching
                float branchAngle = PI * 0.5; // start pointing up
                vec2 pos = stemBase;
                float segLen = 0.8 * noiseScale;

                for (int gen = 0; gen < 3; gen++) {
                    float fg = float(gen);
                    float rh = hash21(vec2(float(root) * 7.3, fg * 13.1));

                    // Branch direction with hash-based deviation
                    float deviation = (rh - 0.5) * 0.6;
                    float thisAngle = branchAngle + deviation;
                    vec2 dir = vec2(cos(thisAngle), sin(thisAngle));
                    vec2 endPos = pos + dir * segLen;

                    // Distance from pixel to this branch segment
                    float segDist = sdSegment(centeredUV, pos, endPos);
                    float lineWidth = 0.03 - fg * 0.008;

                    // Build propagation: depth-dependent activation
                    float depth = (fg + 0.5) / 3.0;
                    float built = smoothstep(depth - 0.15, depth, buildFront);
                    float building = smoothstep(0.0, 0.1,
                        abs(depth - buildFront)) < 0.5 ? 1.0 : 0.0;

                    float branchGlow = smoothstep(lineWidth, lineWidth * 0.2, segDist);
                    branchGlow *= built;

                    // Node at branch point (small bright dot)
                    float nodeDist = length(centeredUV - endPos);
                    float nodeGlow = smoothstep(0.04, 0.01, nodeDist) * built;

                    // Building flash at the wavefront
                    float flashStr = building * bassEnv * 0.8;

                    // Color varies by generation depth
                    vec3 bCol = nixPalette(rh + fg * 0.33 + time * 0.04,
                                            palPrimary, palSecondary, palAccent);
                    vec3 nodeCol = mix(palAccent, palGlow, rh);

                    treeColor += bCol * branchGlow * 0.25
                               + nodeCol * nodeGlow * 0.5
                               + palGlow * (branchGlow + nodeGlow) * flashStr;

                    // Split: second branch at different angle
                    float rh2 = hash21(vec2(float(root) * 11.7, fg * 7.3 + 50.0));
                    float thisAngle2 = branchAngle - deviation * 0.8 + (rh2 - 0.5) * 0.4;
                    vec2 dir2 = vec2(cos(thisAngle2), sin(thisAngle2));
                    vec2 endPos2 = pos + dir2 * segLen * 0.8;

                    float seg2Dist = sdSegment(centeredUV, pos, endPos2);
                    float branch2Glow = smoothstep(lineWidth, lineWidth * 0.2, seg2Dist) * built;
                    float node2Dist = length(centeredUV - endPos2);
                    float node2Glow = smoothstep(0.035, 0.01, node2Dist) * built;

                    vec3 b2Col = nixPalette(rh2 + fg * 0.33, palSecondary, palAccent, palGlow);
                    treeColor += b2Col * branch2Glow * 0.2
                               + nodeCol * node2Glow * 0.4;

                    // Advance to next generation
                    pos = endPos;
                    segLen *= 0.6;
                    branchAngle = thisAngle + (rh - 0.5) * 0.3;
                }
            }

            col += treeColor * particleStr * 0.8;
        }

        // -- Vitality ---------------------------------------------
        if (isHighlighted) {
            col *= 1.15;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.15);
            col *= 0.75 + idlePulse * 0.15;
        }

        // -- SEQUENTIAL HEX CELL LIGHTING BORDER -----------------
        float innerDist = -d;
        float vignette = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.85, 1.0, vignette);

        // Crisp inner edge line (not soft exponential glow)
        float edgeLine = smoothstep(3.0, 1.0, innerDist) * innerGlowStr * 0.6;
        col += palSecondary * edgeLine;

        // Hex cell tessellation in the border region (innerDist < 25px)
        if (innerDist < 25.0) {
            // Hex grid in pixel space along the border
            float hexScale = 0.08;  // size of hex cells in normalized coords
            vec2 borderUV = fragCoord / max(iResolution, vec2(1.0));
            vec2 hp = borderUV / hexScale;

            const vec2 hs = vec2(1.0, 1.7320508);
            vec2 ha = mod(hp, hs) - hs * 0.5;
            vec2 hb = mod(hp - hs * 0.5, hs) - hs * 0.5;
            vec2 hgv = dot(ha, ha) < dot(hb, hb) ? ha : hb;
            vec2 hid = hp - hgv;

            // Angular position of this cell around the zone center
            vec2 cellWorld = hid * hexScale * iResolution;
            float cellAngle = atan(cellWorld.y - center.y / max(iResolution.y, 1.0),
                                   cellWorld.x - center.x / max(iResolution.x, 1.0));
            float cellPhase = (cellAngle + PI) / TAU;  // 0..1

            // Activation wave sweeps around the zone perimeter
            float waveSpeed = 0.3 + bassEnv * 0.5;
            float wavePos = fract(time * waveSpeed);
            float waveDist = abs(fract(cellPhase - wavePos + 0.5) - 0.5);
            float waveActivation = smoothstep(0.15, 0.0, waveDist);

            // Cell fade after wave passes
            float cellBright = waveActivation * 0.5;

            // Bass triggers random additional cell flashes
            float bassFlash = bassEnv * 0.35 * step(0.7, hash21(hid + floor(time * 0.7)));
            cellBright += bassFlash;

            // Only show in border region, fading inward
            float borderMask = smoothstep(25.0, 0.0, innerDist) * smoothstep(0.0, 5.0, innerDist);

            // Hex cell shape mask
            float hexE = max(abs(hgv.x), abs(hgv.x * 0.5 + hgv.y * 0.866025));
            hexE = max(hexE, abs(-hgv.x * 0.5 + hgv.y * 0.866025));
            float cellMask = smoothstep(0.5, 0.35, hexE);

            vec3 cellColor = nixPalette(cellPhase + time * 0.05, palPrimary, palAccent, palGlow);
            col += cellColor * cellBright * borderMask * cellMask;
        }

        // Tint with fill color
        col = mix(col, fillColor.rgb * luminance(col), 0.1);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // -- Border: clean geometric with traveling pulse --------------
    // NO FBM animation. Clean, structured border matching NixOS identity.
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x);

        // Base border: clean gradient between dark and light arms
        float sixFold = 0.5 + 0.5 * cos(angle * 3.0);  // 6-fold symmetry pattern
        vec3 borderCol = mix(palPrimary, palSecondary, sixFold);

        // Traveling pulse along the border perimeter
        float pulsePos = fract(time * speed * 2.0);
        float pulseAngle = (angle + PI) / TAU;  // 0..1
        float pulseDist = abs(fract(pulseAngle - pulsePos + 0.5) - 0.5);
        float pulse = smoothstep(0.12, 0.0, pulseDist) * 0.8;
        borderCol += palAccent * pulse;

        // Second pulse traveling opposite direction
        float pulse2Pos = fract(-time * speed * 1.5 + 0.5);
        float pulse2Dist = abs(fract(pulseAngle - pulse2Pos + 0.5) - 0.5);
        float pulse2 = smoothstep(0.08, 0.0, pulse2Dist) * 0.4;
        borderCol += palGlow * pulse2;

        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= borderBrightness;

        if (isHighlighted) {
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.4 : 1.0;
            borderCol *= borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.2);
            borderCol *= 0.6;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // -- Outer glow: crisp hex-shaped, not soft FBM fog -----------
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 3.0;
    float glowRadius = mix(8.0, 16.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 10.0, borderGlow);
        // 6-fold modulated glow — hex-shaped falloff
        float oAngle = atan(p.y, p.x);
        float hexShape = 0.7 + 0.3 * cos(oAngle * 3.0 + time * 0.3);
        glow *= hexShape;
        vec3 glowCol = mix(palPrimary, palAccent, hexShape);
        glowCol *= mix(0.25, 1.0, vitality);
        result.rgb += glowCol * glow * 0.6;
        result.a = max(result.a, glow * 0.5);
    }

    return result;
}


// =================================================================
//  LABEL COMPOSITING (Clean luminous treatment)
// =================================================================

vec4 compositeNixosLabels(vec4 color, vec2 fragCoord,
                           float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, NIX_TWILIGHT);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, NIX_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, NIX_SKY);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, NIX_GLOW);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    float time = iTime;

    // -- Audio envelopes ------------------------------------------
    float bassEnv   = hasAudio ? smoothstep(0.01, 0.18, bass)   * labelAudioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.01, 0.25, mids)   * labelAudioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.02, 0.30, treble) * labelAudioReact : 0.0;

    // =============================================================
    //  HEX-FACETED CRYSTALLINE HALO
    // =============================================================
    //  Halo samples snap to a coarse hex grid, creating visible faceted
    //  ice-crystal edges. Mids increase crystallization. No other drift
    //  shader quantizes halo samples to a hex lattice.

    float haloSmooth = 0.0;
    float haloHex = 0.0;

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px;

            float wSmooth = exp(-r2 * 0.25);
            float s = texture(uZoneLabels, uv + off * labelGlowSpread).a;
            haloSmooth += s * wSmooth;

            // Hex-snapped sample: coarse cells for visible faceting
            vec2 hexOff = off * labelGlowSpread;
            float hexSnap = 18.0; // coarse cells — visible crystalline edges
            vec2 hp = hexOff * hexSnap;
            const vec2 hs = vec2(1.0, 1.7320508);
            vec2 ha = mod(hp, hs) - hs * 0.5;
            vec2 hb = mod(hp - hs * 0.5, hs) - hs * 0.5;
            vec2 snapped = (dot(ha, ha) < dot(hb, hb) ? (hp - ha) : (hp - hb)) / hexSnap;
            float sHex = texture(uZoneLabels, uv + snapped).a;
            haloHex += sHex * wSmooth;
        }
    }
    haloSmooth /= 14.0;
    haloHex /= 14.0;

    // Heavy crystallization — hex facets dominate, mids push further
    float facetMix = 0.8 + midsEnv * 0.15;
    float halo = mix(haloSmooth, haloHex, facetMix);

    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);

        // NixOS palette cycling
        float t = uv.x * 1.5 + time * 0.06;
        vec3 haloCol = nixPalette(t, palPrimary, palAccent, palGlow);

        // Bass brightens halo
        float pulse = 0.85 + 0.15 * sin(time * 2.5);
        pulse *= 1.0 + bassEnv * 0.6;

        color.rgb += haloCol * haloEdge * 0.7 * pulse;

        // -- FROST DENDRITE RAYS from text edges --------------------
        // 6 branching crystallization rays at hex angles with secondary
        // branches — like real ice crystal growth on a cold surface.
        // No other shader has angular dendritic growth on labels.
        {
            float frostStr = 0.0;
            vec3 frostCol = vec3(0.0);
            float frostAngle = atan(uv.y - 0.5, uv.x - 0.5);
            for (int fi = 0; fi < 6; fi++) {
                float hexAng = float(fi) * TAU / 6.0;

                // Primary ray
                float angDiff = abs(mod(frostAngle - hexAng + PI, TAU) - PI);
                float primaryRay = smoothstep(0.10, 0.0, angDiff);

                // Secondary branches at ±30deg
                float secAng1 = abs(mod(frostAngle - hexAng - 0.52 + PI, TAU) - PI);
                float secAng2 = abs(mod(frostAngle - hexAng + 0.52 + PI, TAU) - PI);
                float secRay = smoothstep(0.06, 0.0, secAng1)
                             + smoothstep(0.06, 0.0, secAng2);
                secRay *= 0.5;

                // Growth: pulsates outward on bass
                float growT = fract(time * 0.25 + float(fi) * 0.167);
                float growEnv = smoothstep(0.0, 0.2, growT) * smoothstep(0.7, 0.4, growT);
                growEnv *= 0.6 + bassEnv * 0.8;

                float ray = (primaryRay + secRay) * growEnv;

                // Color shifts per ray
                vec3 rCol = nixPalette(float(fi) / 6.0 + time * 0.04,
                                        palAccent, palGlow, palSecondary);
                frostCol += rCol * ray;
                frostStr += ray;
            }
            frostStr *= haloEdge;
            color.rgb += frostCol * haloEdge * 0.5;
        }

        // -- HASH BIT GRID around text edges ------------------------
        // Tiny checkerboard pattern where each cell is a "hash bit"
        // that flips on treble — visualizes hash computation around
        // the text. Unique to NixOS labels.
        if (haloEdge > 0.02 && haloEdge < 0.3) {
            vec2 bitCoord = floor(uv * vec2(40.0, 12.0));
            float bitHash = hash21(bitCoord);
            // Treble flips random bits
            float bitFlip = step(0.85, hash21(bitCoord + floor(time * 0.8))) * trebleEnv;
            float bit = step(0.5, fract(bitHash + bitFlip * 0.5));
            // Checker: alternating ON/OFF cells
            float checker = mod(bitCoord.x + bitCoord.y, 2.0);
            float bitVal = mix(bit, 1.0 - bit, checker);
            vec3 bitCol = mix(palPrimary, palAccent, bitVal);
            float bitMask = haloEdge * smoothstep(0.02, 0.05, haloEdge)
                          * smoothstep(0.3, 0.15, haloEdge);
            color.rgb += bitCol * bitMask * (0.3 + midsEnv * 0.3 + trebleEnv * 0.5);
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // =============================================================
    //  TEXT BODY: hash-grid pattern + checksum scanline + bass pulse
    // =============================================================
    if (labels.a > 0.01) {
        // Pixel-space hash grid — quantized noise creates discrete colored
        // blocks inside text, like data cells in a verification readout.
        vec2 hashCoord = floor(fragCoord / 6.0);  // 6px cells
        float cellHash = hash21(hashCoord + floor(time * 0.5) * 0.1);
        float cellBand = floor(cellHash * 3.0) / 2.0;  // 0.0, 0.5, 1.0
        vec3 textCol = mix(palPrimary, palAccent, cellBand);
        // Alternating cells get glow tint for contrast
        float checker = mod(hashCoord.x + hashCoord.y, 2.0);
        textCol = mix(textCol, palGlow * 0.8, checker * 0.35);

        // CHECKSUM SCANLINE: crisp verification line sweeping L→R
        float scanSpeed = 0.2 + bassEnv * 0.4;
        float scanPos = fract(time * scanSpeed);
        // Use fragCoord for scan position so it's visible per-character
        float scanX = fragCoord.x / max(iResolution.x, 1.0);
        float scanDist = abs(scanX - scanPos);
        scanDist = min(scanDist, 1.0 - scanDist);
        float scanLine = smoothstep(0.015, 0.0, scanDist);
        textCol = mix(textCol, palGlow * 2.5, scanLine * 0.6);
        // Trailing verified glow
        float verified = smoothstep(scanPos + 0.03, scanPos - 0.15, scanX);
        textCol *= 1.0 + verified * 0.25;

        // Stroke edge rim
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);
        textCol += palGlow * rim * 0.5;

        textCol *= labelBrightness;

        // Per-character bass pulse with phase offset (pixel-space)
        float charPhase = floor(fragCoord.x / 20.0) * 0.3;
        float charPulse = 1.0 + bassEnv * 0.5 * (0.5 + 0.5 * sin(time * 4.0 + charPhase));
        textCol *= charPulse;

        // Treble: random hash cells flash bright
        float cellFlash = hash21(hashCoord + floor(time * 0.7) * 0.1);
        if (trebleEnv > 0.1 && cellFlash > 0.6) {
            textCol = mix(textCol, palGlow * 3.0, trebleEnv * 0.35);
        }

        // Gentle tonemap
        textCol = textCol / (0.6 + textCol);

        color.rgb = mix(color.rgb, textCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}


// =================================================================
//  ENTRY POINT
// =================================================================

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderNixosZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    // Slot 29 (showLabels): customParams[7].y — default true when unset (<0)
    float showLabelsVal = customParams[7].y;
    if (showLabelsVal < 0.0 || showLabelsVal > 0.5) {
        color = compositeNixosLabels(color, fragCoord, bass, mids, treble, hasAudio);
    }

    fragColor = clampFragColor(color);
}
