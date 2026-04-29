// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * TUMBLEWEED DRIFT — openSUSE Tumbleweed branded zone overlay
 *
 * Desert wind / rolling release theme.  Arid landscape with perpetual
 * directional wind, sand particles, dust-devil vortices, erosion flow
 * lines, and drifting Tumbleweed pinwheel logos.
 *
 * Logo geometry: 5 subpaths, 150 polygon vertices (even-odd fill).
 * Coordinates centered on origin, range ~[-0.5, 0.5].
 *
 * Audio reactivity (organic — modulates existing animation):
 *   Bass  = flow acceleration + logo pulse + shockwave rings + dust bursts
 *   Mids  = palette warmth drift + vortex speed + iridescent edge shift
 *   Treble = sparkle + edge discharge + particle twinkle + scan acceleration
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

// ─── Parameter helpers ─────────────────────────────────────────────

float pSpeed()           { return customParams[0].x >= 0.0 ? customParams[0].x : 0.08; }
float pFlowSpeed()       { return customParams[0].y >= 0.0 ? customParams[0].y : 0.15; }
float pNoiseScale()      { return customParams[0].z >= 0.0 ? customParams[0].z : 3.5;  }
float pWindDirection()   { return customParams[0].w >= 0.0 ? customParams[0].w : 0.3;  }
float pBrightness()      { return customParams[1].x >= 0.0 ? customParams[1].x : 0.7;  }
float pContrast()        { return customParams[1].y >= 0.0 ? customParams[1].y : 0.9;  }
// slot 6 reserved
float pInnerGlowStr()    { return customParams[1].w >= 0.0 ? customParams[1].w : 0.4;  }
float pFillOpacity()     { return customParams[2].x >= 0.0 ? customParams[2].x : 0.85; }
float pBorderGlow()      { return customParams[2].y >= 0.0 ? customParams[2].y : 0.35; }
float pEdgeFadeStart()   { return customParams[2].z >= 0.0 ? customParams[2].z : 30.0; }
float pBorderBrightness(){ return customParams[2].w >= 0.0 ? customParams[2].w : 1.4;  }
float pAudioReactivity() { return customParams[3].x >= 0.0 ? customParams[3].x : 1.0;  }
float pParticleStrength(){ return customParams[3].y >= 0.0 ? customParams[3].y : 0.5;  }
float pDustDevilStr()    { return customParams[3].z >= 0.0 ? customParams[3].z : 0.4;  }
float pErosionStrength() { return customParams[3].w >= 0.0 ? customParams[3].w : 0.3;  }
float pLabelGlowSpread() { return customParams[4].x >= 0.0 ? customParams[4].x : 3.0;  }
float pLabelBrightness() { return customParams[4].y >= 0.0 ? customParams[4].y : 2.5;  }
float pLabelAudioReact() { return customParams[4].z >= 0.0 ? customParams[4].z : 1.0;  }
float pLabelChroma()     { return customParams[4].w >= 0.0 ? customParams[4].w : 0.5;  }
float pLogoScale()       { return customParams[5].x >= 0.0 ? customParams[5].x : 0.35; }
float pLogoIntensity()   { return customParams[5].y >= 0.0 ? customParams[5].y : 0.8;  }
float pLogoPulse()       { return customParams[5].z >= 0.0 ? customParams[5].z : 0.8;  }
float pLogoCount()       { return customParams[5].w >= 0.0 ? customParams[5].w : 3.0;  }
float pLogoSizeMin()     { return customParams[6].x >= 0.0 ? customParams[6].x : 0.4;  }
float pLogoSizeMax()     { return customParams[6].y >= 0.0 ? customParams[6].y : 1.0;  }
float pLogoSpin()        { return customParams[6].z >= 0.0 ? customParams[6].z : 0.3;  }
float pIdleStrength()    { return customParams[6].w >= 0.0 ? customParams[6].w : 0.6;  }
float pFlowCenterX()     { return customParams[7].x >= -1.5 ? customParams[7].x : 0.4; }
float pShowLabels()      { return customParams[7].y >= 0.0 ? customParams[7].y : 1.0;  }
float pFlowCenterY()     { return customParams[7].z >= -1.5 ? customParams[7].z : 0.5; }
float pSparkleIntensity(){ return customParams[7].w >= 0.0 ? customParams[7].w : 2.0;  }

// ─── Palette ───────────────────────────────────────────────────────

vec3 colPrimary()   { return colorWithFallback(customColors[0].rgb, vec3(0.102, 0.227, 0.290)); }
vec3 colSecondary() { return colorWithFallback(customColors[1].rgb, vec3(0.769, 0.584, 0.416)); }
vec3 colAccent()    { return colorWithFallback(customColors[2].rgb, vec3(0.208, 0.725, 0.671)); }
vec3 colGlow()      { return colorWithFallback(customColors[3].rgb, vec3(0.451, 0.729, 0.145)); }

// ─── Rotation matrix ───────────────────────────────────────────────

mat2 rot2(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c);
}

// ─── FBM ───────────────────────────────────────────────────────────

float fbm(in vec2 uv, int octaves, float rotAngle) {
    float value = 0.0;
    float amplitude = 0.5;
    float c = cos(rotAngle);
    float s = sin(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise2D(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.55;
    }
    return value;
}

// ─── Desert palette interpolation ──────────────────────────────────

vec3 tumbleweedPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else               return mix(accent, primary, (t - 0.66) * 3.0);
}

// ─── Wind field (curl-noise based) ─────────────────────────────────

vec2 windField(vec2 p, float t) {
    float ns = pNoiseScale();
    float fs = pFlowSpeed();
    float wdir = pWindDirection() * TAU;

    vec2 warp = vec2(
        noise2D(p * ns * 0.7 + vec2(t * fs * 0.3, 0.0)),
        noise2D(p * ns * 0.7 + vec2(0.0, t * fs * 0.3) + 50.0)
    ) * 0.4;

    vec2 q = p * ns + warp;
    float eps = 0.01;
    vec2 flow = t * fs * vec2(1.0, 0.2);
    float n1 = noise2D(q + vec2(0.0, eps) + flow);
    float n2 = noise2D(q - vec2(0.0, eps) + flow);
    float n3 = noise2D(q + vec2(eps, 0.0) + flow);
    float n4 = noise2D(q - vec2(eps, 0.0) + flow);

    vec2 curl = vec2(n1 - n2, -(n3 - n4)) / (2.0 * eps);
    vec2 bias = vec2(cos(wdir), sin(wdir)) * 1.5;
    return normalize(curl + bias) * (length(curl) * 0.5 + 0.5);
}

// ─── Sand particle system (3-layer parallax) ──────────────────────

float sandParticles(vec2 uv, float t, float bassEnv, float trebleEnv, vec2 flowDir) {
    float strength = pParticleStrength();
    if (strength <= 0.0) return 0.0;

    float acc = 0.0;
    for (int pi = 0; pi < 3; pi++) {
        float pScale = 6.0 + float(pi) * 3.0;
        float pSpd = 0.2 + float(pi) * 0.1 + bassEnv * 0.3;
        vec2 pUV = uv * pScale + flowDir * t * pSpd;
        vec2 pCell = floor(pUV);
        vec2 pFract = fract(pUV);
        vec2 pOffset = hash22(pCell) * 0.6 + 0.2;
        float pDist = length(pFract - pOffset);
        float pRadius = 0.10 - float(pi) * 0.02;
        float particle = smoothstep(pRadius, pRadius * 0.15, pDist);
        float twinkle = 0.5 + 0.5 * sin(hash21(pCell) * TAU + t * 2.5);
        twinkle *= 1.0 + trebleEnv * 2.0;
        acc += particle * twinkle;
    }

    return acc * strength * 0.3;
}

// ─── Wind-blown sand streams (signature horizontal flow) ─────────

float sandStreams(vec2 uv, float t, float bassEnv, float midsEnv, float trebleEnv, vec2 flowDir) {
    float acc = 0.0;
    float flowSpd = 0.6 + bassEnv * 0.8;

    // Project UV along and across the wind direction so streams
    // follow pWindDirection() instead of always flowing horizontally.
    vec2 flowNorm = normalize(flowDir);
    vec2 flowPerp = vec2(-flowNorm.y, flowNorm.x);
    float alongFlow = dot(uv, flowNorm);
    float acrossFlow = dot(uv, flowPerp);

    for (int si = 0; si < 5; si++) {
        float fi = float(si);
        float yBase = hash21(vec2(fi * 7.13, 3.91));
        float yPos = yBase + sin(t * (0.08 + fi * 0.03) + fi * 2.5) * 0.12;
        float yDist = abs(acrossFlow - yPos);

        // Stream width varies with bass
        float width = 0.015 + 0.01 * sin(t * 0.2 + fi) + bassEnv * 0.012;
        float stream = smoothstep(width, width * 0.1, yDist);

        // Internal noise makes it look like blowing sand, not a solid line
        float nx = alongFlow * (12.0 + fi * 3.0) - t * flowSpd * (1.0 + fi * 0.3);
        float sandNoise = noise2D(vec2(nx, acrossFlow * 20.0 + fi * 50.0));
        sandNoise = smoothstep(0.3, 0.7, sandNoise);

        // Density varies along length (gusts)
        float gust = sin(alongFlow * 4.0 - t * flowSpd * 0.5 + fi * 1.7) * 0.5 + 0.5;
        gust *= gust;

        // Treble adds bright sand grain glints inside the stream
        float glint = trebleEnv * smoothstep(0.7, 0.95, noise2D(vec2(nx * 2.0, t * 3.0 + fi * 11.0)));

        acc += stream * sandNoise * gust * (0.6 + glint * 1.5);
    }

    return acc * (0.15 + midsEnv * 0.1);
}

// ─── Dust devil vortices ───────────────────────────────────────────

float dustDevil(vec2 uv, vec2 center, float radius, float t,
                float bassEnv, float midsEnv, float trebleEnv) {
    vec2 delta = uv - center;
    float r = length(delta);
    float a = atan(delta.y, delta.x);

    float rotSpeed = 3.5 + bassEnv * 2.0;
    float spiral = sin(a * 4.0 - r * 18.0 / radius + t * rotSpeed) * 0.5 + 0.5;
    float falloff = smoothstep(radius, radius * 0.05, r);

    // Angular noise for organic shape — mids thicken the spiral arms
    float angNoise = noise2D(vec2(a * 2.0, t * 0.5)) * (0.35 + midsEnv * 0.2);
    angNoise += noise2D(vec2(a * 6.0, t * 0.8 + r * 10.0)) * 0.2;
    spiral = clamp(spiral + angNoise, 0.0, 1.0);

    // Bass drives intensity, mids warm the core, treble adds sparkle
    float intensity = 0.4 + bassEnv * 1.2 + midsEnv * 0.4 + trebleEnv * 0.5;
    return falloff * spiral * intensity;
}

float dustDevils(vec2 uv, float t, float bassEnv, float midsEnv, float trebleEnv) {
    float str = pDustDevilStr();
    if (str <= 0.0) return 0.0;

    float acc = 0.0;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float phase = fi * 2.094 + t * 0.35;
        vec2 center = vec2(0.25 + 0.4 * sin(phase * 0.7 + fi),
                           0.25 + 0.35 * cos(phase * 0.9 + fi * 1.5));
        float radius = 0.14 + 0.06 * sin(t * 0.5 + fi * 3.0) + bassEnv * 0.08;

        float life = sin(t * 0.4 + fi * 2.0) * 0.5 + 0.5;
        life = smoothstep(0.15, 0.5, life);

        acc += dustDevil(uv, center, radius, t, bassEnv, midsEnv, trebleEnv) * life;
    }

    return acc * str;
}

// ─── Erosion flow lines ────────────────────────────────────────────

float erosionLines(vec2 uv, float t, float bassEnv) {
    float str = pErosionStrength();
    if (str <= 0.0) return 0.0;

    float ns = pNoiseScale() * 0.8;
    float fs = pFlowSpeed() * 0.3;

    vec2 q = uv * ns;
    q.x *= 2.5;

    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; i++) {
        vec2 offset = vec2(t * fs * freq * 0.5 + bassEnv * 0.3, 0.0);
        val += amp * noise2D(q * freq + offset);
        freq *= 2.1;
        amp *= 0.45;
    }

    float lines = abs(sin(val * 12.0 + uv.x * 20.0));
    lines = smoothstep(0.90, 1.0, lines);

    return lines * str * (0.5 + bassEnv * 0.3);
}

// ─── Tumbleweed logo SDF ───────────────────────────────────────────

// ── Tumbleweed logo polygon data ──────────────────────────────
// Extracted from official openSUSE Tumbleweed SVG.  5 subpaths,
// even-odd fill rule.  Coordinates centered on origin, range ~[-0.5, 0.5].

const int TW_S0_N = 68;
const vec2 TW_S0[68] = vec2[68](
    vec2(-0.235115, -0.500000), vec2(-0.305518, -0.490653), vec2(-0.368836, -0.463968),
    vec2(-0.422506, -0.422510), vec2(-0.463964, -0.368841), vec2(-0.490648, -0.305524),
    vec2(-0.499995, -0.235120), vec2(-0.490332, -0.164695), vec2(-0.463399, -0.101307),
    vec2(-0.421754, -0.047513), vec2(-0.367957, -0.005873), vec2(-0.304566,  0.021058),
    vec2(-0.234139,  0.030720), vec2(-0.168416,  0.030877), vec2(-0.139292,  0.030933),
    vec2(-0.111948,  0.030996), vec2(-0.086934,  0.031054), vec2(-0.064780,  0.031105),
    vec2(-0.046018,  0.031148), vec2(-0.031179,  0.031182), vec2(-0.031130,  0.054426),
    vec2(-0.031059,  0.086836), vec2(-0.030976,  0.124712), vec2(-0.030887,  0.164355),
    vec2(-0.030801,  0.202065), vec2(-0.030725,  0.234144), vec2(-0.021064,  0.304571),
    vec2( 0.005867,  0.367962), vec2( 0.047507,  0.421759), vec2( 0.101301,  0.463403),
    vec2( 0.164690,  0.490337), vec2( 0.235115,  0.500000), vec2( 0.305518,  0.490653),
    vec2( 0.368836,  0.463969), vec2( 0.422506,  0.422511), vec2( 0.463964,  0.368841),
    vec2( 0.490648,  0.305524), vec2( 0.499995,  0.235120), vec2( 0.490332,  0.164695),
    vec2( 0.463399,  0.101307), vec2( 0.421755,  0.047513), vec2( 0.367957,  0.005873),
    vec2( 0.304566, -0.021058), vec2( 0.234139, -0.030720), vec2( 0.225452, -0.030740),
    vec2( 0.216387, -0.030761), vec2( 0.206731, -0.030783), vec2( 0.196274, -0.030806),
    vec2( 0.184805, -0.030832), vec2( 0.172114, -0.030860), vec2( 0.142368, -0.030928),
    vec2( 0.114300, -0.030991), vec2( 0.088529, -0.031048), vec2( 0.065674, -0.031098),
    vec2( 0.046356, -0.031141), vec2( 0.031195, -0.031174), vec2( 0.031146, -0.054418),
    vec2( 0.031076, -0.086827), vec2( 0.030992, -0.124703), vec2( 0.030903, -0.164346),
    vec2( 0.030816, -0.202057), vec2( 0.030741, -0.234136), vec2( 0.021078, -0.304564),
    vec2(-0.005855, -0.367956), vec2(-0.047499, -0.421754), vec2(-0.101296, -0.463399),
    vec2(-0.164687, -0.490332), vec2(-0.235114, -0.499996)
);

const int TW_S1_N = 15;
const vec2 TW_S1[15] = vec2[15](
    vec2(-0.235273, -0.437469), vec2(-0.226706, -0.437254), vec2(-0.218236, -0.436679),
    vec2(-0.209859, -0.435777), vec2(-0.209859, -0.210616), vec2(-0.435879, -0.210616),
    vec2(-0.436727, -0.218749), vec2(-0.437266, -0.226970), vec2(-0.437466, -0.235280),
    vec2(-0.430388, -0.289304), vec2(-0.410143, -0.337716), vec2(-0.378620, -0.378627),
    vec2(-0.337709, -0.410148), vec2(-0.289299, -0.430392), vec2(-0.235278, -0.437468)
);

const int TW_S2_N = 27;
const vec2 TW_S2[27] = vec2[27](
    vec2(-0.147328, -0.417393), vec2(-0.115082, -0.398026), vec2(-0.087079, -0.373229),
    vec2(-0.064034, -0.343716), vec2(-0.046661, -0.310200), vec2(-0.035674, -0.273392),
    vec2(-0.031788, -0.234006), vec2(-0.031713, -0.201998), vec2(-0.031628, -0.164387),
    vec2(-0.031540, -0.124844), vec2(-0.031457, -0.087041), vec2(-0.031385, -0.054650),
    vec2(-0.031333, -0.031341), vec2(-0.054640, -0.031393), vec2(-0.087028, -0.031465),
    vec2(-0.124827, -0.031548), vec2(-0.164365, -0.031635), vec2(-0.201971, -0.031720),
    vec2(-0.233975, -0.031795), vec2(-0.273497, -0.035709), vec2(-0.310420, -0.046773),
    vec2(-0.344022, -0.064265), vec2(-0.373586, -0.087463), vec2(-0.398389, -0.115644),
    vec2(-0.417714, -0.148086), vec2(-0.147320, -0.148086), vec2(-0.147320, -0.417393)
);

const int TW_S3_N = 27;
const vec2 TW_S3[27] = vec2[27](
    vec2( 0.031333,  0.031327), vec2( 0.054645,  0.031379), vec2( 0.087037,  0.031451),
    vec2( 0.124839,  0.031534), vec2( 0.164381,  0.031621), vec2( 0.201990,  0.031706),
    vec2( 0.233998,  0.031781), vec2( 0.274027,  0.035797), vec2( 0.311372,  0.047146),
    vec2( 0.345286,  0.065076), vec2( 0.375023,  0.088835), vec2( 0.399833,  0.117670),
    vec2( 0.418971,  0.150829), vec2( 0.149694,  0.150829), vec2( 0.149694,  0.418466),
    vec2( 0.116831,  0.399247), vec2( 0.088266,  0.374435), vec2( 0.064739,  0.344766),
    vec2( 0.046990,  0.310976), vec2( 0.035759,  0.273801), vec2( 0.031786,  0.233977),
    vec2( 0.031711,  0.201971), vec2( 0.031626,  0.164363), vec2( 0.031538,  0.124825),
    vec2( 0.031455,  0.087027), vec2( 0.031384,  0.054640), vec2( 0.031332,  0.031336)
);

const int TW_S4_N = 13;
const vec2 TW_S4[13] = vec2[13](
    vec2( 0.212213,  0.213324), vec2( 0.436225,  0.213324), vec2( 0.437133,  0.224217),
    vec2( 0.437464,  0.235266), vec2( 0.430386,  0.289290), vec2( 0.410141,  0.337702),
    vec2( 0.378618,  0.378613), vec2( 0.337707,  0.410135), vec2( 0.289296,  0.430378),
    vec2( 0.235275,  0.437455), vec2( 0.223659,  0.437080), vec2( 0.212216,  0.436059),
    vec2( 0.212216,  0.213320)
);

// ── Winding-number SDF with even-odd fill across all 5 subpaths ──

float sdTumbleweed(vec2 p) {
    vec2 dLo = vec2(-0.51, -0.51) - p;
    vec2 dHi = p - vec2(0.51, 0.51);
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) return sqrt(boxDist2);

    float d = 1e20;
    float s = 1.0;

    for (int i = 0, j = TW_S0_N - 1; i < TW_S0_N; j = i, i++) {
        vec2 e = TW_S0[j] - TW_S0[i];
        vec2 w = p - TW_S0[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= TW_S0[i].y, p.y < TW_S0[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }

    for (int i = 0, j = TW_S1_N - 1; i < TW_S1_N; j = i, i++) {
        vec2 e = TW_S1[j] - TW_S1[i];
        vec2 w = p - TW_S1[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= TW_S1[i].y, p.y < TW_S1[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }

    for (int i = 0, j = TW_S2_N - 1; i < TW_S2_N; j = i, i++) {
        vec2 e = TW_S2[j] - TW_S2[i];
        vec2 w = p - TW_S2[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= TW_S2[i].y, p.y < TW_S2[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }

    for (int i = 0, j = TW_S3_N - 1; i < TW_S3_N; j = i, i++) {
        vec2 e = TW_S3[j] - TW_S3[i];
        vec2 w = p - TW_S3[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= TW_S3[i].y, p.y < TW_S3[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }

    for (int i = 0, j = TW_S4_N - 1; i < TW_S4_N; j = i, i++) {
        vec2 e = TW_S4[j] - TW_S4[i];
        vec2 w = p - TW_S4[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= TW_S4[i].y, p.y < TW_S4[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }

    return s * sqrt(d);
}

// ─── Per-instance UV computation ───────────────────────────────────

const vec2 TW_LOGO_CENTER = vec2(0.0);

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse, float spinRate,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            timeSin(0.17) * 0.012 + timeSin(0.31) * 0.006,
            timeCos(0.23) * 0.010 + timeCos(0.13) * 0.005
        );
        uv -= drift;
        // Rolling rotation
        float rotAng = time * spinRate * 0.8;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + timeSin(0.8) * 0.015;
        float springT = fract(time * 1.5);
        float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + TW_LOGO_CENTER;
        return uv;
    }

    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.07 + float(idx) * 0.023;
    float f2 = 0.05 + float(idx) * 0.019;
    vec2 drift = vec2(
        timeSin(f1, h1 * TAU) * roam + timeSin(f1 * 2.3, h3 * TAU) * roam * 0.3,
        timeCos(f2, h2 * TAU) * roam * 0.9 + timeCos(f2 * 1.7, h4 * TAU) * roam * 0.25
    );
    uv -= drift;

    // Rolling rotation — tumbleweeds spin as they drift
    float rotAng = time * spinRate * (0.8 + h3 * 0.4) + float(idx) * 1.047;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + timeSin(0.6 + float(idx) * 0.13, h1 * TAU) * 0.015;
    float springT = fract(time * 1.5 + h2);
    float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + TW_LOGO_CENTER;
    return uv;
}

// ─── Per-zone rendering ────────────────────────────────────────────

vec4 renderTumbleweedZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                          vec4 params, bool isHighlighted,
                          float bass, float mids, float treble, float overall, bool hasAudio) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);
    float vitality     = zoneVitality(isHighlighted);
    float audioReact   = pAudioReactivity();

    float brightness   = pBrightness();
    float contrast     = pContrast();
    float innerGlowStr = pInnerGlowStr();
    float sparkleStr   = pSparkleIntensity();
    float logoIntensity = pLogoIntensity();
    float logoPulse    = pLogoPulse();
    float spinRate     = pLogoSpin();
    float logoScale    = pLogoScale();
    float sizeMin      = pLogoSizeMin();
    float sizeMax      = pLogoSizeMax();
    int   logoCount    = clamp(int(pLogoCount()), 1, 8);
    float fillOpacity  = pFillOpacity();

    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * timeSin(0.8 * PI)) * pIdleStrength();

    // Audio envelopes — responsive to moderate levels while still
    // smoothing out jitter via smoothstep shaping.
    float bassEnv   = hasAudio ? smoothstep(0.05, 0.30, bass) * audioReact : idlePulse;
    float midsEnv   = hasAudio ? smoothstep(0.05, 0.30, mids) * audioReact : idlePulse * 0.6;
    float trebleEnv = hasAudio ? smoothstep(0.06, 0.35, treble) * audioReact : 0.0;

    vec3 palPrimary   = colPrimary();
    vec3 palSecondary = colSecondary();
    vec3 palAccent    = colAccent();
    vec3 palGlow      = colGlow();

    float flowAngle = pWindDirection() * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    float aspect  = rectSize.x / max(rectSize.y, 1.0);

    float time = iTime;
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float gAspect = iResolution.x / max(iResolution.y, 1.0);

    vec4 result = vec4(0.0);

    // ── Zone fill ──

    if (d < 0.0) {
        vec2 uv = (fragCoord - rectPos) / max(rectSize, vec2(1.0));

        // ── Desert landscape background ─────────────────────────
        float flowSpeed = pFlowSpeed();
        float speed = pSpeed();

        // Horizon gradient with bass-reactive distortion
        float horizonDrift = timeSin(0.15) * 0.1 + bassEnv * 0.06;
        float horizonWave = sin(uv.x * 3.0 + time * 0.25) * 0.03
                          + sin(uv.x * 7.0 + time * 0.4) * 0.01 * (1.0 + bassEnv * 2.0);
        vec3 horizonSand = palSecondary * 0.9 + palAccent * 0.1;
        vec3 horizonMid  = palAccent * (0.25 + horizonDrift) + palPrimary * 0.55
                         + palSecondary * (0.2 - horizonDrift);
        vec3 horizonSky  = mix(palPrimary, palAccent * 0.3 + palPrimary * 0.7,
                               timeSin(0.1, uv.x * 2.0) * 0.15 + 0.15);
        float yShift = uv.y + horizonWave;
        vec3 col = mix(horizonSand, horizonMid, smoothstep(0.0, 0.45, yShift));
        col = mix(col, horizonSky, smoothstep(0.35, 1.0, yShift));
        col *= brightness;

        // FBM texture overlay — slow, subtle sand-dune ripples
        float ns = pNoiseScale();
        vec2 duneUV = uv * ns;
        duneUV.x *= 2.5;  // horizontal stretch for wind-carved feel
        vec2 duneFlow = flowDir * time * flowSpeed * 0.4;
        float q = fbm(duneUV + duneFlow, 4, 0.6);
        float r = fbm(duneUV + q * 1.2 + duneFlow * 0.7, 4, 0.6);

        // Warm sand texture
        float duneT = r * contrast + time * 0.04;
        vec3 duneColor = tumbleweedPalette(duneT, palSecondary, horizonMid, palAccent);
        float duneMix = 0.4 * smoothstep(1.2, 0.05, uv.y);
        col = mix(col, duneColor * brightness, duneMix);

        // Mids shift warmth
        col = mix(col, palSecondary * brightness, midsEnv * 0.3);

        // Wind field distortion (stronger with bass)
        vec2 wind = windField(uv, time);
        float windIntensity = length(wind) * (1.0 + bassEnv * 1.5);
        col += palAccent * windIntensity * 0.2;

        // Sand particles (3-layer parallax)
        float particles = sandParticles(uv, time, bassEnv, trebleEnv, flowDir);
        col += mix(palSecondary, vec3(1.0), 0.5) * particles;

        // Wind-blown sand streams (signature horizontal flow)
        float streams = sandStreams(uv, time, bassEnv, midsEnv, trebleEnv, flowDir);
        col += mix(palSecondary, vec3(1.0, 0.95, 0.85), 0.4) * streams;

        // Dust devils
        float devils = dustDevils(uv, time, bassEnv, midsEnv, trebleEnv);
        col += palSecondary * devils * 0.5 + palAccent * devils * 0.2;

        // Erosion flow lines
        float erosion = erosionLines(uv, time, bassEnv);
        col += palSecondary * erosion * 0.6;

        // Dust storm wave (wide, prominent, bass-reactive)
        float scanPos = fract(time * 0.07);
        float bandDist = abs(uv.y - scanPos);
        float bandWidth = 0.12 + bassEnv * 0.08;
        float bandAlpha = 0.25 + bassEnv * 0.35;
        float band = smoothstep(bandWidth, 0.0, bandDist) * bandAlpha;
        // Sand density spikes inside the storm band
        float stormNoise = noise2D(vec2(uv.x * 15.0 - time * 1.2, uv.y * 8.0)) * 0.5 + 0.5;
        band *= 0.6 + stormNoise * 0.4;
        col += mix(palSecondary, palAccent * 0.5, 0.3) * band;

        // Heat shimmer — mirage wobble near ground
        float shimmerWave = sin(uv.x * 25.0 + time * 1.2) * sin(uv.x * 11.0 - time * 0.7);
        float shimmerMask = smoothstep(0.3, 0.0, uv.y);
        float shimmer = shimmerWave * shimmerMask * 0.06 * (1.0 + bassEnv * 1.0);
        col += palSecondary * abs(shimmer);

        // Treble sparkle field — sand grains catching light across entire surface
        if (trebleEnv > 0.03) {
            float sparkN = noise2D(uv * 30.0 + time * 2.0);
            sparkN = smoothstep(0.65, 0.92, sparkN);
            col += vec3(1.0, 0.95, 0.85) * sparkN * trebleEnv * 0.8;
        }

        // ── Multi-instance logo rendering ─────────────────────────
        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, gAspect, time,
                                              logoScale, bassEnv, logoPulse, spinRate,
                                              sizeMin, sizeMax, instScale);

            // Bounding check
            if (iLogoUV.x < -0.7 || iLogoUV.x > 0.7 ||
                iLogoUV.y < -0.7 || iLogoUV.y > 0.7) continue;

            float maxScale = sizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            float logoD = sdTumbleweed(iLogoUV);

            if (logoD > 0.1) continue;

            // ── Light casting (warm glow in sand haze) ──────────
            float lightCast = exp(-max(logoD, 0.0) * 12.0) * 0.35;
            vec3 logoLight = tumbleweedPalette(time * 0.15 + iLogoUV.y + float(li) * 0.3,
                                                palGlow, palAccent, palSecondary);
            col += logoLight * lightCast * instIntensity * (1.0 + bassEnv * 0.8) * depthFactor;

            // ── Bass shockwave ring (prominent, expanding) ──────
            float iShockPhase = fract(time * 0.5 + float(li) * 0.137);
            float iShockStr = bassEnv * (1.0 - iShockPhase) * logoPulse;
            if (iShockStr > 0.01) {
                float iLogoDist = length(iLogoUV - TW_LOGO_CENTER);
                float iShockRadius = iShockPhase * 0.6;
                float shockDist = abs(iLogoDist - iShockRadius);
                float shockWidth = 0.04 + bassEnv * 0.03;
                float shockMask = smoothstep(shockWidth, 0.0, shockDist) * iShockStr;
                vec3 shockCol = tumbleweedPalette(iShockRadius * 3.0 + time * 0.3 + float(li),
                                                   palGlow, palAccent, palSecondary);
                col += shockCol * shockMask * 0.8 * depthFactor;
            }

            // ── Logo fill ───────────────────────────────────────
            if (logoD < 0.03) {
                if (logoD < 0.0) {
                    // Slow FBM energy inside — teal/green desert plant life
                    vec2 energyUV = iLogoUV * 3.0 + vec2(float(li) * 17.0);
                    float eq = fbm(energyUV + time * speed * 0.4, 3, 0.6);
                    float er = fbm(energyUV + eq * 1.2 + time * speed * 0.2, 3, 0.6);

                    // Stay in the warm accent/glow range — teal and green
                    float fillColorT = er * contrast + time * 0.02 + float(li) * 0.3;
                    vec3 fillCol = mix(palAccent, palGlow, fract(fillColorT) * 0.6 + 0.2);
                    fillCol *= brightness * 1.6;

                    // Bass pumps brighter
                    fillCol *= 1.0 + bassEnv * logoPulse * 1.2;

                    // Mids shift warmth visibly
                    fillCol = mix(fillCol, palSecondary * brightness * 1.5, midsEnv * 0.25);

                    // Treble flicker
                    if (trebleEnv > 0.08) {
                        float flickerN = noise2D(iLogoUV * 15.0 + time * 4.0);
                        if (flickerN > 0.6) {
                            fillCol = mix(fillCol, palGlow * brightness * 2.5, trebleEnv * 0.6);
                        }
                    }

                    // Fresnel-like edge brightening
                    float fresnelLike = smoothstep(-0.015, -0.001, logoD);
                    fillCol += palGlow * fresnelLike * 0.2 * (1.0 + midsEnv * 0.3);

                    float aa = smoothstep(0.0, -0.003, logoD);
                    col = mix(col, fillCol * instIntensity, aa);
                }

                // ── Multi-layer glow ────────────────────────────
                float glow1 = exp(-max(logoD, 0.0) * 60.0) * 0.6;
                float glow2 = exp(-max(logoD, 0.0) * 20.0) * 0.35;
                float glow3 = exp(-max(logoD, 0.0) * 6.0) * 0.18;
                vec3 edgeCol = tumbleweedPalette(time * 0.2 + iLogoUV.y + float(li) * 0.2,
                                                  palGlow, palAccent, palSecondary);
                float flare = 1.0 + bassEnv * 0.8;
                col += edgeCol * glow1 * flare * pParticleStrength() * 2.5 * depthFactor;
                col += palAccent * glow2 * flare * 0.7 * depthFactor;
                col += palGlow * glow3 * 0.5 * depthFactor;
            }

            // ── Treble edge sparks — windblown sand glints ───────
            if (trebleEnv > 0.05 && logoD > -0.005 && logoD < 0.02) {
                float sparkN = noise2D(iLogoUV * 25.0 + time * 2.5 + float(li) * 33.0);
                sparkN = smoothstep(0.6, 0.95, sparkN);
                float edgeMask = smoothstep(0.02, 0.0, abs(logoD));
                col += mix(palGlow, palSecondary, 0.3) * sparkN * edgeMask * trebleEnv
                       * sparkleStr * 0.6 * depthFactor;
            }

            // ── Sand trail behind logo ──────────────────────────
            {
                float h1 = hash21(vec2(float(li) * 7.31, 3.17));
                float f1 = 0.07 + float(li) * 0.023;
                float f2 = 0.05 + float(li) * 0.019;
                vec2 trailDir = normalize(vec2(
                    cos(time * f1 + h1 * TAU),
                    -sin(time * f2 + h1 * TAU)));
                vec2 trailUV = iLogoUV - TW_LOGO_CENTER;
                float trailDot = dot(normalize(trailUV + 0.001), -trailDir);
                float trailDist = length(trailUV);
                float trailLen = 0.35 + bassEnv * 0.2;
                float trail = smoothstep(0.0, 0.5, trailDot)
                            * smoothstep(trailLen, 0.05, trailDist)
                            * 0.08 * instIntensity * vitality;
                // Treble sparkle in trail — like sand catching light
                float trailSparkle = 1.0 + trebleEnv * noise2D(iLogoUV * 15.0 + time * 1.5) * 1.5;
                col += palSecondary * 0.5 * trail * trailSparkle;
            }
        } // end logo instance loop

        // ── Vitality ───────────────────────────────────────────
        if (isHighlighted) {
            col *= 1.1;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.15);
            col *= 0.8 + idlePulse * 0.1;
        }

        // ── Inner edge glow (iridescent) ────────────────────────
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, pEdgeFadeStart(), innerDist);
        col *= mix(0.6, 1.0, 1.0 - depthDarken * 0.35);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.05;
        vec3 iriCol = tumbleweedPalette(iriT, palSecondary, palAccent, palGlow);
        col += iriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.15);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border with FBM flow ──────────────────────────────────────

    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x) * 2.0;
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * 0.12, 3, 0.5);
        vec3 borderCol = tumbleweedPalette(borderFlow * contrast,
                                            palSecondary, palAccent, palGlow);
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= pBorderBrightness();

        if (isHighlighted) {
            float bBreathe = 0.85 + 0.15 * sin(time * 2.5);
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.8 : 1.0;
            borderCol *= bBreathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.3);
            borderCol *= 0.55;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // ── Outer glow with angular noise ──────────────────────────────

    float bassGlowPush = hasAudio ? bassEnv * 5.0 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 22.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && pBorderGlow() > 0.01) {
        float glow = expGlow(d, 7.0, pBorderGlow());
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.08);
        vec3 glowCol = tumbleweedPalette(glowT, palSecondary, palAccent, palGlow);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}

// ─── Custom Label Composite (wind-carved sandstone text) ─────────

vec4 compositeTumbleweedLabels(vec4 color, vec2 fragCoord,
                               float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colPrimary();
    vec3 palSecondary = colSecondary();
    vec3 palAccent    = colAccent();
    vec3 palGlow      = colGlow();

    float labelGlowSpread = pLabelGlowSpread();
    float labelBrightness = pLabelBrightness();
    float labelAudioReact = pLabelAudioReact();

    float bassR   = hasAudio ? bass * labelAudioReact   : 0.0;
    float midsR   = hasAudio ? mids * labelAudioReact   : 0.0;
    float trebleR = hasAudio ? treble * labelAudioReact : 0.0;

    // ── Two-layer halo: tight warm core + wide dust aura ────────
    float haloInner = 0.0;
    float haloOuter = 0.0;
    for (int i = 0; i < 16; i++) {
        float angle = float(i) * TAU / 16.0;
        for (int r = 1; r <= 4; r++) {
            float radius = float(r) * labelGlowSpread;
            vec2 offset = vec2(cos(angle), sin(angle)) * px * radius;
            float s = texture(uZoneLabels, uv + offset).a;
            if (r <= 2) haloInner += s * exp(-float(r) * 0.4);
            haloOuter += s * exp(-float(r) * 0.35);
        }
    }
    haloInner /= 24.0;
    haloOuter /= 48.0;

    if (haloOuter > 0.002) {
        float innerMask = haloInner * (1.0 - labels.a);
        float outerMask = haloOuter * (1.0 - labels.a);

        // Halo angle for directional effects
        float haloAngle = atan(uv.y - 0.5, uv.x - 0.5);

        // Warm dust-cloud color — slowly drifts between sandy tan and teal
        float dustPhase = sin(haloAngle * 1.5 + iTime * 0.3 + midsR * 0.8) * 0.5 + 0.5;
        vec3 haloCol = mix(palSecondary * 1.3, palAccent * 0.9, dustPhase * 0.4);

        // Inner halo: tight, warm, bright core hugging the text
        float innerBright = innerMask * (0.8 + bassR * 0.5);
        color.rgb += haloCol * innerBright * 1.2;

        // Outer halo: wide atmospheric dust aura
        vec3 outerCol = mix(haloCol, palSecondary * 0.6, 0.4);
        float outerBright = outerMask * (0.25 + bassR * 0.15);
        color.rgb += outerCol * outerBright * 0.8;

        // Wind-streaked dust lines through the halo — horizontal bias
        float windLine = sin(fragCoord.y * 0.4 + iTime * 0.3) * 0.5 + 0.5;
        windLine *= sin(fragCoord.y * 1.1 - iTime * 0.15 + fragCoord.x * 0.05) * 0.5 + 0.5;
        color.rgb += palSecondary * 0.5 * innerMask * windLine * (0.3 + bassR * 0.2);

        // Treble — sand grain sparkle drifts around the halo perimeter
        if (trebleR > 0.06) {
            float sparkN = noise2D(uv * 40.0 + iTime * 1.5);
            float spark = smoothstep(0.6, 0.9, sparkN) * trebleR * 2.0;
            float sparkDrift = fract(haloAngle / TAU + iTime * 0.2);
            float sparkBand = smoothstep(0.0, 0.1, sparkDrift) * smoothstep(0.3, 0.15, sparkDrift);
            color.rgb += vec3(1.0, 0.92, 0.78) * innerMask * spark * (0.5 + sparkBand * 0.8);
        }

        color.a = max(color.a, innerMask * 0.7);
    }

    // ── Text fill: layered desert sandstone ──────────────────────
    if (labels.a > 0.01) {
        // Erosion layers — horizontal strata like canyon walls
        // Stretched heavily in X (wind direction), compressed in Y (sediment layers)
        vec2 strataUV = fragCoord * vec2(0.008, 0.03);
        strataUV.x += iTime * 0.01;  // very slow horizontal drift
        float strata1 = noise2D(strataUV);
        float strata2 = noise2D(strataUV * 2.3 + 40.0);
        // Hard layer bands from quantized noise
        float layerBand = floor(strata1 * 5.0) / 4.0;  // 5 distinct strata
        float layerSoft = strata2;  // continuous variation within each layer

        // Per-layer color — warm palette cycles through sand/tan/teal
        vec3 layerCol = tumbleweedPalette(layerBand * 0.7 + iTime * 0.015,
                                           palSecondary, mix(palSecondary, palAccent, 0.3), palAccent);

        // Fine sand grain texture within each layer
        float grain = noise2D(fragCoord * 0.06 + vec2(iTime * 0.008, 0.0));
        float fineGrain = noise2D(fragCoord * 0.15);
        layerCol *= 0.8 + layerSoft * 0.3 + grain * 0.15;
        // Micro-texture: each grain catches light differently
        layerCol *= 0.92 + fineGrain * 0.16;

        // Edge detection — teal patina on stroke edges (oxidized copper in desert)
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float edgeStrength = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

        // Patina color flows along edges slowly
        float patinaFlow = noise2D(fragCoord * 0.02 + vec2(0.0, iTime * 0.04));
        vec3 patinaCol = mix(palAccent, palGlow, patinaFlow * 0.4);
        layerCol += patinaCol * edgeStrength * 0.6;

        // Wind-erosion highlight — a slow bright band sweeping horizontally
        float erosionPos = fract(iTime * 0.025);
        float erosionBand = smoothstep(0.06, 0.0, abs(fract(uv.y * 0.5 + 0.3) - erosionPos));
        layerCol += palSecondary * 0.3 * erosionBand;

        // Bass breathing — strata glow warmer
        layerCol *= 1.0 + bassR * 0.35;

        // Mids — shift layer warmth (sand vs teal emphasis)
        layerCol = mix(layerCol, palSecondary * 1.2, midsR * 0.08);

        // Treble — sand grain glints scattered across the surface
        if (trebleR > 0.06) {
            float glintN = noise2D(fragCoord * 0.08 + iTime * 0.8);
            float glint = smoothstep(0.75, 0.95, glintN) * trebleR;
            layerCol += mix(palGlow, vec3(1.0, 0.95, 0.85), 0.5) * glint * 1.5;
        }

        layerCol *= labelBrightness;

        // Tonemap (preserves warm saturation)
        layerCol = layerCol / (0.6 + layerCol);

        // Saturation boost post-tonemap
        float textLum = dot(layerCol, vec3(0.2126, 0.7152, 0.0722));
        layerCol = mix(vec3(textLum), layerCol, 1.4);
        layerCol = max(layerCol, vec3(0.0));

        color.rgb = mix(color.rgb, layerCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ─── Main ──────────────────────────────────────────────────────────

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

        vec4 zoneColor = renderTumbleweedZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (pShowLabels() > 0.5)
        color = compositeTumbleweedLabels(color, fragCoord, bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
