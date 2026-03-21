// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * KDE NEON DRIFT - Fragment Shader (Neon Tube Gear — Multi-Instance)
 *
 * KDE Neon gear logo rendered as glowing neon tube lighting against a
 * sunset-inspired sky with aurora bands, drifting cloud wisps, rising
 * ember particles, and prismatic gear glow.
 *
 * Palette inspired by KDE Neon sunset theme: warm pinks, oranges,
 * purples blending into the signature teal/blue gear neon.
 *
 * Gear geometry from official KDE Neon SVG (48x48 viewBox):
 *   Outer ring  r=0.391    Inner ring  r=0.278
 *   8 teeth at 45deg       8 dots at r=0.412
 *   Central circle r=0.103
 *
 * Audio reactivity:
 *   Bass   = gear spin + arc discharge + tendril surge + shockwaves + warmth shift
 *   Mids   = hue drift + ring breathe + tendril sway + aurora intensity
 *   Treble = flicker + electron speed + edge sparks + ember spawn rate
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ── Sunset palette constants (from KDE Neon theme wallpaper) ────
const vec3 SUNSET_PINK   = vec3(0.831, 0.341, 0.478);
const vec3 SUNSET_ORANGE = vec3(0.910, 0.627, 0.251);
const vec3 SUNSET_PURPLE = vec3(0.420, 0.247, 0.627);
const vec3 SUNSET_WARM   = vec3(0.910, 0.627, 0.753);


// ── Noise helpers ───────────────────────────────────────────────

float rand2D(in vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

float noise(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));
    vec2 u = quintic(f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(in vec2 uv, int octaves, float rotAngle) {
    float value = 0.0;
    float amplitude = 0.5;
    float c = cos(rotAngle), s = sin(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.55;
    }
    return value;
}


// ── KDE Neon palette ─────────────────────────────────────────────

vec3 neonPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else                return mix(accent, primary, (t - 0.66) * 3.0);
}

// Sunset-warm variant: blends sunset colors into the neon palette
vec3 sunsetPalette(float t, float warmth) {
    t = fract(t);
    vec3 cool;
    if (t < 0.33)      cool = mix(SUNSET_PURPLE, SUNSET_PINK, t * 3.0);
    else if (t < 0.66) cool = mix(SUNSET_PINK, SUNSET_ORANGE, (t - 0.33) * 3.0);
    else                cool = mix(SUNSET_ORANGE, SUNSET_WARM, (t - 0.66) * 3.0);
    return cool;
}


// ── SDF primitives ──────────────────────────────────────────────

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}


// ═══════════════════════════════════════════════════════════════
//  NEON TUBE RENDERING
// ═══════════════════════════════════════════════════════════════

vec3 neonTube(float dist, vec3 tubeColor, float intensity) {
    float core = exp(-abs(dist) * 200.0) * 1.2;
    float inner = exp(-abs(dist) * 50.0) * 0.7;
    float bloom = exp(-abs(dist) * 18.0) * 0.4;
    float ambient = exp(-abs(dist) * 6.0) * 0.08;

    vec3 white = vec3(1.0, 1.0, 0.98);
    vec3 lightTint = mix(white, tubeColor, 0.55);
    vec3 col = white * core
             + lightTint * inner
             + tubeColor * bloom
             + tubeColor * 0.6 * ambient;

    return col * intensity;
}

float neonFlicker(float time, float seed, float trebleEnv) {
    float base = 0.92 + 0.08 * sin(time * 60.0 + seed * 100.0);
    float buzz = step(0.97, noise(vec2(time * 30.0, seed * 7.0))) * 0.4;
    float trebleBuzz = trebleEnv * step(0.9, noise(vec2(time * 50.0, seed * 13.0))) * 0.5;
    return clamp(base - buzz - trebleBuzz, 0.4, 1.0);
}


// ═══════════════════════════════════════════════════════════════
//  HEXAGONAL GRID
// ═══════════════════════════════════════════════════════════════

vec3 hexGrid(vec2 p, float scale) {
    p *= scale;
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = mod(p, r) - h;
    vec2 b = mod(p - h, r) - h;
    vec2 gv = (dot(a, a) < dot(b, b)) ? a : b;
    float edgeDist = 0.5 - max(abs(gv.x), abs(gv.y) * 0.5773 + abs(gv.x) * 0.5);
    vec2 id = p - gv;
    float cellHash = hash21(id);
    return vec3(edgeDist, cellHash, length(gv));
}


// ═══════════════════════════════════════════════════════════════
//  KDE NEON GEAR GEOMETRY
// ═══════════════════════════════════════════════════════════════

const float R_OUTER_RING  = 0.391;
const float R_OUTER_TEETH = 0.412;
const float R_INNER_RING  = 0.278;
const float R_INNER_VIS   = 0.268;
const float R_CENTRAL     = 0.103;
const float RING_HALF_W   = 0.013;
const float TOOTH_HALF_W  = 0.016;
const float R_DOT         = 0.019;

const vec2 GEAR_CENTER = vec2(0.50, 0.50);


// ═══════════════════════════════════════════════════════════════
//  GEAR NEON RENDERING
// ═══════════════════════════════════════════════════════════════

float gearSDF(vec2 p, float rotOuter, float ringBreath, float toothScatter, float time) {
    float r = length(p);
    float d = 1e9;
    d = min(d, abs(r - R_OUTER_RING) - RING_HALF_W * ringBreath);
    d = min(d, abs(r - R_INNER_RING) - RING_HALF_W * ringBreath);
    d = min(d, abs(r - R_CENTRAL) - 0.008 * ringBreath);
    for (int i = 0; i < 8; i++) {
        float a = float(i) * TAU / 8.0 + rotOuter;
        vec2 dir = vec2(cos(a), sin(a));
        float scatter = toothScatter * (0.5 + 0.5 * sin(float(i) * 2.4 + time));
        vec2 p1 = dir * (R_INNER_VIS - 0.005);
        vec2 p2 = dir * (R_OUTER_TEETH + 0.005 + scatter * 0.04);
        d = min(d, sdSegment(p, p1, p2) - TOOTH_HALF_W);
        vec2 dotPos = dir * (R_OUTER_TEETH + scatter * 0.04);
        d = min(d, length(p - dotPos) - R_DOT);
    }
    return d;
}

vec3 evalGearNeon(vec2 p, float rotOuter, float rotInner, float time,
                   float bassEnv, float midsEnv, float trebleEnv,
                   float toothScatter,
                   vec3 palTeal, vec3 palBlue, vec3 palCyan, vec3 palGlow,
                   float intensity, float flickerSeed) {
    vec3 col = vec3(0.0);
    float r = length(p);

    float flicker = neonFlicker(time, flickerSeed, trebleEnv);
    float tubeIntensity = intensity * flicker;
    float ringBreath = 1.0 + midsEnv * 0.3;

    // Outer ring
    float outerDist = abs(r - R_OUTER_RING) - RING_HALF_W * ringBreath;
    col += neonTube(outerDist, palTeal, tubeIntensity * 1.1);

    // Inner ring
    float innerDist = abs(r - R_INNER_RING) - RING_HALF_W * ringBreath;
    col += neonTube(innerDist, palBlue, tubeIntensity * 1.0);

    // Central circle
    float centerDist = abs(r - R_CENTRAL) - 0.008 * ringBreath;
    col += neonTube(centerDist, palCyan, tubeIntensity * 1.5);
    float centerFill = exp(-r * r / (R_CENTRAL * R_CENTRAL * 0.4)) * 0.3;
    col += palGlow * centerFill * tubeIntensity;

    // 8 radial teeth
    for (int i = 0; i < 8; i++) {
        float a = float(i) * TAU / 8.0 + rotOuter;
        vec2 dir = vec2(cos(a), sin(a));
        float scatter = toothScatter * (0.5 + 0.5 * sin(float(i) * 2.4 + time));
        vec2 p1 = dir * (R_INNER_VIS - 0.005);
        vec2 p2 = dir * (R_OUTER_TEETH + 0.005 + scatter * 0.04);
        float toothDist = sdSegment(p, p1, p2) - TOOTH_HALF_W;
        vec3 toothCol = (i % 2 == 0) ? mix(palTeal, palCyan, 0.4)
                                       : mix(palBlue, palCyan, 0.3);
        col += neonTube(toothDist, toothCol, tubeIntensity * 0.95);
    }

    // 8 orbital dots
    for (int i = 0; i < 8; i++) {
        float a = float(i) * TAU / 8.0 + rotOuter;
        vec2 dir = vec2(cos(a), sin(a));
        float scatter = toothScatter * (0.5 + 0.5 * sin(float(i) * 2.4 + time));
        vec2 dotPos = dir * (R_OUTER_TEETH + scatter * 0.04);
        float dotDist = length(p - dotPos) - R_DOT;
        col += neonTube(dotDist, palGlow, tubeIntensity * 1.4);
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  ELECTRIC ARCS
// ═══════════════════════════════════════════════════════════════

vec3 electricArc(vec2 p, vec2 from, vec2 to, float time, float strength,
                  vec3 arcColor, float seed) {
    if (strength < 0.01) return vec3(0.0);

    vec3 col = vec3(0.0);
    vec2 dir = to - from;
    float len = length(dir);
    vec2 norm = vec2(-dir.y, dir.x) / max(len, 0.001);

    float closest = 1e9;
    for (int j = 0; j < 8; j++) {
        float t = float(j) / 7.0;
        vec2 basePos = mix(from, to, t);
        float jag = noise(vec2(t * 12.0 + seed * 7.0, time * 8.0 + seed)) * 2.0 - 1.0;
        jag += noise(vec2(t * 25.0 + seed * 13.0, time * 15.0)) * 0.5 - 0.25;
        float envelope = t * (1.0 - t) * 4.0;
        vec2 arcPos = basePos + norm * jag * 0.03 * envelope;
        closest = min(closest, length(p - arcPos));
    }

    float arcGlow = exp(-closest * 160.0) * strength * 1.5;
    float arcBloom = exp(-closest * 25.0) * strength * 0.7;
    col += vec3(1.0) * arcGlow + arcColor * arcBloom;

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  PLASMA TENDRILS
// ═══════════════════════════════════════════════════════════════

vec3 plasmaTendrils(vec2 p, float time, float bassEnv, float midsEnv,
                     vec3 palTeal, vec3 palCyan, int tendrilCount) {
    vec3 col = vec3(0.0);
    float r = length(p);

    for (int i = 0; i < tendrilCount && i < 12; i++) {
        float baseAngle = float(i) * TAU / float(tendrilCount);
        float sway = sin(time * 0.5 + float(i) * 1.3) * 0.2 * (1.0 + midsEnv);
        float tendrilAngle = baseAngle + sway;

        float angle = atan(p.y, p.x);
        float angleDiff = abs(mod(angle - tendrilAngle + PI, TAU) - PI);

        float width = (0.08 - r * 0.12) * (1.0 + bassEnv * 0.5);
        width = max(width, 0.01);

        float tendril = smoothstep(width, 0.0, angleDiff);
        float radialMask = smoothstep(R_CENTRAL, R_CENTRAL + 0.05, r) *
                           smoothstep(R_OUTER_RING + 0.05, R_INNER_RING, r);

        float flow = noise(vec2(r * 8.0 - time * 3.0, float(i) * 5.0));
        tendril *= flow * radialMask;

        float surge = 1.0 + bassEnv * 2.0;
        vec3 tendrilCol = neonPalette(float(i) / float(tendrilCount) + time * 0.05,
                                       palTeal, palCyan, palTeal);
        col += tendrilCol * tendril * 0.35 * surge;
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  ORBITAL ELECTRONS
// ═══════════════════════════════════════════════════════════════

vec3 orbitalElectrons(vec2 p, float time, float trebleEnv,
                       vec3 palGlow, int electronCount) {
    vec3 col = vec3(0.0);

    for (int i = 0; i < electronCount && i < 16; i++) {
        float h = hash11(float(i) * 7.31);
        float orbitR = mix(R_INNER_VIS, R_OUTER_TEETH + 0.05, h);
        float speed = (1.5 + h * 2.0) * (1.0 + trebleEnv * 1.0);
        float dir = (i % 2 == 0) ? 1.0 : -1.0;
        float angle = time * speed * dir + float(i) * TAU / float(electronCount);

        vec2 electronPos = vec2(cos(angle), sin(angle)) * orbitR;
        float dist = length(p - electronPos);

        float point = exp(-dist * dist * 6000.0) * 1.0;
        float bloom = exp(-dist * 40.0) * 0.25;

        float trailLen = 0.3 + trebleEnv * 0.2;
        float trail = 0.0;
        for (int t = 1; t <= 4; t++) {
            float trailAngle = angle - dir * float(t) * 0.08 * trailLen;
            vec2 trailPos = vec2(cos(trailAngle), sin(trailAngle)) * orbitR;
            float trailDist = length(p - trailPos);
            trail += exp(-trailDist * 80.0) * 0.1 / float(t);
        }

        vec3 eCol = neonPalette(h + time * 0.1, palGlow, vec3(1.0), palGlow);
        col += eCol * (point + bloom + trail);
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  AURORA BANDS — flowing sinusoidal color waves
// ═══════════════════════════════════════════════════════════════

vec3 auroraBands(vec2 uv, float time, float bassEnv, float midsEnv,
                  float trebleEnv, float aspect,
                  vec3 palPrimary, vec3 palAccent) {
    vec3 col = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float freq = 1.5 + fi * 0.7;
        float phase = fi * 1.3 + time * (0.15 + fi * 0.05);
        // Bass pumps the wave amplitude — bands swell and contract
        float amp = (0.12 + fi * 0.04) * (1.0 + bassEnv * 0.6);

        // Sinusoidal ribbon with noise perturbation
        float ribbon = sin(uv.x * freq * aspect + phase) * amp;
        ribbon += noise(vec2(uv.x * 3.0 + time * 0.3, fi * 7.0)) * 0.06;
        // Bass adds low-freq wave distortion — bands undulate
        ribbon += sin(uv.x * 0.8 * aspect + time * 0.5 + fi * 2.0) * bassEnv * 0.08;
        float dist = abs(uv.y - 0.5 - ribbon);

        // Treble tightens the band — sharper edges on high frequencies
        float sharpness = 600.0 + trebleEnv * 400.0;
        float band = exp(-dist * dist * sharpness) * 0.4;
        float haze = exp(-dist * 14.0) * 0.10;

        float t = fi * 0.25 + time * 0.03 + midsEnv * 0.15;
        vec3 auroraCol = mix(
            sunsetPalette(t, 1.0),
            neonPalette(t + 0.3, palPrimary, palAccent, SUNSET_PURPLE),
            0.5 + 0.5 * sin(time * 0.2 + fi)
        );

        float intensity = 0.9 + midsEnv * 0.5;
        col += auroraCol * (band + haze) * intensity;
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  EMBER PARTICLES — warm particles rising upward
// ═══════════════════════════════════════════════════════════════

vec3 emberParticles(vec2 uv, float time, float trebleEnv, float bassEnv,
                     float particleStr) {
    vec3 col = vec3(0.0);

    for (int layer = 0; layer < 3; layer++) {
        float fl = float(layer);
        float gridSize = 8.0 + fl * 4.0;
        float riseSpeed = 0.3 + fl * 0.15 + trebleEnv * 0.2;

        // Embers rise upward with slight horizontal drift
        vec2 emberUV = uv * gridSize;
        emberUV.y -= time * riseSpeed;
        emberUV.x += sin(time * 0.4 + fl * 2.0) * 0.5;

        vec2 cell = floor(emberUV);
        vec2 local = fract(emberUV);

        float h1 = hash21(cell + fl * 100.0);
        float h2 = hash21(cell * 1.7 + fl * 50.0);

        // Only some cells have embers
        if (h1 > 0.6) {
            vec2 center = vec2(h1, h2) * 0.6 + 0.2;
            float dist = length(local - center);

            // Ember size varies
            float radius = 0.04 + h2 * 0.03 - fl * 0.01;
            float ember = smoothstep(radius, radius * 0.15, dist);

            // Twinkle/flicker
            float twinkle = 0.4 + 0.6 * sin(h1 * TAU * 5.0 + time * (4.0 + h2 * 3.0));
            twinkle *= 0.7 + 0.3 * step(0.3, noise(vec2(time * 2.0, h1 * 20.0)));

            // Color: warm orange to pink, brighter at higher treble
            vec3 emberCol = mix(SUNSET_ORANGE, SUNSET_PINK, h2);
            emberCol = mix(emberCol, SUNSET_WARM, trebleEnv * 0.4);

            col += emberCol * ember * twinkle * (1.0 + bassEnv * 0.5);
        }
    }

    return col * particleStr * 1.2;
}


// ═══════════════════════════════════════════════════════════════
//  PER-INSTANCE UV COMPUTATION
// ═══════════════════════════════════════════════════════════════

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            sin(time * 0.13) * 0.015 + sin(time * 0.29) * 0.008,
            cos(time * 0.19) * 0.012 + cos(time * 0.11) * 0.006
        );
        uv -= drift;
        // Gentle rotation
        float rotAng = sin(time * 0.12) * 0.04;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + sin(time * 0.6) * 0.02;
        float springT = fract(time * 1.2);
        float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + GEAR_CENTER;
        return uv;
    }

    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.06 + float(idx) * 0.021;
    float f2 = 0.04 + float(idx) * 0.017;
    vec2 drift = vec2(
        sin(time * f1 + h1 * TAU) * roam + sin(time * f1 * 2.1 + h3 * TAU) * roam * 0.3,
        cos(time * f2 + h2 * TAU) * roam * 0.9 + cos(time * f2 * 1.6 + h4 * TAU) * roam * 0.25
    );
    uv -= drift;

    // Per-instance rotation oscillation
    float rotAng = sin(time * (0.08 + float(idx) * 0.025) + h4 * TAU) * 0.05;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + sin(time * (0.5 + float(idx) * 0.11) + h1 * TAU) * 0.02;
    float springT = fract(time * 1.2 + h2);
    float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + GEAR_CENTER;
    return uv;
}


// ═══════════════════════════════════════════════════════════════
//  MAIN ZONE RENDER
// ═══════════════════════════════════════════════════════════════

vec4 renderNeonZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                     bool isHighlighted, float bass, float mids, float treble, float overall,
                     bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.12;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.25;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    int octaves         = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);

    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 4.0;
    float gridStrength  = customParams[1].y >= 0.0 ? customParams[1].y : 0.3;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.8;
    float contrast      = customParams[1].w >= 0.0 ? customParams[1].w : 0.9;

    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.45;
    float sparkleStr    = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;

    float fbmRot        = customParams[4].w >= 0.0 ? customParams[4].w : 0.6;
    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.3;

    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.55;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.85;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 4.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;

    float flowCenterX   = customParams[6].w >= -1.5 ? customParams[6].w : 0.5;
    float flowCenterY   = customParams[7].x >= -1.5 ? customParams[7].x : 0.5;

    float gearSpin      = customParams[7].z >= 0.0 ? customParams[7].z : 0.15;
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

    // KDE Neon brand palette
    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, vec3(0.102, 0.737, 0.612));
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, vec3(0.161, 0.502, 0.725));
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, vec3(0.239, 0.859, 0.761));
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, vec3(0.878, 1.0, 0.969));

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * idleStrength;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    // ── Audio envelopes ────────────────────────────────────────
    float bassEnv   = hasAudio ? smoothstep(0.02, 0.3, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * audioReact : 0.0;

    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;
    centeredUV.x *= aspect;

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        float audioColorShift = midsEnv * 0.15;
        // Bass-driven warmth: shifts palette toward sunset tones
        float warmth = bassEnv * 0.3 + 0.1;

        // ── Background: sunset sky gradient with FBM clouds ─────
        // Vertical gradient: warm orange/pink at bottom → teal/blue at top
        float skyT = globalUV.y;
        vec3 skyBottom = mix(SUNSET_ORANGE, SUNSET_PINK, 0.5 + 0.5 * sin(time * 0.05));
        vec3 skyTop = mix(palSecondary, SUNSET_PURPLE, 0.4);
        vec3 skyGrad = mix(skyBottom, skyTop, skyT);

        // Cloud wisps via FBM — flowCenter creates radial pull on cloud drift
        vec2 flowCenter = (vec2(flowCenterX, flowCenterY) * 2.0 - 1.0) * noiseScale;
        flowCenter.x *= aspect;
        vec2 toLogo = flowCenter - centeredUV;
        float pullStrength = 0.15 / (length(toLogo) + 0.1);
        vec2 cloudUV = centeredUV * 0.7
                     + (flowDir * flowSpeed + normalize(toLogo + 0.001) * pullStrength) * time * 0.3;
        float clouds = fbm(cloudUV, octaves, fbmRot);
        float cloudDetail = fbm(cloudUV * 1.5 + clouds * 0.8, max(octaves - 2, 3), fbmRot);

        // Blend cloud layers into sky — rich saturated clouds
        vec3 cloudCol = mix(SUNSET_WARM, palGlow, cloudDetail);
        float cloudMask = smoothstep(0.25, 0.6, clouds) * 0.6;
        vec3 col = mix(skyGrad, cloudCol, cloudMask);

        // Tint with user palette — keep saturation high
        float colorT = cloudDetail * contrast + audioColorShift;
        vec3 palTint = neonPalette(colorT, palPrimary, palSecondary, palAccent);
        col = mix(col, palTint, 0.35);
        col *= brightness * 0.85;

        // Warm shift on bass — saturated sunset blend
        vec3 warmCol = sunsetPalette(colorT + time * 0.02, warmth);
        col = mix(col, warmCol * brightness * 0.7, warmth * 0.4);

        // ── Aurora bands ────────────────────────────────────────
        col += auroraBands(globalUV, time, bassEnv, midsEnv, trebleEnv, aspect, palPrimary, palAccent);

        // ── Hexagonal grid overlay ──────────────────────────────
        vec3 hex = hexGrid(centeredUV + time * speed * 0.15, gridScale);
        float hexEdge = smoothstep(0.05, 0.0, hex.x);
        float gridAudio = 1.0 + trebleEnv * 0.8;
        vec3 hexColor = neonPalette(hex.y + colorT * 0.3, palPrimary, palSecondary, palAccent);
        col = mix(col, hexColor * 0.6 * gridAudio, hexEdge * gridStrength);

        // ── Multi-instance gear rendering ─────────────────────────
        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iGearUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            // Wide bounding check
            if (iGearUV.x < -0.4 || iGearUV.x > 1.4 ||
                iGearUV.y < -0.4 || iGearUV.y > 1.4) continue;

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            // Gear-local coordinates
            vec2 gearP = iGearUV - GEAR_CENTER;
            float gearR = length(gearP);

            // Circular vignette
            float gearVignette = 1.0 - smoothstep(0.48, 0.72, gearR);

            // Counter-rotating gear angles
            float spinBase = time * gearSpin * (1.0 + bassEnv * 0.8);
            float rotDir = (li % 2 == 0) ? 1.0 : -1.0;
            float rotOuter = spinBase * rotDir + float(li) * 1.618;
            float rotInner = -spinBase * rotDir * 1.3 + float(li) * 2.718;
            float toothScatter = bassEnv * logoPulse;
            float ringBreath = 1.0 + midsEnv * 0.3;

            // Compute gear SDF for multi-layer effects
            float gDist = gearSDF(gearP, rotOuter, ringBreath, toothScatter, time);

            // Skip if too far from gear (optimization)
            if (gDist > 0.15) continue;

            vec3 gearCol = vec3(0.0);

            // ── Neon light casting (warm halo in fog) ──────────────
            float lightCast = exp(-max(gDist, 0.0) * 20.0) * 0.2;
            vec3 logoLight = mix(
                neonPalette(time * 0.08 + iGearUV.y + float(li) * 0.3,
                              palGlow, palPrimary, palAccent),
                SUNSET_WARM,
                warmth * 0.4
            );
            gearCol += logoLight * lightCast * instIntensity * (1.0 + bassEnv * 0.2) * depthFactor;

            // ── Per-instance bass shockwave ring ───────────────────
            float iShockPhase = fract(time * 0.7 + float(li) * 0.137);
            float iShockStr = bassEnv * (1.0 - iShockPhase) * logoPulse;
            if (iShockStr > 0.01) {
                float iShockRadius = iShockPhase * 0.5;
                float shockDist = abs(gearR - iShockRadius);
                float shockMask = smoothstep(0.06, 0.0, shockDist) * iShockStr;
                vec3 shockCol = mix(
                    neonPalette(iShockRadius * 3.0 + time * 0.2 + float(li),
                                  palGlow, palPrimary, palAccent),
                    SUNSET_PINK,
                    warmth * 0.3
                );
                gearCol += shockCol * shockMask * 0.2 * depthFactor;
            }

            // ── Gear neon tubes ───────────────────────────────────
            gearCol += evalGearNeon(gearP, rotOuter, rotInner, time,
                                     bassEnv, midsEnv, trebleEnv,
                                     toothScatter,
                                     palPrimary, palSecondary, palAccent, palGlow,
                                     instIntensity, float(li))
                       * depthFactor;

            // ── Prismatic glow — chromatic split on gear edges ─────
            if (gDist > -0.005) {
                // Warm channel glow (sunset pink/orange)
                float warmGlow = exp(-max(gDist, 0.0) * 50.0) * 0.2;
                // Cool channel glow (teal/cyan)
                float coolGlow = exp(-max(gDist, 0.0) * 65.0) * 0.25;
                // Ambient halo (tighter to avoid blobby shape)
                float haloGlow = exp(-max(gDist, 0.0) * 18.0) * 0.06;

                float flare = 1.0 + bassEnv * 0.6;
                // Angle-dependent color split creates prismatic rainbow effect
                float gAngle = atan(gearP.y, gearP.x);
                float prismT = fract(gAngle / TAU + time * 0.06);

                vec3 warmEdge = mix(SUNSET_PINK, SUNSET_ORANGE, prismT);
                vec3 coolEdge = mix(palPrimary, palAccent, 1.0 - prismT);

                gearCol += warmEdge * warmGlow * flare * particleStr * 1.5 * depthFactor;
                gearCol += coolEdge * coolGlow * flare * particleStr * 1.5 * depthFactor;
                gearCol += mix(palGlow, SUNSET_WARM, 0.3) * haloGlow * flare * 0.5 * depthFactor;
            }

            // ── Plasma tendrils from center ───────────────────────
            int tendrilCount = 6 + int(bassEnv * 4.0);
            gearCol += plasmaTendrils(gearP, time + float(li) * 3.0,
                                       bassEnv, midsEnv,
                                       palPrimary, palAccent, tendrilCount)
                       * instIntensity * depthFactor;

            // ── Electric arcs between teeth on bass ───────────────
            if (bassEnv > 0.1) {
                for (int ai = 0; ai < 8; ai++) {
                    float a1 = float(ai) * TAU / 8.0 + rotOuter;
                    float a2 = float((ai + 1) % 8) * TAU / 8.0 + rotOuter;
                    vec2 from = vec2(cos(a1), sin(a1)) * R_OUTER_TEETH;
                    vec2 to = vec2(cos(a2), sin(a2)) * R_OUTER_TEETH;

                    float arcSeed = float(ai) + float(li) * 8.0;
                    float arcChance = step(0.6, noise(vec2(arcSeed * 3.0, time * 2.0)));
                    float arcStr = bassEnv * logoPulse * arcChance * sparkleStr * 0.4;

                    // Arcs tinted with sunset warmth
                    vec3 arcCol = mix(palAccent, SUNSET_ORANGE, warmth * 0.5);
                    gearCol += electricArc(gearP, from, to, time, arcStr,
                                            arcCol, arcSeed)
                               * instIntensity * depthFactor;
                }

                for (int ai = 0; ai < 4; ai++) {
                    int ti = ai * 2;
                    float a1 = float(ti) * TAU / 8.0 + rotInner;
                    float a2 = float(ti) * TAU / 8.0 + rotOuter;
                    vec2 from = vec2(cos(a1), sin(a1)) * R_INNER_RING;
                    vec2 to = vec2(cos(a2), sin(a2)) * R_OUTER_RING;

                    float arcSeed = float(ti) + float(li) * 8.0 + 100.0;
                    float arcChance = step(0.7, noise(vec2(arcSeed * 3.0, time * 1.5)));
                    float arcStr = bassEnv * logoPulse * arcChance * sparkleStr * 0.3;

                    gearCol += electricArc(gearP, from, to, time, arcStr,
                                            palGlow, arcSeed)
                               * instIntensity * depthFactor;
                }
            }

            // ── Orbital electrons ─────────────────────────────────
            int electronCount = 4 + int(depthFactor * 4.0);
            gearCol += orbitalElectrons(gearP, time + float(li) * 5.0, trebleEnv,
                                         palGlow, electronCount)
                       * instIntensity * depthFactor * particleStr * 2.0;

            // ── Treble edge discharge sparks ──────────────────────
            if (trebleEnv > 0.01 && gDist > -0.005 && gDist < 0.025) {
                float sparkN = noise(iGearUV * 35.0 + time * 7.0 + float(li) * 33.0);
                sparkN = smoothstep(0.5, 0.95, sparkN);
                float edgeMask = smoothstep(0.025, 0.0, abs(gDist));
                vec3 sparkCol = mix(palGlow, SUNSET_WARM, 0.3);
                gearCol += mix(sparkCol, vec3(1.0), 0.5) * sparkN * edgeMask * trebleEnv * sparkleStr * depthFactor;
            }

            // ── Heat shimmer — UV distortion haze near gear ───────
            if (gDist > -0.01 && gDist < 0.08) {
                float shimmerMask = smoothstep(0.08, 0.0, gDist) * 0.6;
                float shimmerN = noise(iGearUV * 20.0 + vec2(time * 2.0, time * 1.3));
                float shimmer = shimmerN * shimmerMask * (1.0 + bassEnv * 0.5);
                vec3 heatCol = mix(SUNSET_ORANGE, palGlow, 0.5) * shimmer * 0.25;
                gearCol += heatCol * instIntensity * depthFactor;
            }

            // Soft-clamp gear color to prevent blowout (keeps color, tames peaks)
            gearCol = gearCol / (1.0 + gearCol);
            // Apply circular vignette
            col += gearCol * gearVignette;

        } // end gear instance loop

        // ── Ember particles (rising warm motes) ─────────────────────
        col += emberParticles(globalUV, time, trebleEnv, bassEnv, particleStr);

        // ── Vitality ───────────────────────────────────────────
        if (isHighlighted) {
            col *= 1.2;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.15);
            col *= 0.75 + idlePulse * 0.15;
        }

        // ── Inner edge glow (iridescent sunset/neon blend) ──────────
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.6, 1.0, 1.0 - depthDarken * 0.35);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.05 + midsEnv * 0.2;
        vec3 iriCol = mix(
            neonPalette(iriT, palPrimary, palSecondary, palAccent),
            sunsetPalette(iriT + 0.2, 1.0),
            0.3
        );
        col += iriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.1);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border ───────────────────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x) * 2.0;
        // FBM-animated border with sunset warmth
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * 0.4, 3, 0.5);
        vec3 borderCol = mix(
            neonPalette(borderFlow * contrast + midsEnv * 0.2,
                          palPrimary, palSecondary, palAccent),
            sunsetPalette(borderFlow + time * 0.03, 1.0),
            0.2
        );
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= borderBrightness;

        // Neon tube effect on border
        float borderNeon = exp(-abs(d) * 8.0) * 0.3;
        borderCol += palAccent * borderNeon;

        if (isHighlighted) {
            float bBreathe = 0.85 + 0.15 * sin(time * 2.5);
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.4 : 1.0;
            borderCol *= bBreathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.15);
            borderCol *= 0.7;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // ── Outer glow ───────────────────────────────────────────
    float bassGlowPush = hasAudio ? bassEnv * 2.5 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 20.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 8.0, borderGlow);
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.06) + midsEnv * 0.1;
        vec3 glowCol = mix(
            neonPalette(glowT, palPrimary, palSecondary, palAccent),
            sunsetPalette(glowT, 1.0),
            0.15
        );
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.8;
        result.a = max(result.a, glow * 0.6);
    }

    return result;
}

// ─── Custom Label Composite (Neon Sign Effect) ──────────────────────

vec4 compositeNeonLabels(vec4 color, vec2 fragCoord,
                          float bass, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, vec3(0.102, 0.737, 0.612));
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, vec3(0.161, 0.502, 0.725));
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, vec3(0.239, 0.859, 0.761));
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, vec3(0.878, 1.0, 0.969));

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    float time = iTime;

    // ── Multi-layer neon halo (tight core + colored bloom + wide haze) ──
    float haloTight = 0.0;
    float haloWide = 0.0;
    float haloVWide = 0.0;
    // Chromatic aberration offsets for retro neon look
    float haloR = 0.0, haloG = 0.0, haloB = 0.0;
    vec2 chromOff = vec2(px.x * 2.5, px.y * 0.8);

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px;

            float wTight = exp(-r2 * 0.6);
            float wWide = exp(-r2 * 0.2);
            float wVWide = exp(-r2 * 0.1);

            float s = texture(uZoneLabels, uv + off * labelGlowSpread).a;
            haloTight += s * wTight;
            haloWide += s * wWide;
            haloVWide += s * wVWide;

            // Chromatic channels (slightly offset samples)
            haloR += texture(uZoneLabels, uv + off * labelGlowSpread + chromOff).a * wWide;
            haloG += texture(uZoneLabels, uv + off * labelGlowSpread).a * wWide;
            haloB += texture(uZoneLabels, uv + off * labelGlowSpread - chromOff).a * wWide;
        }
    }
    haloTight /= 10.0;
    haloWide /= 16.5;
    haloVWide /= 20.0;
    haloR /= 16.5;
    haloG /= 16.5;
    haloB /= 16.5;

    // ── Neon sign flicker (per-region, not uniform) ──────────────────
    float flickerRegion = floor(uv.x * 6.0 + uv.y * 3.0);
    float flickerSeed = hash11(flickerRegion * 13.7);
    float flicker = 0.88 + 0.12 * sin(time * 55.0 + flickerSeed * 100.0);
    float buzzOff = step(0.96, noise(vec2(time * 25.0, flickerSeed * 7.0))) * 0.5;
    flicker = clamp(flicker - buzzOff, 0.35, 1.0);

    // ── Color sweep across text (neon powering on) ──────────────────
    float sweep = fract(time * 0.15);
    float sweepPos = uv.x * 0.7 + uv.y * 0.3;
    float sweepWave = smoothstep(sweep - 0.3, sweep, sweepPos) *
                      smoothstep(sweep + 0.3, sweep, sweepPos);
    // Moving highlight band
    float sweepBright = 1.0 + sweepWave * 0.6;

    if (haloWide > 0.003) {
        float haloEdge = haloWide * (1.0 - labels.a);
        float haloEdgeTight = haloTight * (1.0 - labels.a);
        float haloEdgeVWide = haloVWide * (1.0 - labels.a);

        // Animated color that cycles through sunset + neon palette
        float t = uv.x * 2.0 + time * 0.12;
        vec3 haloCol1 = neonPalette(t, palPrimary, palSecondary, palAccent);
        vec3 haloCol2 = sunsetPalette(t + 0.3, 1.0);
        vec3 haloCol = mix(haloCol1, haloCol2, 0.3 + 0.2 * sin(time * 0.3));

        // Bass warmth shift
        float bassStr = hasAudio ? bass * labelAudioReact : 0.0;
        haloCol = mix(haloCol, SUNSET_ORANGE, bassStr * 0.3);

        // Tight core: near-white hot center
        vec3 coreCol = mix(vec3(1.0, 1.0, 0.97), haloCol, 0.2);
        color.rgb += coreCol * haloEdgeTight * 0.8 * flicker * sweepBright;

        // Chromatic bloom: RGB channels slightly offset
        vec3 chromHalo = vec3(haloR, haloG, haloB) * (1.0 - labels.a);
        vec3 chromCol = chromHalo * haloCol * 0.6 * flicker;
        // Tint the shifted channels with warm/cool split
        chromCol.r *= 1.0 + 0.3 * (haloCol2.r - 0.5);
        chromCol.b *= 1.0 + 0.2 * (haloCol1.b - 0.5);
        color.rgb += chromCol;

        // Very wide ambient haze (soft colored fog around text)
        color.rgb += haloCol * 0.4 * haloEdgeVWide * flicker * (0.7 + bassStr * 0.5);

        // Treble sparks along the halo edge
        if (hasAudio && treble > 0.08) {
            float sparkNoise = noise2D(uv * 50.0 + time * 5.0);
            float spark = smoothstep(0.6, 0.92, sparkNoise) * treble * 2.5 * labelAudioReact;
            vec3 sparkCol = mix(palGlow, SUNSET_WARM, 0.4);
            color.rgb += sparkCol * haloEdge * spark * flicker;
        }

        // Downward drip glow (neon light bleeding down like wet reflections)
        float dripSample = texture(uZoneLabels, uv + vec2(0.0, -px.y * labelGlowSpread * 4.0)).a;
        float drip = dripSample * (1.0 - labels.a) * 0.3;
        if (drip > 0.01) {
            vec3 dripCol = mix(haloCol, SUNSET_PINK, 0.3) * drip;
            float dripFade = smoothstep(0.0, 0.15, uv.y); // fade near bottom
            color.rgb += dripCol * dripFade * flicker;
        }

        color.a = max(color.a, haloEdge * 0.6);
    }

    // ── Label text body: neon tube lit text ──────────────────────────
    if (labels.a > 0.01) {
        // Color sweep in pixel space — visible within each character
        float neonWave = sin(fragCoord.x * 0.2 - time * 2.5 + fragCoord.y * 0.12) * 0.5 + 0.5;
        vec3 tubeColor = mix(
            neonPalette(neonWave + time * 0.08, palPrimary, palAccent, palSecondary),
            sunsetPalette(neonWave + 0.15, 1.0),
            0.3
        );

        // Neon tube flicker noise — breaks up solid fill with bright/dim regions
        float tubeNoise = noise(fragCoord * 0.1 + time * 2.0);
        vec3 tubeCol = mix(tubeColor, vec3(1.0, 1.0, 0.97), tubeNoise * 0.4);

        // Stroke edge rim — neon tubes glow brightest at edges
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

        // Combine: neon tube body + bright white rim
        vec3 textCol = tubeCol * 0.7 + vec3(1.0, 1.0, 0.97) * rim * 0.5;
        textCol *= labelBrightness * flicker * sweepBright;

        float bassPulse = hasAudio ? 1.0 + bass * 0.6 * labelAudioReact : 1.0;
        textCol *= bassPulse;

        // Gentle tonemap
        textCol = textCol / (0.6 + textCol);

        color.rgb = mix(color.rgb, textCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

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
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderNeonZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[7].y > 0.5) {
        color = compositeNeonLabels(color, fragCoord, bass, treble, hasAudio);
    }
    fragColor = clampFragColor(color);
}
