// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * ARCH DRIFT - Fragment Shader (Mountain Ridge Topography — Multi-Instance)
 *
 * Terminal rain columns, isometric chevron grid, traveling data packets,
 * CRT scan lines — the spirit of Arch: minimalism, precision, the CLI.
 * Logo interior: roiling cloud flow, lightning flashes, rain streaks,
 * animated energy contour waves, summit beacon, traveling edge pulses.
 *
 * Logo geometry: 95-vertex Arch polygon from official Crystal SVG.
 *
 * Audio reactivity:
 *   Bass  = terminal rain brightens, grid pulses, data packets glow,
 *           summit beacon, echo rings, interior lightning
 *   Mids  = palette warmth drift, interior cloud churn
 *   Treble = grid cell flashes, edge sparks, contour sharpen
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ── Arch brand constants ─────────────────────────────────────────
const vec3 ARCH_BLUE = vec3(0.090, 0.576, 0.820);
const vec3 ARCH_DEEP = vec3(0.035, 0.220, 0.380);
const vec3 ARCH_ICE  = vec3(0.310, 0.765, 0.969);
const vec3 ARCH_SNOW = vec3(0.702, 0.898, 0.988);


// ── Simplex noise (unique to Arch shader) ────────────────────────

vec3 simplexMod289(vec3 x) { return x - floor(x / 289.0) * 289.0; }
vec2 simplexMod289v2(vec2 x) { return x - floor(x / 289.0) * 289.0; }
vec3 simplexPermute(vec3 x) { return simplexMod289((x * 34.0 + 1.0) * x); }

float simplex2D(vec2 v) {
    const vec4 C = vec4(0.211324865405, 0.366025403784,
                        -0.577350269189, 0.024390243902);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = simplexMod289v2(i);
    vec3 p = simplexPermute(simplexPermute(i.y + vec3(0.0, i1.y, 1.0))
                            + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy),
                             dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x_ = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x_) - 0.5;
    vec3 ox = floor(x_ + 0.5);
    vec3 a0 = x_ - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// ── Terminal rain column ──────────────────────────────────────────
// Simulates a single column of scrolling terminal output.
// Returns brightness of a "character" at this position.
float terminalColumn(vec2 uv, float colSeed, float time, float scrollSpeed) {
    // Each column scrolls at a unique speed
    float colHash = hash21(vec2(colSeed, 7.31));
    float scroll = time * scrollSpeed * (0.5 + colHash * 1.0);

    // Character grid: each cell is a "character"
    float charY = uv.y * 30.0 + scroll;
    float cellY = floor(charY);
    float cellFract = fract(charY);

    // Character presence — some cells are empty (sparse terminal output)
    float charPresent = step(0.55, hash21(vec2(colSeed, cellY)));

    // Character brightness fades as it scrolls (old output fades)
    float age = fract(scroll * 0.1 + hash21(vec2(colSeed + 50.0, cellY)));
    float fade = pow(1.0 - age, 3.0);

    // Character shape: small bright rectangle within the cell
    float charShape = smoothstep(0.15, 0.2, cellFract) * smoothstep(0.85, 0.8, cellFract);

    return charPresent * charShape * fade;
}

// ── Isometric grid (angular, echoing the mountain geometry) ──────
float isoGrid(vec2 uv, float scale) {
    vec2 p = uv * scale;
    // Two sets of angled lines that form a chevron/mountain pattern
    float line1 = abs(fract(p.x + p.y) - 0.5) * 2.0;
    float line2 = abs(fract(p.x - p.y) - 0.5) * 2.0;
    float horiz = abs(fract(p.y * 0.5) - 0.5) * 2.0;
    float grid = min(line1, min(line2, horiz));
    return smoothstep(0.04, 0.0, grid);
}

// ── Data packet traveling along straight paths ───────────────────
float dataPacket(vec2 uv, float pathY, float time, float seed) {
    float pHash = hash21(vec2(seed, 3.17));
    float speed = 0.3 + pHash * 0.5;
    float packetX = fract(time * speed + pHash);

    // Packet position
    vec2 packetPos = vec2(packetX, pathY);
    float dist = length(uv - packetPos);

    // Bright head + fading trail
    float head = smoothstep(0.008, 0.0, dist);
    float trail = exp(-max(uv.x - packetX, 0.0) * 30.0) * smoothstep(0.01, 0.0, abs(uv.y - pathY));
    trail *= step(0.0, packetX - uv.x); // trail only behind

    return head * 3.0 + trail * 0.5;
}


// ── Catmull-Rom palette interpolation ────────────────────────────

vec3 catmullRom(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * ((2.0 * p1) +
                   (-p0 + p2) * t +
                   (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                   (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

vec3 archPaletteCR(float t, vec3 primary, vec3 secondary, vec3 accent, vec3 glow) {
    t = fract(t);
    float seg = t * 4.0;
    int idx = int(seg);
    float f = fract(seg);
    // Wrap-around: primary -> secondary -> accent -> glow -> primary
    vec3 colors[5] = vec3[5](primary, secondary, accent, glow, primary);
    int i0 = max(idx - 1, 0);
    int i1 = idx;
    int i2 = min(idx + 1, 4);
    int i3 = min(idx + 2, 4);
    return clamp(catmullRom(colors[i0], colors[i1], colors[i2], colors[i3], f), 0.0, 1.0);
}

vec3 paletteSweep(float t, vec3 primary, vec3 secondary, vec3 accent, vec3 glow,
                  float audioShift) {
    // Subtle hue shift from audio stays within Arch color family
    float shifted = t + audioShift * 0.08;
    return archPaletteCR(shifted, primary, secondary, accent, glow);
}


// ── SDF primitives ──────────────────────────────────────────────

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}


// =================================================================
//  ARCH LINUX LOGO -- 95-vertex polygon from official Crystal SVG
// =================================================================

const vec2 LOGO_CENTER = vec2(0.50, 0.55);

const int ARCH_N = 95;
const vec2 ARCH[95] = vec2[95](
    vec2(0.500000, 0.000000), //  0 Summit
    vec2(0.485756, 0.035081), vec2(0.472477, 0.067966), vec2(0.459835, 0.099249),
    vec2(0.447501, 0.129523), vec2(0.435144, 0.159379), vec2(0.422435, 0.189412),
    vec2(0.409045, 0.220215), vec2(0.394646, 0.252379),
    vec2(0.378906, 0.286499), //  9 Left notch start
    vec2(0.398102, 0.306442), vec2(0.419749, 0.327615), vec2(0.444627, 0.350011),
    vec2(0.473518, 0.373621), vec2(0.507202, 0.398438), // 14 Left notch peak
    vec2(0.470543, 0.382348), vec2(0.438401, 0.366199), vec2(0.410319, 0.349940),
    vec2(0.385838, 0.333522), vec2(0.364502, 0.316895), // 19 Left notch end
    vec2(0.344328, 0.358398), vec2(0.321597, 0.403975), vec2(0.295911, 0.454330),
    vec2(0.266873, 0.510172), vec2(0.234085, 0.572206), vec2(0.197150, 0.641140),
    vec2(0.155672, 0.717680), vec2(0.109252, 0.802532), vec2(0.057494, 0.896403),
    vec2(0.000000, 1.000000), // 29 Bottom-left
    vec2(0.046493, 0.973609), vec2(0.091013, 0.949320), vec2(0.133723, 0.927132),
    vec2(0.174786, 0.907045), vec2(0.214367, 0.889056), vec2(0.252628, 0.873166),
    vec2(0.289734, 0.859373), vec2(0.325847, 0.847676), vec2(0.361132, 0.838074),
    vec2(0.395752, 0.830566), // 39 Left base near oval
    vec2(0.392515, 0.814405), vec2(0.390182, 0.797646), vec2(0.388803, 0.780380),
    vec2(0.388428, 0.762695), // 43 Inner oval left
    vec2(0.393813, 0.709153), vec2(0.406335, 0.666191), vec2(0.424989, 0.630467),
    vec2(0.448647, 0.603647), vec2(0.476184, 0.587394),
    vec2(0.506470, 0.583374), // 49 Inner oval top
    vec2(0.532328, 0.590432), vec2(0.555836, 0.606288), vec2(0.576339, 0.629780),
    vec2(0.593184, 0.659745), vec2(0.605717, 0.695020), vec2(0.613285, 0.734445),
    vec2(0.615234, 0.776855), // 56 Inner oval right
    vec2(0.614554, 0.791051), vec2(0.613237, 0.804912), vec2(0.611300, 0.818386),
    vec2(0.608765, 0.831421), // 60 Right base near oval
    vec2(0.643016, 0.839090), vec2(0.677931, 0.848799), vec2(0.713666, 0.860550),
    vec2(0.750379, 0.874343), vec2(0.788227, 0.890178), vec2(0.827367, 0.908056),
    vec2(0.867958, 0.927976), vec2(0.910155, 0.949940), vec2(0.954117, 0.973948),
    vec2(1.000000, 1.000000), // 70 Bottom-right
    vec2(0.981874, 0.966595), vec2(0.964430, 0.934386), vec2(0.947577, 0.903217),
    vec2(0.931225, 0.872934), vec2(0.915283, 0.843384), // 75 Right notch start
    vec2(0.894241, 0.826567), vec2(0.871707, 0.808314), vec2(0.846482, 0.788794),
    vec2(0.817368, 0.768182), vec2(0.783165, 0.746648),
    vec2(0.742676, 0.724365), // 81 Right notch peak
    vec2(0.784976, 0.736617), vec2(0.821554, 0.749757), vec2(0.853117, 0.763751),
    vec2(0.880371, 0.778564), // 85 Right notch end
    vec2(0.807272, 0.641696), vec2(0.747015, 0.527224), vec2(0.697705, 0.431667),
    vec2(0.657450, 0.351544), vec2(0.624354, 0.283375),
    vec2(0.596525, 0.223678), vec2(0.572069, 0.168973), vec2(0.549092, 0.115779),
    vec2(0.525700, 0.060615)  // 94 Right slope (near summit)
);

const vec2 ARCH_AABB_LO = vec2(0.000, 0.000);
const vec2 ARCH_AABB_HI = vec2(1.000, 1.000);


// ── Signed distance to the 95-vertex Arch polygon ───────────────

float sdArchPolygon(vec2 p) {
    vec2 dLo = ARCH_AABB_LO - p;
    vec2 dHi = p - ARCH_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) return sqrt(boxDist2);

    float d = dot(p - ARCH[0], p - ARCH[0]);
    float s = 1.0;
    for (int i = 0, j = ARCH_N - 1; i < ARCH_N; j = i, i++) {
        vec2 e = ARCH[j] - ARCH[i];
        vec2 w = p - ARCH[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= ARCH[i].y, p.y < ARCH[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    return s * sqrt(d);
}


// ── Logo hit result ─────────────────────────────────────────────

struct LogoHit {
    float dist;
    float altitude;   // 0 at base, 1 at summit
    float edgeDist;   // distance to nearest polygon edge (always positive)
};

// Combined SDF + edge-distance in a single 95-edge pass.
// Returns vec2(signedDist, edgeDist) — avoids the redundant second loop.
vec2 sdArchPolygonWithEdge(vec2 p) {
    vec2 dLo = ARCH_AABB_LO - p;
    vec2 dHi = p - ARCH_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) {
        float bd = sqrt(boxDist2);
        return vec2(bd, bd);
    }

    float d = dot(p - ARCH[0], p - ARCH[0]);
    float s = 1.0;
    for (int i = 0, j = ARCH_N - 1; i < ARCH_N; j = i, i++) {
        vec2 e = ARCH[j] - ARCH[i];
        vec2 w = p - ARCH[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= ARCH[i].y, p.y < ARCH[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    float edgeDist = sqrt(d);
    return vec2(s * edgeDist, edgeDist);
}

LogoHit evalArchLogo(vec2 p) {
    LogoHit hit;
    vec2 result = sdArchPolygonWithEdge(p);
    hit.dist = result.x;
    hit.edgeDist = result.y;
    hit.altitude = clamp(1.0 - p.y, 0.0, 1.0);
    return hit;
}


// ── Per-instance UV computation ─────────────────────────────────

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    float wobbleAmp = customParams[7].z >= 0.0 ? customParams[7].z : 0.12;
    uv.x = (uv.x - 0.5) * aspect + 0.5;
    if (totalCount <= 1) {
        vec2 drift = vec2(sin(time * 0.11) * 0.015 + sin(time * 0.27) * 0.005,
                          cos(time * 0.14) * 0.008 + cos(time * 0.09) * 0.004);
        uv -= drift;
        float rotAng = sin(time * 0.08) * wobbleAmp;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        instScale = logoScale * (1.0 + sin(time * 0.5) * 0.01);
        uv = (uv - 0.5) / instScale + LOGO_CENTER;
        return uv;
    }
    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));
    float roam = 0.30;
    float f1 = 0.06 + float(idx) * 0.017, f2 = 0.04 + float(idx) * 0.013;
    vec2 drift = vec2(
        sin(time * f1 + h1 * TAU) * roam + sin(time * f1 * 2.1 + h3 * TAU) * roam * 0.25,
        cos(time * f2 + h2 * TAU) * roam * 0.85 + cos(time * f2 * 1.5 + h4 * TAU) * roam * 0.2);
    uv -= drift;
    float rotAng = sin(time * (0.07 + float(idx) * 0.019) + h4 * TAU) * wobbleAmp * 0.33;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    instScale *= 1.0 + sin(time * (0.4 + float(idx) * 0.09) + h1 * TAU) * 0.012;
    uv = (uv - 0.5) / instScale + LOGO_CENTER;
    return uv;
}


// ── Simplex FBM (used only for interior cloud flow) ─────────────

float simplexFBM(vec2 uv, int octaves) {
    float value = 0.0;
    float amplitude = 0.6;
    float freq = 1.0;
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * (simplex2D(uv * freq) * 0.5 + 0.5);
        freq *= 2.1;
        amplitude *= 0.45;
    }
    return value;
}


// =================================================================
//  MAIN ZONE RENDER
// =================================================================

vec4 renderArchZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                    bool isHighlighted, float bass, float mids, float treble, float overall,
                    bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.10;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.20;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    int octaves         = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);

    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 5.0;
    float gridStrength  = customParams[1].y >= 0.0 ? customParams[1].y : 0.20;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.7;
    float contrast      = customParams[1].w >= 0.0 ? customParams[1].w : 0.9;

    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.4;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.35;
    float sparkleStr    = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;

    float fbmRot        = customParams[4].w >= 0.0 ? customParams[4].w : 0.6;
    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.25;

    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.5;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.75;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 3.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;

    float flowCenterX   = customParams[6].w >= -1.5 ? customParams[6].w : 0.5;
    float flowCenterY   = customParams[7].x >= -1.5 ? customParams[7].x : 0.5;
    float idleStrength  = customParams[7].w >= 0.0 ? customParams[7].w : 0.5;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, ARCH_BLUE);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, ARCH_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, ARCH_ICE);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, ARCH_SNOW);

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * idleStrength;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    float bassEnv   = hasAudio ? smoothstep(0.02, 0.25, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * audioReact : 0.0;

    vec4 result = vec4(0.0);

    if (d < 0.0) {

        // =============================================================
        //  BACKGROUND: Terminal Rain + Isometric Grid + Data Packets
        //  Spirit of Arch: minimalism, precision, the terminal, DIY
        // =============================================================

        // Deep gradient base: near-black -> deep secondary
        vec3 col = mix(palSecondary * 0.06, palSecondary * 0.18,
                        smoothstep(0.0, 1.0, globalUV.y)) * brightness;

        // ── Isometric grid (chevron/mountain angular pattern) ────
        // Clean geometric lines echoing the Arch mountain shape
        // gridScale = density, gridStrength = brightness, fbmRot = line angle
        {
            float gridDensity = gridScale;
            // fbmRot controls grid rotation angle
            float gc = cos(fbmRot), gs = sin(fbmRot);
            mat2 gridRot = mat2(gc, -gs, gs, gc);
            vec2 gridUV = gridRot * (globalUV * vec2(aspect, 1.0)) + vec2(time * speed * 0.05, 0.0);
            float grid = isoGrid(gridUV, gridDensity);

            // Grid pulses with audio; contrast sharpens grid lines
            float gridPulse = 1.0 + bassEnv * 0.5;

            // Cell identity for selective highlighting
            vec2 cellId = floor(gridUV * gridDensity);
            float cellHash = hash21(cellId);

            // Some cells light up on treble hits
            float cellFlash = step(0.8, cellHash)
                            * step(0.3, hash21(cellId + floor(time * 4.0))) * trebleEnv;

            // Grid color: subtle primary with occasional accent highlights
            vec3 gridCol = palPrimary * (0.2 + contrast * 0.2);
            gridCol = mix(gridCol, palAccent * 0.6, cellFlash);

            col += gridCol * grid * gridStrength * gridPulse * brightness;
        }

        // ── Terminal rain (scrolling character columns) ───────────
        // noiseScale = column density, flowSpeed = scroll speed
        // flowCenterX/Y = focal point (columns near here are brighter)
        {
            float numCols = 10.0 + noiseScale * 5.0; // noiseScale 1-10 → 15-60 columns
            float colWidth = 1.0 / (numCols * aspect);
            float colIdx = floor(globalUV.x / colWidth);
            float colFract = fract(globalUV.x / colWidth);

            // Only show character if we're in the center of the column
            float colMask = smoothstep(0.1, 0.2, colFract) * smoothstep(0.9, 0.8, colFract);

            float scrollSpeed = flowSpeed * 5.0;
            float charBright = terminalColumn(globalUV, colIdx, time, scrollSpeed);

            // Column color: varies per column, uses palette
            float colT = hash21(vec2(colIdx, 0.0)) + time * 0.02;
            vec3 charCol = paletteSweep(colT, palPrimary, palSecondary, palAccent, palGlow, midsEnv);

            // Head character (most recent) is brightest
            float headY = fract(time * scrollSpeed * (0.5 + hash21(vec2(colIdx, 7.31))) * 0.1);
            float headDist = abs(globalUV.y - headY);
            float headGlow = exp(-headDist * 20.0) * 0.5;

            // Focal point: columns near flowCenter are brighter
            vec2 focalPt = vec2(flowCenterX, flowCenterY);
            float focalDist = length(globalUV - focalPt);
            float focalBright = 1.0 + exp(-focalDist * 3.0) * 0.5;

            float termStr = (charBright * colMask * 0.4 + headGlow) * brightness * focalBright;
            // Bass makes rain brighter
            termStr *= 1.0 + bassEnv * 0.6;

            col += charCol * termStr;
        }

        // ── Horizontal scan lines (subtle CRT feel) ─────────────
        {
            float scan = sin(globalUV.y * iResolution.y * 0.5 * PI) * 0.5 + 0.5;
            scan = pow(scan, 8.0); // Thin bright lines
            col += palPrimary * 0.03 * scan * brightness;
        }

        // ── Data packets (bright dots traveling along paths) ─────
        // flowDirection controls packet travel angle
        {
            // Hoist trig out of the packet loop (flowAngle is loop-invariant)
            float fc = cos(flowAngle), fs = sin(flowAngle);
            for (int pi = 0; pi < 6; pi++) {
                float pathY = hash21(vec2(float(pi), 5.37)) * 0.8 + 0.1;
                // Rotate packet UV by flowDirection so packets travel in that angle
                vec2 pktUV = globalUV - vec2(0.5);
                pktUV = vec2(pktUV.x * fc + pktUV.y * fs, -pktUV.x * fs + pktUV.y * fc);
                pktUV += vec2(0.5);
                float pkt = dataPacket(pktUV, pathY, time, float(pi));
                vec3 pktCol = mix(palAccent, palGlow, hash21(vec2(float(pi), 11.3)));
                col += pktCol * pkt * particleStr * brightness;

                // Bass spawns brighter packets
                col += palGlow * pkt * bassEnv * 0.3;
            }
        }

        // ── Subtle ambient noise (very faint, adds texture) ──────
        {
            float noise = simplex2D(globalUV * 40.0 + time * 0.1) * 0.5 + 0.5;
            col += palSecondary * noise * 0.02 * brightness;
        }

        // Bass breathing on everything
        col *= (1.0 + bassEnv * 0.25);

        // =============================================================
        //  MULTI-INSTANCE LOGO RENDERING
        // =============================================================

        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            if (iLogoUV.x < -0.3 || iLogoUV.x > 1.3 ||
                iLogoUV.y < -0.2 || iLogoUV.y > 1.3) continue;

            // Cheap AABB early-out: skip the 95-edge polygon eval when clearly outside
            // the logo AABB expanded by the max effect radius (0.25).
            // AABB is [0,0]-[1,1], so expanded is [-0.25,-0.25]-[1.25,1.25].
            {
                vec2 dLo = vec2(-0.25) - iLogoUV;
                vec2 dHi = iLogoUV - vec2(1.25);
                vec2 outerDist = max(max(dLo, dHi), vec2(0.0));
                if (dot(outerDist, outerDist) > 0.0625) continue; // > 0.25^2
            }

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            LogoHit iLogo = evalArchLogo(iLogoUV);
            if (iLogo.dist > 0.25) continue;

            vec3 logoCol = vec3(0.0);
            vec3 outerCol = vec3(0.0);

            vec2 logoP = iLogoUV - LOGO_CENTER;
            float logoR = length(logoP);
            float logoVignette = 1.0 - smoothstep(0.45, 0.75, logoR);

            float fDist = iLogo.dist;
            float alt = iLogo.altitude;

            // Per-instance variation seeds
            float iSeed = float(li) * 1.618;
            float iHash = hash21(vec2(float(li) * 7.31, 3.17));
            float iPhase = iHash * TAU;

            // Light casting onto background
            float lightCast = exp(-max(fDist, 0.0) * 15.0) * 0.25;
            vec3 logoLight = paletteSweep(time * 0.08 + iLogoUV.y + float(li) * 0.3,
                                           palPrimary, palSecondary, palAccent, palGlow, midsEnv);
            col += logoLight * lightCast * instIntensity * (1.0 + bassEnv * 0.2) * depthFactor;

            // =====================================================
            //  OUTER EFFECTS: ECHO RINGS + PARTICLE DRIFT + EMERGE GLOW
            //  Merged under one broad distance guard to reduce branches.
            // =====================================================
            if (fDist > -0.02 && fDist < 0.20) {

                // -- Echo rings (only when fDist > 0) --
                if (fDist > 0.0) {
                    for (int ri = 0; ri < 3; ri++) {
                        float ringPhase = fract(time * 0.35 + float(ri) * 0.33 + float(li) * 0.17);
                        float ringRadius = ringPhase * 0.20;
                        float ringAge = ringPhase;
                        // Echo rings follow SDF contour shape (not circular)
                        float ringDist = abs(fDist - ringRadius);
                        float ringMask = smoothstep(0.008, 0.0, ringDist) * (1.0 - ringAge * ringAge);
                        ringMask *= bassEnv * 1.5 + 0.2;

                        vec3 ringCol = paletteSweep(ringPhase + float(ri) * 0.25,
                                                     palPrimary, palSecondary, palAccent, palGlow,
                                                     midsEnv);
                        outerCol += ringCol * ringMask * 0.5 * depthFactor;
                    }
                }

                // -- Particle drift (fDist > -0.02 && fDist < 0.15) --
                if (fDist < 0.15) {
                    float particleLayer = 0.0;
                    float driftSpeed = 0.15 + trebleEnv * 0.2;
                    for (int pi = 0; pi < 3; pi++) {
                        float pScale = 12.0 + float(pi) * 6.0;
                        vec2 pUV = iLogoUV * pScale + vec2(sin(time * 0.1 * float(pi + 1)),
                                                            -time * driftSpeed * (1.0 + float(pi) * 0.3));
                        pUV += float(li) * vec2(5.7, 3.1);
                        vec2 pCell = floor(pUV);
                        vec2 pFract = fract(pUV);
                        vec2 pOffset = hash22(pCell) * 0.6 + 0.2;
                        float pDist = length(pFract - pOffset);
                        float pRadius = 0.05 - float(pi) * 0.01;
                        float particle = smoothstep(pRadius, pRadius * 0.15, pDist);
                        float twinkle = 0.5 + 0.5 * sin(hash21(pCell) * TAU + time * 3.0);
                        particleLayer += particle * twinkle;
                    }
                    float proximityFade = smoothstep(0.08, 0.0, fDist) * smoothstep(-0.02, 0.01, fDist);
                    outerCol += palGlow * particleLayer * particleStr * 0.35 * proximityFade * depthFactor;
                }

                // -- Emerge glow (only when fDist > 0) --
                if (fDist > 0.0) {
                    float emergeRadius = 0.08 + bassEnv * 0.04;
                    float emergeFalloff = exp(-fDist / emergeRadius) * 0.3;
                    float emergeY = (iLogoUV.y - 0.5) / 0.5;
                    vec3 emergeCol = mix(palPrimary, palGlow, clamp(-emergeY * 0.5 + 0.5, 0.0, 1.0));
                    outerCol += emergeCol * emergeFalloff * instIntensity * depthFactor;
                }
            }

            // =====================================================
            //  LOGO INTERIOR: LIVING STORM WITHIN THE MOUNTAIN
            // =====================================================
            if (fDist < 0.0) {
                float depth = clamp(-fDist / 0.15, 0.0, 1.0);

                // ------- Layer 1: Roiling interior cloud flow -------
                // Domain-warped turbulent noise that continuously morphs
                vec2 cloudUV = iLogoUV * 5.0 + iSeed * 10.0;
                vec2 warpBase = cloudUV * 0.8 + time * 0.12;
                float warp1 = simplex2D(warpBase) * 0.5;
                float warp2 = simplex2D(warpBase + vec2(50.0)) * 0.5;
                cloudUV += vec2(warp1, warp2) * 0.6;
                cloudUV += vec2(time * speed * 0.8, time * speed * 0.3); // drift

                float cloud1 = simplexFBM(cloudUV, max(octaves - 1, 3));
                float cloud2 = simplexFBM(cloudUV * 1.7 + vec2(30.0, time * 0.05), max(octaves - 2, 3));
                float cloudDensity = cloud1 * 0.6 + cloud2 * 0.4;

                // Altitude-tinted cloud base — rich, full intensity
                vec3 cloudDark = mix(palSecondary * 0.5, palPrimary * 0.6, smoothstep(0.0, 0.6, alt));
                vec3 cloudBright = mix(palPrimary, palGlow * 0.85, smoothstep(0.4, 1.0, alt));
                vec3 interiorCol = mix(cloudDark, cloudBright, cloudDensity) * brightness * 1.2;

                // Mids make clouds churn (visible color cycling)
                float cloudPhase = cloudDensity * 1.5 + time * 0.1 + midsEnv * 0.4;
                vec3 cloudTint = paletteSweep(cloudPhase, palPrimary, palSecondary,
                                               palAccent, palGlow, midsEnv);
                interiorCol = mix(interiorCol, cloudTint * brightness, 0.4);

                // ------- Layer 2: Interior lightning flashes -------
                // Brief bright flashes that illuminate the clouds from within
                {
                    for (int fl = 0; fl < 3; fl++) {
                        float flSeed = float(fl) * 23.7 + iSeed * 5.0;
                        float flPeriod = 1.5 + hash21(vec2(flSeed, 1.0)) * 3.0;
                        float flPhase = fract(time / flPeriod + hash21(vec2(flSeed, 2.0)));

                        // Brief flash window
                        float flActive = smoothstep(0.0, 0.005, flPhase)
                                       * (1.0 - smoothstep(0.06, 0.1, flPhase));
                        flActive *= 0.5 + bassEnv * 2.0 + idlePulse * 0.6;

                        if (flActive < 0.01) continue;

                        // Flash epicenter moves each cycle
                        float epX = hash21(vec2(flSeed, floor(time / flPeriod))) * 0.6 + 0.2;
                        float epY = hash21(vec2(flSeed + 3.0, floor(time / flPeriod))) * 0.6 + 0.15;
                        vec2 epicenter = vec2(epX, epY);

                        float flashDist = length(iLogoUV - epicenter);
                        float flashGlow = exp(-flashDist * 4.0);

                        // Flicker
                        float flicker = 0.6 + 0.4 * step(0.3, hash21(vec2(flSeed, time * 20.0)));

                        // Flash illuminates clouds — dramatic
                        float cloudIllum = flashGlow * (0.4 + cloudDensity * 0.6);
                        vec3 flashCol = mix(palGlow, vec3(1.0), 0.5);
                        interiorCol += flashCol * cloudIllum * flActive * flicker * 2.0;

                        // Diffuse glow spread from flash
                        interiorCol += palAccent * 0.4 * flashGlow * flActive * flicker;

                        // Flash brightens the whole interior slightly
                        interiorCol += palPrimary * 0.15 * flActive * flicker;
                    }
                }

                // ------- Layer 3: Rain streaks flowing downward -------
                {
                    float rainStr = 0.0;
                    for (int ri = 0; ri < 3; ri++) {
                        float rSeed = float(ri) * 7.13 + iSeed * 3.0;
                        float rScale = 30.0 + float(ri) * 15.0;
                        // Rain falls downward (positive Y in logo space)
                        vec2 rainUV = iLogoUV * vec2(rScale, rScale * 0.3);
                        rainUV.y += time * (3.0 + float(ri) * 1.5 + trebleEnv * 2.0);
                        rainUV.x += simplex2D(vec2(iLogoUV.y * 8.0 + rSeed, time * 0.3)) * 0.5;

                        vec2 rainCell = floor(rainUV);
                        vec2 rainFract = fract(rainUV);

                        // Elongated rain drop (tall and thin)
                        float rHash = hash21(rainCell + rSeed);
                        vec2 rCenter = vec2(hash21(rainCell + vec2(rSeed, 1.0)) * 0.6 + 0.2, 0.5);
                        vec2 rDelta = rainFract - rCenter;
                        float streak = smoothstep(0.015, 0.0, abs(rDelta.x))
                                     * smoothstep(0.3, 0.0, abs(rDelta.y));
                        // Branchless: step replaces divergent if (rHash > 0.6)
                        rainStr += streak * (0.5 + rHash * 0.5) * step(0.6, rHash);
                    }
                    // Rain visible at base level, treble intensifies
                    float rainVis = rainStr * (0.3 + trebleEnv * 0.5) * brightness;
                    interiorCol += palGlow * rainVis;
                }

                // ------- Layer 4: Energy contour waves (animated outward pulse) -------
                {
                    float sdfAbs = -fDist;
                    float contourSpacing = gridScale * 0.015;

                    // Contours expand outward from center over time — animated rings
                    float waveSpeed = speed * 2.0 + bassEnv * logoPulse * 3.0;
                    float animOffset = fract(time * waveSpeed * 0.3);
                    float contourVal = fract((sdfAbs + animOffset * contourSpacing) / contourSpacing);

                    float contourSharp = contrast + trebleEnv * 2.0;
                    float contourLine = smoothstep(0.06 / contourSharp, 0.0, contourVal)
                                      + smoothstep(0.06 / contourSharp, 0.0, 1.0 - contourVal);

                    // Altitude color shift on contours
                    vec3 contourCol = mix(palAccent * 0.8, palGlow, smoothstep(0.3, 0.9, alt));

                    // Bass makes contours brighter and wider
                    float contourStr = gridStrength * (1.0 + bassEnv * logoPulse * 1.0);
                    interiorCol += contourCol * contourLine * contourStr;
                }

                // ------- Layer 5: Summit beacon -------
                {
                    vec2 summitPt = vec2(0.5, 0.0);
                    float summitDist = length(iLogoUV - summitPt);

                    float beaconGlow = exp(-summitDist * 10.0) * 0.8;
                    float beaconPulse = 0.6 + 0.4 * pow(max(sin(time * 1.5 + iPhase), 0.0), 2.0);
                    beaconPulse *= 1.0 + bassEnv * 1.2;

                    vec3 beaconCol = mix(palPrimary, palGlow, 0.5);
                    interiorCol += beaconCol * beaconGlow * beaconPulse;
                }

                // ------- Layer 6: Edge energy (traveling pulse along polygon) -------
                {
                    float edgeProximity = iLogo.edgeDist;
                    float edgeWidth = 0.006 + bassEnv * 0.004;
                    float edgeGlow = smoothstep(edgeWidth, edgeWidth * 0.1, edgeProximity);

                    // Traveling wave along edges
                    float pulsePhase = iLogoUV.x * 10.0 + iLogoUV.y * 6.0 - time * 2.5 + iPhase;
                    float pulse = pow(max(sin(pulsePhase), 0.0), 3.0);
                    edgeGlow *= 0.4 + pulse * 0.6;

                    // Treble sparks along edges
                    if (trebleEnv > 0.01) {
                        float edgeSpark = simplex2D(iLogoUV * 40.0 + time * 6.0 + iSeed * 20.0);
                        edgeSpark = smoothstep(0.5, 0.9, edgeSpark) * trebleEnv;
                        edgeGlow += edgeSpark * smoothstep(edgeWidth * 2.0, 0.0, edgeProximity);
                    }

                    interiorCol += palAccent * edgeGlow * 1.0;
                }

                // ------- Interior compositing -------
                interiorCol *= 1.0 + bassEnv * logoPulse * 0.5;

                // Volumetric rim light near edges
                float rimLight = exp(-depth * 6.0);
                interiorCol += palGlow * rimLight * 0.3 * (1.0 + midsEnv * 0.5);

                // Fresnel edge highlight
                float fresnelLike = smoothstep(-0.012, -0.001, fDist);
                interiorCol += palGlow * fresnelLike * 0.35 * (1.0 + midsEnv * 0.5);

                // Reinhard tonemap on interior only
                interiorCol = interiorCol / (1.0 + interiorCol);

                // Post-tonemap saturation boost
                float lum = luminance(interiorCol);
                interiorCol = mix(vec3(lum), interiorCol, 1.5);
                interiorCol = max(interiorCol, vec3(0.0));

                // Anti-alias at SDF boundary
                float aa = smoothstep(0.0, -0.003, fDist);
                logoCol = mix(logoCol, interiorCol * instIntensity, aa);
            }

            // ── Edge discharge on treble ─────────────────────────
            if (trebleEnv > 0.01 && fDist > -0.004 && fDist < 0.018) {
                float sparkN = simplex2D(iLogoUV * 30.0 + time * 5.0 + float(li) * 33.0) * 0.5 + 0.5;
                sparkN = smoothstep(0.5, 0.92, sparkN);
                float edgeMask = smoothstep(0.018, 0.0, abs(fDist));
                col += palGlow * sparkN * edgeMask * trebleEnv * sparkleStr * depthFactor;
            }

            // ── Multi-layer glow ─────────────────────────────────
            if (fDist > -0.01 && fDist < 0.20) {
                float clampedDist = max(fDist, 0.0);
                float glow1 = exp(-clampedDist * 80.0) * 0.45;
                float glow2 = exp(-clampedDist * 22.0) * 0.2;
                float glow3 = exp(-clampedDist * 7.0) * 0.1;
                vec3 edgeCol = paletteSweep(time * 0.1 + iLogoUV.y * 0.6 + float(li) * 0.25,
                                             palPrimary, palSecondary, palAccent, palGlow, midsEnv);
                float flare = 1.0 + bassEnv * 0.5;
                col += edgeCol * glow1 * flare * particleStr * 1.8 * depthFactor;
                col += palPrimary * glow2 * flare * 0.4 * depthFactor;
                col += palAccent * glow3 * 0.3 * depthFactor;
            }

            // ── Two-pass composite ───────────────────────────────
            float fillAlpha = smoothstep(0.005, -0.005, fDist);

            // Pass 1: inside logo — opaque fill replaces background
            col = mix(col, logoCol, fillAlpha);

            // Pass 2: outside logo — dim background near logo, add raw outer effects
            float outerMask = (1.0 - fillAlpha) * logoVignette;
            float proximityDim = smoothstep(0.20, 0.0, fDist) * 0.6;
            col *= 1.0 - proximityDim * outerMask;
            col += outerCol * outerMask;

        } // end logo instance loop

        // ── Vitality ─────────────────────────────────────────────
        if (isHighlighted) {
            col *= 1.1;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.25);
            col *= 0.7 + idlePulse * 0.08;
        }

        // ── Inner edge glow ──────────────────────────────────────
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.6, 1.0, 1.0 - depthDarken * 0.35);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.04 + midsEnv * 0.15;
        vec3 iriCol = paletteSweep(iriT, palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        col += iriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.15);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border ───────────────────────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x);
        float borderElev = simplex2D(vec2(sin(angle), cos(angle)) * 3.0 + time * 0.15) * 0.5 + 0.5;
        vec3 borderCol = paletteSweep(borderElev * contrast + midsEnv * 0.2,
                                       palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= borderBrightness;

        if (isHighlighted) {
            float bBreathe = 0.85 + 0.15 * sin(time * 2.5);
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.3 : 1.0;
            borderCol *= bBreathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.3);
            borderCol *= 0.55;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // ── Outer glow ───────────────────────────────────────────────
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 18.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 7.0, borderGlow);
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.06) + midsEnv * 0.1;
        vec3 glowCol = paletteSweep(glowT, palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}


// =================================================================
//  LABEL COMPOSITING
// =================================================================

vec4 compositeArchLabels(vec4 color, vec2 fragCoord,
                         float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, ARCH_BLUE);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, ARCH_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, ARCH_ICE);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, ARCH_SNOW);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    float time = iTime;

    float bassR   = hasAudio ? bass * labelAudioReact   : 0.0;
    float midsR   = hasAudio ? mids * labelAudioReact   : 0.0;
    float trebleR = hasAudio ? treble * labelAudioReact : 0.0;

    // ── 3-pass Gaussian halo ─────────────────────────────────────
    float haloSmooth = 0.0;
    float haloWide = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 offTight = vec2(float(dx), float(dy)) * px * labelGlowSpread;
            float s = texture(uZoneLabels, uv + offTight).a;
            haloSmooth += s * exp(-r2 * 0.4);

            vec2 offWide = vec2(float(dx), float(dy)) * px * labelGlowSpread * 1.5;
            float sw = texture(uZoneLabels, uv + offWide).a;
            haloWide += sw * exp(-r2 * 0.15);
        }
    }
    haloSmooth /= 8.0;
    haloWide   /= 12.0;

    float haloAtmo = 0.0;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px * labelGlowSpread * 1.8;
            // Branchless: step zeroes contribution for corners where r2 > 12
            haloAtmo += texture(uZoneLabels, uv + off).a * exp(-r2 * 0.12) * step(r2, 12.0);
        }
    }
    haloAtmo /= 18.0;

    float haloAngle = atan(uv.y - 0.5, uv.x - 0.5);

    if (haloAtmo > 0.001) {
        float outerMask = haloAtmo * (1.0 - labels.a);
        float innerMask = haloSmooth * (1.0 - labels.a);
        float midMask   = haloWide * (1.0 - labels.a);

        // Halo color: clean monochrome primary/accent, not organic sweep
        vec3 haloCol = mix(palPrimary, palAccent, 0.3);

        color.rgb += haloCol * innerMask * (2.0 + bassR * 1.2);
        color.rgb += haloCol * midMask * (1.2 + bassR * 0.6);

        vec3 outerC = mix(haloCol, palGlow, 0.3);
        color.rgb += outerC * outerMask * (0.6 + bassR * 0.4);

        // 4-fold angular rays (chevron/cross pattern, not 6-fold organic)
        float rayAngle = mod(haloAngle + PI * 0.25, TAU / 4.0) - TAU / 8.0;
        float ray = exp(-abs(rayAngle) * (25.0 + trebleR * 35.0));
        float rayPulse = 0.7 + 0.3 * step(0.5, fract(time * 1.5)); // digital blink, not sine
        color.rgb += palAccent * 1.5 * ray * midMask * rayPulse;

        // Treble: sharp pixel-like flashes (grid-aligned, not organic noise)
        if (trebleR > 0.04) {
            vec2 sparkGrid = floor(uv * 80.0 + time * 2.0);
            float spark = step(0.85, hash21(sparkGrid)) * trebleR * 3.0;
            color.rgb += palAccent * innerMask * spark;
        }

        color.a = max(color.a, midMask * 0.8);
    }

    // ── Text body ────────────────────────────────────────────────
    if (labels.a > 0.01) {
        // Sharp CRT scan lines
        float scanFreq = iResolution.y * 0.5;
        float scanlines = 0.8 + 0.2 * step(0.5, fract(fragCoord.y / 2.0));

        // Edge detection for glow outline
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float edgeStrength = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

        // Text color: clean primary with accent edge glow
        // Slight horizontal gradient (left to right) for digital feel
        float textT = uv.x * 0.4 + 0.3;
        vec3 textCol = mix(palPrimary, palAccent, textT);

        // Traveling cursor highlight (horizontal scan)
        float cursorSpeed = 0.15 + bassR * 0.3;
        float cursorPos = fract(time * cursorSpeed);
        float cursorX = fragCoord.x / max(iResolution.x, 1.0);
        float cursorDist = abs(cursorX - cursorPos);
        cursorDist = min(cursorDist, 1.0 - cursorDist);
        float cursorHighlight = smoothstep(0.02, 0.0, cursorDist);

        textCol *= scanlines;
        textCol = mix(textCol, palGlow * 2.0, cursorHighlight * 0.4);
        textCol += palAccent * edgeStrength * 0.7;

        textCol *= (1.0 + bassR * 0.4);
        textCol *= labelBrightness;

        // Treble: grid-aligned pixel flashes
        if (trebleR > 0.04) {
            vec2 sparkGrid = floor(uv * 80.0 + time * 3.0);
            float spark = step(0.88, hash21(sparkGrid)) * trebleR;
            textCol = mix(textCol, palGlow * 2.5, spark * 0.5);
        }

        textCol = textCol / (0.6 + textCol);
        float textLum = dot(textCol, vec3(0.2126, 0.7152, 0.0722));
        textCol = mix(vec3(textLum), textCol, 1.3);
        textCol = max(textCol, vec3(0.0));

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

    if (zoneCount == 0) { fragColor = vec4(0.0); return; }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        vec4 zoneColor = renderArchZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[7].y > 0.5)
        color = compositeArchLabels(color, fragCoord, bass, mids, treble, hasAudio);
    fragColor = clampFragColor(color);
}
