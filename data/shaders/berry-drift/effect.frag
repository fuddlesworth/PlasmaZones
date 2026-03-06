// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * BERRY DRIFT - Organic Boo-Berry Metaball Overlay
 *
 * Deep dark purple-berry background with organic floating metaball blobs
 * merged via smooth minimum. Soft bubblegum-pink and violet glow halos
 * bloom around blob boundaries. Mint-colored particle sparkles twinkle
 * gently across the field. Frosted, soothing, modern organic aesthetic.
 *
 * All pattern/animation uses fragCoord (screen space) so blobs flow
 * seamlessly across zone boundaries.
 *
 * Parameters (customParams):
 *   [0].x = speed          (0.06)  global animation speed
 *   [0].y = blobScale      (10)    blob count (3–16)
 *   [0].z = blobSoftness   (0.4)   smin k for blob merging
 *   [0].w = glowIntensity  (0.5)   bloom glow around blobs
 *   [1].x = fillOpacity    (0.90)  zone fill opacity
 *   [1].y = sparkleStr     (0.6)   sparkle brightness
 *   [1].z = sparkleSize    (1.0)   sparkle point size
 *   [1].w = audioSens      (1.0)   audio sensitivity
 *   [2].x = driftSpeed     (0.8)   blob drift speed
 *   [2].y = mintIntensity  (0.4)   mint sparkle color intensity
 *   [2].z = bloomWidth     (0.06)  glow falloff width
 *   [2].w = vignetteStr    (0.15)  edge darkening
 *   [3].x = blobSizeMin    (0.05)  minimum blob radius
 *   [3].y = blobSizeMax    (0.14)  maximum blob radius
 *
 * Colors (customColors):
 *   [0] = berryPink   #ff6b9d  primary pink
 *   [1] = mintGreen   #7fffd4  sparkle accent
 *   [2] = deepViolet  #6b21a8  blob secondary
 *   [3] = bubblegum   #ff9ecf  warm glow
 *   [4] = background  #150d20  deep purple-black
 *   [5] = lavender    #c4b5fd  soft accent
 *
 * Audio reactive:
 *   Bass: per-blob breathing (each pulses at own rhythm), organic wobble
 *         deformation (2-3 lobes distort circular shape), glow intensifies
 *   Mids: color temperature shift (blobs drift warmer/cooler)
 *   Treble: high-freq edge jitter (6-8 lobes), sparkle flash bursts
 *   Overall: lifts background warmth, widens glow
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// === BLOB FIELD HELPERS ===

// Polynomial smooth minimum for organic blob merging
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// Blob center position via Lissajous orbital path
// Each blob has a unique home position spread across the screen
vec2 blobCenter(int idx, float time, float drift) {
    float fi = float(idx);
    float seed = fi * 1.618;

    // Home position: spread across the screen (0.1–0.9 range)
    float homeX = 0.1 + hash11(seed + 30.0) * 0.8;
    float homeY = 0.1 + hash11(seed + 35.0) * 0.8;

    // Orbit frequencies: varied so blobs don't move in lockstep
    float freqX = 0.4 + hash11(seed) * 0.6;
    float freqY = 0.3 + hash11(seed + 5.0) * 0.5;
    float phaseX = hash11(seed + 10.0) * TAU;
    float phaseY = hash11(seed + 15.0) * TAU;

    // Orbit radii: how far each blob wanders from home
    float rx = 0.06 + hash11(seed + 20.0) * 0.12;
    float ry = 0.06 + hash11(seed + 25.0) * 0.12;

    return vec2(
        homeX + rx * sin(time * freqX * drift + phaseX),
        homeY + ry * cos(time * freqY * drift + phaseY)
    );
}

// Evaluate merged blob SDF and closest blob index at a screen UV point
// Returns: x = merged SDF distance, y = closest blob index (float)
// uv and aspect must be consistent: pass raw UV, aspect applied internally
vec2 blobField(vec2 uv, float time, float drift, float softness,
               int blobCount, float aspect, float sizeMin, float sizeMax,
               float bass, float treble, float rawTime) {
    float merged = 1e5;
    float closestIdx = 0.0;
    float closestDist = 1e5;

    for (int i = 0; i < 16; i++) {
        if (i >= blobCount) break;
        vec2 c = blobCenter(i, time, drift);
        vec2 diff = uv - c;
        diff.x *= aspect;

        float baseRadius = mix(sizeMin, sizeMax, hash11(float(i) * 3.7));

        // Per-blob breathing: each blob has its own pulse rhythm
        // Bass amplitude drives how much they breathe; stronger bass = deeper breaths
        float breathPhase = hash11(float(i) * 2.3) * TAU;
        float breathRate = 1.5 + hash11(float(i) * 4.1) * 2.0;
        float breath = sin(rawTime * breathRate + breathPhase) * 0.5 + 0.5;
        baseRadius *= 1.0 + breath * bass * 0.4;

        // Angular wobble: bass deforms shape (2-3 lobes = organic wobble),
        // treble adds high-freq edge jitter (6-8 lobes = nervous energy)
        float angle = atan(diff.y, diff.x);
        float lobes = 2.0 + floor(hash11(float(i) * 5.5) * 2.0);  // 2 or 3 lobes (integer → seamless at ±PI)
        float wobblePhase = hash11(float(i) * 6.1) * TAU;
        float wobble = sin(angle * lobes + rawTime * 1.2 + wobblePhase) * bass * 0.25;
        float jitter = sin(angle * (6.0 + float(i)) + rawTime * 4.0) * treble * 0.12;
        float radius = baseRadius * (1.0 + wobble + jitter);

        float dist = length(diff) - radius;
        if (dist < closestDist) {
            closestDist = dist;
            closestIdx = float(i);
        }
        merged = smin(merged, dist, softness);
    }
    return vec2(merged, closestIdx);
}


// === ZONE RENDER ===

vec4 renderBerryZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                     vec4 params, bool isHighlighted,
                     float bass, float mids, float treble, float overall, bool hasAudio)
{
    // Zone geometry
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.0);
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Parameters (sentinel pattern)
    float speed        = customParams[0].x >= 0.0 ? customParams[0].x : 0.06;
    float blobScale    = customParams[0].y >= 0.0 ? customParams[0].y : 10.0;
    float blobSoftness = customParams[0].z >= 0.0 ? customParams[0].z : 0.4;
    float glowIntensity= customParams[0].w >= 0.0 ? customParams[0].w : 0.5;
    float fillOpacity  = customParams[1].x >= 0.0 ? customParams[1].x : 0.90;
    float sparkleStr   = customParams[1].y >= 0.0 ? customParams[1].y : 0.6;
    float sparkleSize  = customParams[1].z >= 0.0 ? customParams[1].z : 1.0;
    float audioSens    = customParams[1].w >= 0.0 ? customParams[1].w : 1.0;
    float driftSpeed   = customParams[2].x >= 0.0 ? customParams[2].x : 0.8;
    float mintIntensity= customParams[2].y >= 0.0 ? customParams[2].y : 0.4;
    float bloomWidth   = customParams[2].z >= 0.0 ? customParams[2].z : 0.06;
    float vignetteStr  = customParams[2].w >= 0.0 ? customParams[2].w : 0.15;
    float blobSizeMin  = customParams[3].x >= 0.0 ? customParams[3].x : 0.05;
    float blobSizeMax  = customParams[3].y >= 0.0 ? customParams[3].y : 0.14;
    float sparkleGridDensity = customParams[4].y >= 0.0 ? customParams[4].y : 8.0;
    float rimGlowWidth = customParams[4].z >= 0.0 ? customParams[4].z : 0.025;

    // Colors (fallback pattern)
    vec3 berryPink  = colorWithFallback(customColors[0].rgb, vec3(1.0, 0.42, 0.616));
    vec3 mintGreen  = colorWithFallback(customColors[1].rgb, vec3(0.498, 1.0, 0.831));
    vec3 deepViolet = colorWithFallback(customColors[2].rgb, vec3(0.42, 0.13, 0.659));
    vec3 bubblegum  = colorWithFallback(customColors[3].rgb, vec3(1.0, 0.62, 0.812));
    vec3 bgColor    = colorWithFallback(customColors[4].rgb, vec3(0.082, 0.051, 0.125));
    vec3 lavender   = colorWithFallback(customColors[5].rgb, vec3(0.769, 0.71, 0.992));

    // Audio
    float energy = hasAudio ? overall * audioSens : 0.0;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * 0.3;
    float bassAmt = hasAudio ? bass * audioSens : idlePulse;
    float midsAmt = hasAudio ? mids * audioSens : idlePulse * 0.5;
    float trebleAmt = hasAudio ? treble * audioSens : idlePulse * 0.3;
    float vitality = zoneVitality(isHighlighted);

    float time = iTime * speed;

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
        float aspect = iResolution.x / max(iResolution.y, 1.0);

        // === LAYER 1: Nebula Background ===
        // Warped multi-octave noise clouds — not flat
        {
            vec2 nebUV = globalUV * 2.5;
            float warpStr = 0.4 + energy * 0.3 + idlePulse * 0.15;
            float n1 = noise2D(nebUV + iTime * 0.08);
            float n2 = noise2D(nebUV * 1.6 - iTime * 0.06 + 50.0);
            nebUV += vec2(n1, n2) * warpStr;

            float neb = 0.0;
            float amp = 0.5;
            vec2 octUV = nebUV;
            for (int o = 0; o < 4; o++) {
                neb += noise2D(octUV) * amp;
                octUV *= 2.1;
                amp *= 0.5;
            }

            // Color: cycle through deep purple and violet tones
            float nebPhase = neb + iTime * 0.04;
            vec3 nebColor = mix(bgColor, deepViolet * 0.5, smoothstep(0.3, 0.7, fract(nebPhase)));
            nebColor = mix(nebColor, bubblegum * 0.15, smoothstep(0.6, 0.9, neb));
            nebColor += bgColor * 0.5;
            result.rgb = nebColor * (0.6 + neb * 0.4);
        }

        // === LAYER 2: Organic Blob Field ===
        float blobTime = iTime * driftSpeed;
        float softK = max(blobSoftness * 0.05, 0.001);
        int blobCount = clamp(int(blobScale), 3, 16);

        // Mids-driven color warmth: applied per-blob below
        float midsWarmth = midsAmt * 0.12;

        vec2 field = blobField(globalUV, blobTime, 1.0, softK,
                               blobCount, aspect, blobSizeMin, blobSizeMax,
                               bassAmt, trebleAmt, iTime);
        float blobDist = field.x;
        float closestIdx = field.y;

        // Inside blobs: flowing internal texture
        if (blobDist < 0.0) {
            float t = fract(closestIdx * 0.618);

            // Animated internal noise — swirling patterns inside the blob
            vec2 internalUV = globalUV * 6.0;
            float flow1 = noise2D(internalUV + iTime * 0.15 + closestIdx * 10.0);
            float flow2 = noise2D(internalUV * 1.5 - iTime * 0.12 + 30.0 + closestIdx * 7.0);
            float internalPattern = flow1 * 0.6 + flow2 * 0.4;

            // Base color alternation + noise-driven color shifts
            vec3 blobColor = mix(berryPink, deepViolet, smoothstep(0.3, 0.7, t));
            blobColor = mix(blobColor, bubblegum, smoothstep(0.4, 0.8, internalPattern) * 0.5);
            blobColor = mix(blobColor, lavender * 0.8, smoothstep(0.7, 0.95, internalPattern) * 0.3);

            // Mids shift color temperature: positive warmth pushes toward bubblegum/pink,
            // modulated per-blob so each drifts slightly differently
            float blobWarmthBias = hash11(closestIdx * 1.9 + 42.0) * 0.6 + 0.7; // 0.7–1.3
            blobColor = mix(blobColor, bubblegum, midsWarmth * blobWarmthBias);

            // Center-to-edge gradient: center is deeper/richer, edges brighter
            float depth = clamp(-blobDist / 0.06, 0.0, 1.0);
            blobColor = mix(blobColor * 1.2, blobColor * 0.6 + deepViolet * 0.3, depth);

            // Internal luminosity variation from noise
            float luma = 0.7 + internalPattern * 0.3;
            blobColor *= luma;

            // Audio brightens interiors
            blobColor += berryPink * energy * 0.15;

            blobColor *= vitalityScale(0.7, 1.2, vitality);
            blobColor += lavender * vitalityScale(0.0, 0.08, vitality);

            result.rgb = mix(result.rgb, blobColor, 0.9);
        }

        // Fresnel rim: wide inner glow at blob edges (not just a thin line)
        {
            float rim = smoothstep(-rimGlowWidth, 0.0, blobDist)
                      * (1.0 - smoothstep(0.0, rimGlowWidth * 0.5, blobDist));
            // Also add thin bright edge
            float thinEdge = exp(-abs(blobDist) / 0.002);
            float rimTotal = rim * 0.8 + thinEdge * 0.5;
            rimTotal *= 0.8 + trebleAmt * 0.4;

            vec3 rimColor = mix(bubblegum, lavender, 0.4 + 0.3 * sin(iTime * 1.5));
            {
                float shimmer = 0.7 + 0.3 * sin(iTime * 4.0 + globalUV.x * 15.0 + globalUV.y * 10.0);
                rimTotal *= vitalityScale(0.5, shimmer * 1.3, vitality);
                rimColor = mix(rimColor, vec3(1.0), vitalityScale(0.0, 0.15, vitality));
            }
            result.rgb += rimColor * rimTotal;
        }

        // === LAYER 3: Glow Halos ===
        // Wide two-tone glow outside blobs
        if (blobDist > 0.0) {
            float glowFalloff = bloomWidth * (1.0 + energy * 0.5);
            // Inner glow: bubblegum, tight
            float innerGlow = exp(-blobDist / glowFalloff) * glowIntensity;
            // Outer glow: deepViolet, wide
            float outerGlow = exp(-blobDist / (glowFalloff * 3.0)) * glowIntensity * 0.4;
            innerGlow *= 1.0 + bassAmt * 0.6;
            outerGlow *= 1.0 + bassAmt * 0.3;

            vec3 innerColor = bubblegum;
            vec3 outerColor = mix(deepViolet, berryPink, 0.3);
            innerGlow *= vitalityScale(0.5, 1.5, vitality);
            outerGlow *= vitalityScale(0.5, 1.3, vitality);
            innerColor = mix(innerColor, berryPink, vitalityScale(0.0, 0.3, vitality));
            result.rgb += innerColor * innerGlow + outerColor * outerGlow;
            result.a = max(result.a, innerGlow * 0.6);
        }

        // === LAYER 4: Mint Sparkles ===
        // Bigger, brighter, with 4-point star shape
        {
            for (int sy = -2; sy <= 2; sy++) {
                for (int sx = -2; sx <= 2; sx++) {
                    vec2 cell = floor(globalUV * sparkleGridDensity) + vec2(float(sx), float(sy));
                    vec2 cellHash = hash22(cell);
                    vec2 sparklePos = (cell + cellHash) / sparkleGridDensity;

                    vec2 diff = globalUV - sparklePos;
                    diff.x *= aspect;
                    float sDist = length(diff);

                    // Point core
                    float pointSize = 0.006 * sparkleSize;
                    float sparkle = exp(-sDist * sDist / (pointSize * pointSize));

                    // 4-point star rays
                    float angle = atan(diff.y, diff.x);
                    float star = pow(abs(cos(angle * 2.0)), 8.0);
                    float starRay = exp(-sDist / (pointSize * 4.0)) * star * 0.6;
                    sparkle += starRay;

                    // Twinkling
                    float phase = hash21(cell + 100.0) * TAU;
                    float twinkleSpeed = 1.5 + hash21(cell + 200.0) * 2.5;
                    float twinkle = 0.5 + 0.5 * sin(iTime * twinkleSpeed + phase);
                    twinkle = pow(twinkle, 2.0);

                    // Treble flash
                    twinkle += trebleAmt * 0.4 * step(0.65, hash21(cell + 300.0));

                    // Brighter near blob edges
                    float nearEdge = exp(-abs(blobDist) / 0.04);
                    twinkle *= 1.0 + nearEdge;

                    float intensity = sparkle * twinkle * sparkleStr;
                    intensity *= vitalityScale(0.5, 1.5, vitality);

                    // Mint with white-hot core
                    vec3 sColor = mix(mintGreen * mintIntensity + lavender * (1.0 - mintIntensity),
                                      vec3(1.0), sparkle * 0.5);
                    result.rgb += sColor * intensity;
                }
            }
        }

        // === LAYER 5: Ambient + Vignette ===
        {
            float centerDist = length(globalUV - 0.5);
            // Atmospheric radial glow
            vec3 ambGlow = mix(lavender * 0.5, deepViolet * 0.3, centerDist);
            ambGlow *= exp(-centerDist * 1.5) * 0.08;
            result.rgb += ambGlow;

            // Soft flowing ambient noise overlay
            float ambNoise = noise2D(globalUV * 4.0 + iTime * 0.1);
            result.rgb += deepViolet * ambNoise * 0.02;

            // Vignette
            float vignette = smoothstep(0.3, 0.85, centerDist) * vignetteStr;
            result.rgb *= 1.0 - vignette;
        }

        // Dormant desaturation
        result.rgb = vitalityDesaturate(result.rgb, vitality);
        result.a = fillOpacity;
    }

    // === Border ===
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 borderClr = colorWithFallback(borderColor.rgb, berryPink);
        float flow = 0.5 + 0.5 * sin(atan(p.y, p.x) * 4.0 + iTime * 1.5 + energy * 2.0);
        borderClr = mix(borderClr, lavender, flow * 0.25);

        {
            float pulse = 0.8 + 0.2 * sin(iTime * vitalityScale(1.5, 3.0, vitality));
            borderClr = mix(borderClr, berryPink, vitalityScale(0.1, 0.5, vitality) * pulse);
            borderClr *= vitalityScale(0.8, 1.1, vitality);
            borderClr = vitalityDesaturate(borderClr, vitality);
        }

        if (hasAudio && bass > 0.4) {
            float bassFlash = (bass - 0.4) * audioSens;
            borderClr = mix(borderClr, vec3(1.0), bassFlash * 0.2);
        }

        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }

    // === Outer glow (both states, vitality-modulated) ===
    if (d > 0.0) {
        float glowR = vitalityScale(12.0, 24.0, vitality) + (hasAudio ? bass * audioSens * vitalityScale(6.0, 16.0, vitality) : idlePulse * vitalityScale(3.0, 8.0, vitality));
        float breathe = 1.0 + vitalityScale(0.05, 0.15, vitality) * sin(iTime * 2.0);
        glowR *= breathe;
        if (d < glowR) {
            float glowFalloff = vitalityScale(4.0, 8.0, vitality);
            float glowAmt = vitalityScale(0.15, 0.45, vitality) * (1.0 + energy * 0.4);
            float glow = expGlow(d, glowFalloff, glowAmt);
            vec3 glowColor = vitalityDesaturate(bubblegum, vitality);
            result.rgb += glowColor * glow * vitalityScale(0.15, 0.35, vitality);
            result.a = max(result.a, glow * vitalityScale(0.2, 0.5, vitality));
        }
    }

    return result;
}


// Custom label composite — frosted glass with mint sparkle burst
vec4 compositeBerryLabels(vec4 color, vec2 fragCoord,
                          float bass, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float labelGlowSpread = customParams[3].z >= 0.0 ? customParams[3].z : 3.0;
    float labelBrightness = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;
    float labelAudioReact = customParams[4].x >= 0.0 ? customParams[4].x : 1.0;

    vec3 bPink  = colorWithFallback(customColors[0].rgb, vec3(1.0, 0.42, 0.616));
    vec3 mint   = colorWithFallback(customColors[1].rgb, vec3(0.498, 1.0, 0.831));
    vec3 lavndr = colorWithFallback(customColors[5].rgb, vec3(0.769, 0.71, 0.992));
    vec3 bubble = colorWithFallback(customColors[3].rgb, vec3(1.0, 0.62, 0.812));

    // Gaussian-weighted halo — soft organic bleed
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * labelGlowSpread).a * w;
        }
    }
    halo /= 16.5;

    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);
        vec3 haloColor = mix(bubble, lavndr, 0.6 + 0.4 * sin(iTime * 1.2));
        float haloBright = haloEdge * (0.5 + (hasAudio ? bass * 0.5 * labelAudioReact : 0.0));
        color.rgb += haloColor * haloBright;

        // Mint sparkle burst on treble
        if (hasAudio && treble > 0.15) {
            float angle = atan(uv.y - 0.5, uv.x - 0.5);
            float sparkle = pow(max(sin(angle * 6.0 + iTime * 8.0), 0.0), 8.0) * treble * labelAudioReact;
            color.rgb += mint * haloEdge * sparkle * 0.6;
        }
    }

    // Frosted glass core — brighten, tint, and slightly whiten
    if (labels.a > 0.01) {
        vec3 frosted = color.rgb * labelBrightness + bPink * 0.25 + lavndr * 0.1;
        frosted = mix(frosted, vec3(1.0), 0.12);
        color.rgb = mix(color.rgb, frosted, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}


// === MAIN ===

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    bool hasAudio = iAudioSpectrumSize > 0;
    float bass = getBassSoft();
    float mids = getMidsSoft();
    float treble = getTrebleSoft();
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderBerryZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositeBerryLabels(color, fragCoord, bass, treble, hasAudio);
    fragColor = clampFragColor(color);
}
