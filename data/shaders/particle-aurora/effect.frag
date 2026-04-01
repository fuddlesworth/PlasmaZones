// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>
#include <particles.glsl>

/*
 * PARTICLE AURORA — Fragment Shader (Image Pass)
 *
 * Composites compute-generated particle texture over rich atmospheric
 * aurora backgrounds with flowing curtains, bloom, ribbon trails,
 * atmospheric fog, and full audio reactivity.
 *
 * Parameters:
 *   customParams[0].x = spawnRate
 *   customParams[0].y = maxLifetime
 *   customParams[0].z = gravity
 *   customParams[0].w = turbulence
 *   customParams[1].x = particleSize
 *   customParams[1].y = glowIntensity
 *   customParams[1].z = audioReactivity
 *   customParams[1].w = driftSpeed
 *   customParams[2].x = showLabels
 *   customParams[2].y = labelSpread
 *   customParams[2].z = labelBrightness
 *   customParams[2].w = labelAudioReact
 *   customParams[3].x = curtainIntensity
 *   customParams[3].y = ribbonIntensity
 *   customParams[3].z = fogDensity
 *   customParams[3].w = bloomStrength
 *   customParams[4].x = borderEnergy
 *   customParams[4].y = mouseHaloRadius
 *   customParams[4].z = vignetteStrength
 *   customParams[4].w = colorTemperature
 *   customColors[0]   — Primary background (deep violet)
 *   customColors[1]   — Aurora green
 *   customColors[2]   — Aurora blue/cyan
 *   customColors[3]   — Aurora pink/magenta
 */

// ═══════════════════════════════════════════════════════════════════════
// FBM noise with domain warping for aurora curtains
// ═══════════════════════════════════════════════════════════════════════

float fbm(vec2 p, int octaves) {
    float f = 0.0;
    float amp = 0.5;
    mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < octaves; i++) {
        f += amp * noise2D(p);
        p = rot * p * 2.0;
        amp *= 0.5;
    }
    return f;
}

// Domain-warped FBM for flowing organic aurora shapes
float warpedFbm(vec2 p, float warpAmount, int octaves, float t) {
    vec2 q = vec2(
        fbm(p + vec2(0.0, 0.0) + t * 0.05, octaves),
        fbm(p + vec2(5.2, 1.3) - t * 0.04, octaves)
    );
    vec2 r = vec2(
        fbm(p + 4.0 * q + vec2(1.7, 9.2) + t * 0.07, octaves),
        fbm(p + 4.0 * q + vec2(8.3, 2.8) - t * 0.06, octaves)
    );
    return fbm(p + warpAmount * 4.0 * r, octaves);
}

// Ridged noise for aurora ribbon/curtain shapes
float ridgedNoise(vec2 p, int octaves, float t) {
    float w = warpedFbm(p, 0.8, octaves, t);
    float ridge = 1.0 - abs(w * 2.0 - 1.0);
    return ridge * ridge;
}

// ═══════════════════════════════════════════════════════════════════════
// Aurora curtain background: domain-warped vertical bands
// ═══════════════════════════════════════════════════════════════════════

vec3 auroraCurtains(vec2 uv, float t, float intensity, float bassPulse,
                    vec3 col1, vec3 col2, vec3 col3) {
    if (intensity <= 0.0) return vec3(0.0);

    // Vertical curtain bands using domain-warped noise
    vec2 curtainUV = vec2(uv.x * 3.0, uv.y * 1.5);
    float warp1 = fbm(curtainUV * 0.8 + vec2(t * 0.02, 0.0), 4);
    float warp2 = fbm(curtainUV * 0.6 + vec2(5.7, t * 0.015), 4);

    // Primary curtain: tall vertical bands that sway
    vec2 warped = curtainUV + vec2(warp1 * 1.2, warp2 * 0.4);
    float curtain1 = fbm(warped + vec2(t * 0.03, 0.0), 5);
    curtain1 = smoothstep(0.3, 0.7, curtain1);

    // Secondary curtain: offset, different frequency
    vec2 warped2 = curtainUV * 1.3 + vec2(warp2 * 0.9 + 3.0, warp1 * 0.3);
    float curtain2 = fbm(warped2 + vec2(-t * 0.025, t * 0.01), 5);
    curtain2 = smoothstep(0.35, 0.75, curtain2);

    // Vertical emphasis: curtains are brighter in upper portion
    float vertGrad = smoothstep(0.0, 0.8, uv.y);

    // Color assignment: each curtain layer gets different aurora hues
    float colorShift = sin(uv.x * 4.0 + t * 0.1) * 0.5 + 0.5;
    vec3 c1 = mix(col1, col2, colorShift) * curtain1 * vertGrad;
    vec3 c2 = mix(col2, col3, 1.0 - colorShift) * curtain2 * vertGrad * 0.7;

    // Bass pulses curtain brightness and width
    float bassBoost = 1.0 + bassPulse * 0.6;

    return (c1 + c2) * intensity * bassBoost * 0.35;
}

// ═══════════════════════════════════════════════════════════════════════
// Aurora ribbon trails: ridged noise curtains independent of particles
// ═══════════════════════════════════════════════════════════════════════

vec3 auroraRibbons(vec2 uv, float t, float intensity, float bass, float treble,
                   vec3 col1, vec3 col2, vec3 col3) {
    if (intensity <= 0.0) return vec3(0.0);

    // Primary ribbon: sweeping horizontal curtain shape
    vec2 ribbonUV = vec2(uv.x * 2.5, uv.y * 4.0);
    float ribbon1 = ridgedNoise(ribbonUV + vec2(t * 0.08, t * 0.02), 4, t);

    // Secondary ribbon: slower, different scale
    vec2 ribbonUV2 = vec2(uv.x * 1.8 + 2.0, uv.y * 3.0);
    float ribbon2 = ridgedNoise(ribbonUV2 + vec2(-t * 0.05, t * 0.03), 4, t * 0.7);

    // Tertiary: fast thin wisps
    vec2 ribbonUV3 = vec2(uv.x * 4.0, uv.y * 6.0);
    float ribbon3 = ridgedNoise(ribbonUV3 + vec2(t * 0.12, -t * 0.04), 3, t * 1.3);
    ribbon3 *= ribbon3; // sharper

    // Bass widens ribbons (softens the ridge threshold)
    float bassWiden = 1.0 + bass * 0.4;
    ribbon1 = pow(ribbon1, max(1.0 / bassWiden, 0.5));

    // Treble adds shimmer to thin wisps
    float shimmer = 1.0 + treble * 2.0 * sin(uv.x * 30.0 + t * 8.0) * 0.5;
    ribbon3 *= shimmer;

    // Color: sweep through aurora palette based on position
    float hue = fract(uv.x * 0.5 + t * 0.02);
    vec3 rc1 = mix(col1, col2, smoothstep(0.0, 0.5, hue));
    vec3 rc2 = mix(col2, col3, smoothstep(0.3, 0.8, hue));
    vec3 rc3 = mix(col3, col1, smoothstep(0.5, 1.0, hue));

    vec3 result = rc1 * ribbon1 * 0.5
                + rc2 * ribbon2 * 0.35
                + rc3 * ribbon3 * 0.25;

    return result * intensity;
}

// ═══════════════════════════════════════════════════════════════════════
// Atmospheric fog/mist at zone bottom
// ═══════════════════════════════════════════════════════════════════════

vec3 atmosphericFog(vec2 uv, float t, float density, vec3 fogTint) {
    if (density <= 0.0) return vec3(0.0);

    // Base gradient: thick at bottom, fading upward
    float gradient = smoothstep(0.4, 0.0, uv.y);

    // Noise perturbation: rolling mist
    vec2 fogUV = uv * vec2(3.0, 2.0);
    float mist1 = fbm(fogUV + vec2(t * 0.03, t * 0.02), 4);
    float mist2 = fbm(fogUV * 1.5 + vec2(-t * 0.025, t * 0.035), 3);

    float fog = gradient * (mist1 * 0.6 + mist2 * 0.4);

    // Wispy tendrils rising from the mist
    float tendril = fbm(vec2(uv.x * 5.0, uv.y * 8.0 - t * 0.1), 5);
    tendril = smoothstep(0.5, 0.7, tendril) * smoothstep(0.5, 0.1, uv.y);

    return fogTint * (fog + tendril * 0.3) * density;
}

// ═══════════════════════════════════════════════════════════════════════
// Particle bloom: cheap multi-sample blur of particle texture
// ═══════════════════════════════════════════════════════════════════════

vec3 particleBloom(vec2 uv, float strength) {
    if (strength <= 0.0) return vec3(0.0);

    vec2 texel = 1.0 / max(iResolution, vec2(1.0));
    float bloomRadius = 3.0 * strength;
    vec3 bloom = vec3(0.0);
    float total = 0.0;

    // 13-tap blur kernel (cross + diagonals)
    const int TAPS = 13;
    vec2 offsets[13] = vec2[](
        vec2( 0.0,  0.0),
        vec2( 1.0,  0.0), vec2(-1.0,  0.0),
        vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 1.0,  1.0), vec2(-1.0,  1.0),
        vec2( 1.0, -1.0), vec2(-1.0, -1.0),
        vec2( 2.0,  0.0), vec2(-2.0,  0.0),
        vec2( 0.0,  2.0), vec2( 0.0, -2.0)
    );
    float weights[13] = float[](
        1.0,
        0.7, 0.7,
        0.7, 0.7,
        0.4, 0.4,
        0.4, 0.4,
        0.25, 0.25,
        0.25, 0.25
    );

    for (int i = 0; i < TAPS; i++) {
        vec2 sampleUV = uv + offsets[i] * texel * bloomRadius;
        vec4 s = texture(uParticleTexture, sampleUV);
        bloom += s.rgb * weights[i];
        total += weights[i];
    }

    return bloom / total;
}

// ═══════════════════════════════════════════════════════════════════════
// Mouse aurora halo
// ═══════════════════════════════════════════════════════════════════════

vec3 mouseHalo(vec2 fragCoord, float radius, float t, vec3 col1, vec3 col2) {
    vec2 mousePos = iMouse.xy;
    if (mousePos.x < 0.0 || mousePos.y < 0.0) return vec3(0.0);
    if (radius <= 0.0) return vec3(0.0);

    float d = length(fragCoord - mousePos);
    float haloRadius = radius * pxScale();
    if (d > haloRadius * 1.5) return vec3(0.0);

    // Soft ring
    float ring = exp(-pow((d - haloRadius * 0.4) / (haloRadius * 0.15), 2.0));
    // Inner glow
    float inner = exp(-d / (haloRadius * 0.3)) * 0.5;
    // Outer haze
    float outer = exp(-d / (haloRadius * 0.8)) * 0.2;

    // Animated color rotation
    float angle = atan(fragCoord.y - mousePos.y, fragCoord.x - mousePos.x);
    float colorT = sin(angle * 3.0 + t * 2.0) * 0.5 + 0.5;
    vec3 haloCol = mix(col1, col2, colorT);

    return haloCol * (ring * 0.6 + inner + outer);
}

// ═══════════════════════════════════════════════════════════════════════
// Zone rendering
// ═══════════════════════════════════════════════════════════════════════

vec4 renderAuroraZone(
    vec2 fragCoord, vec2 texCoord, vec4 rect, vec4 fillCol, vec4 borderCol,
    vec4 params, bool isHighlighted,
    vec4 particleSample, vec3 bloomSample,
    float bass, float mids, float treble, float energy, bool hasAudio,
    vec3 primary, vec3 aurora1, vec3 aurora2, vec3 aurora3,
    float glowIntensity, float curtainIntensity, float ribbonIntensity,
    float fogDensity, float bloomStrength, float borderEnergy,
    float mouseHaloRadius, float audioReact
) {
    float px = pxScale();
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;

    float borderRadius = max(params.x, 6.0) * px;
    float borderWidth = max(params.y, 2.5) * px;
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Early-out for fragments far outside the zone
    if (d > borderWidth * 2.0 + 60.0 * px) return vec4(0.0);

    float vitality = zoneVitality(isHighlighted);
    float t = iTime;

    // Audio modulation
    float audioPulse = bass * audioReact * vitality;
    float audioMid = mids * audioReact * vitality;
    float audioShimmer = treble * audioReact * vitality;

    vec4 result = vec4(0.0);
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    if (d < 0.0) {
        // ── Rich atmospheric background ─────────────────────────────
        // Base: dark primary with radial gradient for depth
        float vignette = 1.0 - length(localUV - 0.5) * 0.3;
        vec3 bg = primary * 0.8 * vignette + fillCol.rgb * 0.1;

        // Aurora curtain bands in background
        vec3 curtains = auroraCurtains(localUV, t, curtainIntensity * vitality,
                                       audioPulse, aurora1, aurora2, aurora3);
        // Bass pulses the background curtains
        curtains *= 1.0 + audioPulse * 0.5;
        bg += curtains;

        // Aurora ribbon trails
        vec3 ribbons = auroraRibbons(localUV, t, ribbonIntensity * vitality,
                                     bass * audioReact, treble * audioReact,
                                     aurora1, aurora2, aurora3);
        // Mids affect ribbon brightness
        ribbons *= 1.0 + audioMid * 0.4;
        bg += ribbons;

        // Atmospheric fog at bottom
        vec3 fogCol = mix(primary, aurora1, 0.2) * 0.5;
        bg += atmosphericFog(localUV, t, fogDensity * vitality, fogCol);

        // ── Particle composite with bloom ────────────────────────────
        // Raw particles: additive
        vec3 col = bg + particleSample.rgb;

        // Bloom halo around bright particle areas
        col += bloomSample * bloomStrength * glowIntensity * (1.0 + energy * 0.5);

        // ── Audio-reactive sparkle ───────────────────────────────────
        if (audioShimmer > 0.05) {
            float sparkle = noise2D(localUV * 40.0 + t * 8.0);
            sparkle = smoothstep(0.75 - audioShimmer * 0.1, 0.85, sparkle);
            vec3 sparkCol = mix(aurora2, vec3(1.0), 0.5);
            col += sparkCol * sparkle * audioShimmer * 0.6;
        }

        // ── Energy boost ─────────────────────────────────────────────
        col *= 1.0 + energy * 0.2;

        // ── Vitality response ────────────────────────────────────────
        if (isHighlighted) {
            // Brighter, more saturated for highlighted zones
            col *= 1.15;
        } else {
            col = vitalityDesaturate(col, 0.3);
        }

        result.rgb = col;
        result.a = mix(0.85, 0.95, vitality) * fillCol.a;

        // ── Inner edge glow ──────────────────────────────────────────
        float innerDist = -d;
        float innerFalloff = mix(40.0, 18.0, vitality) * px;
        float innerGlow = exp(-innerDist / innerFalloff) * mix(0.05, 0.15, vitality);
        innerGlow *= glowIntensity * (0.5 + energy * 0.4);
        // Particle clinging: sample particles near the border for edge interest
        float edgeParticle = particleSample.r + particleSample.g + particleSample.b;
        edgeParticle *= exp(-innerDist / (20.0 * px));
        vec3 innerCol = mix(aurora1, aurora2, sin(t * 0.5 + localUV.x * 4.0) * 0.5 + 0.5);
        result.rgb += innerCol * (innerGlow + edgeParticle * 0.15 * vitality);

        // ── Mouse aurora halo ────────────────────────────────────────
        result.rgb += mouseHalo(fragCoord, mouseHaloRadius, t, aurora1, aurora2);
    }

    // ── Border with flowing energy ──────────────────────────────────
    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        float borderAngle = atan(p.x, -p.y) / TAU + 0.5;
        float borderEn = borderEnergy * (1.0 + energy * mix(0.2, 0.8, vitality));

        // Color flow along border
        float colorFlow = sin(borderAngle * TAU * 2.0 + t * 1.5) * 0.5 + 0.5;
        vec3 coreColor = mix(aurora1, aurora2, colorFlow);
        coreColor = mix(coreColor, aurora3, sin(borderAngle * TAU * 3.0 - t * 0.8) * 0.3 + 0.3);
        coreColor *= glowIntensity * borderEn;

        // Angular noise for organic flow
        float flow = angularNoise(borderAngle, 12.0, -t * mix(0.3, 1.5, vitality));
        coreColor *= mix(0.5, 1.3, flow);

        // Particle trails along border: sample particle texture near the edge
        float edgeLum = (particleSample.r + particleSample.g + particleSample.b) * 0.33;
        float borderParticle = edgeLum * exp(-abs(d) / (8.0 * px)) * 2.0;
        coreColor += aurora1 * borderParticle * vitality;

        // Traveling energy pulse (like neon-venom)
        float pulse1 = exp(-mod(borderAngle - t * 0.4, 1.0) * 8.0);
        float pulse2 = exp(-mod(borderAngle + t * 0.3 + 0.5, 1.0) * 10.0);
        coreColor += aurora2 * (pulse1 + pulse2) * 0.5 * vitality;

        // Bass flash
        if (hasAudio && bass > 0.5) {
            float flash = (bass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, aurora3 * 2.5, flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core * borderCol.a);
    }

    // ── Outer glow ──────────────────────────────────────────────────
    float baseGlowR = mix(8.0, 20.0, vitality) * px;
    float glowRadius = baseGlowR + energy * 5.0 * px;
    if (d > 0.0 && d < glowRadius * 2.0) {
        float glow1 = expGlow(d, glowRadius * 0.2, glowIntensity * mix(0.08, 0.3, vitality));
        float glow2 = expGlow(d, glowRadius * 0.6, glowIntensity * mix(0.03, 0.1, vitality));
        // Color shifts along the glow perimeter
        float ga = atan(p.x, -p.y) / TAU + 0.5;
        vec3 glowColor = mix(aurora1, aurora2, sin(ga * TAU + t * 0.5) * 0.5 + 0.5) * 0.5;
        result.rgb += glowColor * (glow1 + glow2);
        result.a = max(result.a, (glow1 + glow2) * 0.4);
    }

    return result;
}

void main() {
    vec2 fc = vFragCoord;

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    // ─── Read parameters ────────────────────────────────────────────
    float glowIntensity    = max(customParams[1].y, 0.5);
    float audioReact       = customParams[1].z;
    bool  showLabels       = customParams[2].x > 0.5;
    float labelSpread      = max(customParams[2].y, 1.0);
    float labelBright      = max(customParams[2].z, 0.5);
    float labelReact       = customParams[2].w;
    float curtainIntensity = max(customParams[3].x, 0.0);
    float ribbonIntensity  = max(customParams[3].y, 0.0);
    float fogDensity       = max(customParams[3].z, 0.0);
    float bloomStrength    = max(customParams[3].w, 0.0);
    float borderEnergy     = max(customParams[4].x, 0.5);
    float mouseHaloRadius  = customParams[4].y;
    float vignetteStrength = max(customParams[4].z, 0.0);
    float colorTemp        = customParams[4].w;

    vec3 primary = colorWithFallback(customColors[0].rgb, vec3(0.04, 0.0, 0.13));
    vec3 aurora1 = colorWithFallback(customColors[1].rgb, vec3(0.0, 1.0, 0.53));
    vec3 aurora2 = colorWithFallback(customColors[2].rgb, vec3(0.0, 0.53, 1.0));
    vec3 aurora3 = colorWithFallback(customColors[3].rgb, vec3(0.8, 0.2, 0.9));

    // Audio bands (soft-gated to suppress jitter)
    bool hasAudio = iAudioSpectrumSize > 0;
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();
    float energy = hasAudio ? getOverallSoft() * audioReact : 0.0;

    // ─── Sample particle texture ────────────────────────────────────
    // vTexCoord is the unflipped UV matching the compute shader's coordinate space.
    vec4 particleSample = texture(uParticleTexture, vTexCoord);

    // Bloom: blurred halo around bright particle areas
    vec3 bloomSample = particleBloom(vTexCoord, bloomStrength);

    // ─── Render zones ───────────────────────────────────────────────
    vec4 color = vec4(0.0);

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zone = renderAuroraZone(
            fc, vTexCoord, rect, zoneFillColors[i], zoneBorderColors[i],
            zoneParams[i], zoneParams[i].z > 0.5,
            particleSample, bloomSample,
            bass, mids, treble, energy, hasAudio,
            primary, aurora1, aurora2, aurora3,
            glowIntensity, curtainIntensity, ribbonIntensity,
            fogDensity, bloomStrength, borderEnergy,
            mouseHaloRadius, audioReact
        );

        color = blendOver(color, zone);
    }

    // ─── Labels: Aurora-Energized Glowing Text ──────────────────────
    if (showLabels) {
        vec2 luv = labelsUv(fc);
        vec2 texPx = 1.0 / max(iResolution, vec2(1.0));
        vec4 labels = texture(uZoneLabels, luv);
        float spread = labelSpread * pxScale();
        float t = iTime;

        float bassMod = hasAudio ? bass * labelReact : 0.0;
        float trebleMod = hasAudio ? treble * labelReact : 0.0;

        // ── Multi-layer aurora halo sampling ─────────────────────────
        float haloTight = 0.0, haloWide = 0.0, haloVWide = 0.0;
        float haloR = 0.0, haloG = 0.0, haloB = 0.0;
        vec2 chromOff = vec2(texPx.x * 2.0, texPx.y * 0.5);
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                vec2 off = vec2(float(dx), float(dy)) * texPx;
                float r2 = float(dx * dx + dy * dy);
                float wTight = exp(-r2 * 0.5);
                float wWide = exp(-r2 * 0.2);
                float wVWide = exp(-r2 * 0.1);

                float s = texture(uZoneLabels, luv + off * spread).a;
                haloTight += s * wTight;
                haloWide += s * wWide;
                haloVWide += s * wVWide;

                haloR += texture(uZoneLabels, luv + off * spread + chromOff).a * wWide;
                haloG += s * wWide;
                haloB += texture(uZoneLabels, luv + off * spread - chromOff).a * wWide;
            }
        }
        haloTight /= 10.0;
        haloWide /= 16.5;
        haloVWide /= 22.0;

        // Aurora pulse: organic flickering synchronized with audio
        float auroraPulse = 0.85 + 0.10 * sin(t * 4.0 + luv.x * 15.0)
                               + 0.05 * sin(t * 2.5 + luv.y * 10.0);
        auroraPulse *= 1.0 + bassMod * 0.5;

        if (haloWide > 0.003) {
            float haloEdge = haloWide * (1.0 - labels.a);
            float haloEdgeTight = haloTight * (1.0 - labels.a);
            float haloEdgeVWide = haloVWide * (1.0 - labels.a);

            // ── Aurora curtain tendrils radiating from label ─────────
            float angle = atan(luv.y - 0.5, luv.x - 0.5);
            float tendrilNoise = noise2D(vec2(angle * 5.0, haloEdge * 8.0 + t * 0.3));
            float tendril = smoothstep(0.3, 0.7, tendrilNoise) * haloEdgeVWide;
            vec3 tendrilCol = mix(aurora1, aurora2, 0.5 + 0.5 * sin(angle * 2.0 + t));
            color.rgb += tendrilCol * tendril * 0.5 * auroraPulse;

            // ── Tight core glow: hot aurora center ───────────────────
            vec3 coreGlow = mix(aurora1, aurora2, 0.3) * 1.3;
            color.rgb += coreGlow * haloEdgeTight * 0.6 * auroraPulse;

            // ── Chromatic aurora bloom: green/blue split channels ─────
            vec3 chromHalo = vec3(haloR, haloG, haloB) * (1.0 - labels.a);
            vec3 chromCol = chromHalo * mix(aurora1, aurora2, 0.4) * 0.4 * auroraPulse;
            chromCol.g *= aurora1.g * 1.3;
            chromCol.b *= aurora2.b * 1.2;
            color.rgb += chromCol;

            // ── Wide atmospheric aurora haze ─────────────────────────
            vec3 hazeCol = mix(aurora1 * 0.3, aurora3 * 0.3, 0.5 + 0.5 * sin(t * 0.2));
            color.rgb += hazeCol * haloEdgeVWide * 0.25 * auroraPulse;

            // ── Upward aurora bleed from text ────────────────────────
            float bleedSample = texture(uZoneLabels, luv + vec2(0.0, texPx.y * spread * 4.0)).a;
            float bleed = bleedSample * (1.0 - labels.a) * 0.35;
            if (bleed > 0.01) {
                float bleedNoise = noise2D(vec2(luv.x * 30.0, t * 0.4));
                vec3 bleedCol = mix(aurora1, aurora3, bleedNoise * 0.5) * bleed;
                color.rgb += bleedCol * auroraPulse;
            }

            // ── Bass sparks along halo ───────────────────────────────
            if (hasAudio && bass > 0.1) {
                float sparkNoise = noise2D(luv * 50.0 + t * 5.0);
                float spark = smoothstep(0.6, 0.9, sparkNoise) * bass * 2.0 * labelReact;
                color.rgb += aurora2 * haloEdge * spark * auroraPulse;
            }

            color.a = max(color.a, haloEdge * 0.5);
        }

        // ── Label text body: aurora-energized glowing tubes ──────────
        if (labels.a > 0.01) {
            // Color sweep: aurora green/cyan cycling through characters
            float auroraWave = sin(fc.x * 0.12 - t * 2.5 + fc.y * 0.06) * 0.5 + 0.5;
            vec3 tubeColor = mix(aurora1, aurora2, auroraWave * 0.5);
            tubeColor = mix(tubeColor, aurora3, pow(auroraWave, 3.0) * 0.25);

            // Internal shimmer texture
            float shimmerTex = noise2D(luv * 60.0 + t * 0.6);
            tubeColor = mix(tubeColor, vec3(1.0) * 0.8, shimmerTex * 0.15);

            // Edge-detected rim for glowing tube effect
            float aL = texture(uZoneLabels, luv + vec2(-texPx.x, 0.0)).a;
            float aR = texture(uZoneLabels, luv + vec2( texPx.x, 0.0)).a;
            float aU = texture(uZoneLabels, luv + vec2(0.0, -texPx.y)).a;
            float aD = texture(uZoneLabels, luv + vec2(0.0,  texPx.y)).a;
            float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

            vec3 textCol = tubeColor * 0.6 + mix(aurora1, vec3(1.0), 0.5) * rim * 0.5;
            textCol *= labelBright * auroraPulse;
            textCol *= 1.0 + bassMod * 0.4;

            // Treble: aurora flash bands across text
            if (hasAudio && treble > 0.1) {
                float flashBand = step(0.82, fract(fc.y * 0.1 + t * 7.0));
                textCol = mix(textCol, aurora3 * labelBright * 1.5, flashBand * trebleMod * 0.4);
            }

            // Gentle tonemap to prevent blowout
            textCol = textCol / (0.5 + textCol);

            color.rgb = mix(color.rgb, textCol, labels.a);
            color.a = max(color.a, labels.a);
        }
    }

    // ─── Vignette + color grading ───────────────────────────────────
    vec2 q = fc / max(iResolution, vec2(1.0));

    // Improved vignette curve
    float vigDist = length(q - 0.5) * 1.4;
    float vig = 1.0 - vigDist * vigDist * vignetteStrength;
    vig = max(vig, 0.0);
    color.rgb *= vig;

    // Subtle chromatic shift at vignette edges
    float chromaEdge = vigDist * vigDist * 0.02;
    color.r *= 1.0 + chromaEdge * 0.5;
    color.b *= 1.0 - chromaEdge * 0.3;

    // Color temperature adjustment: positive = warm, negative = cool
    color.r *= 1.0 + colorTemp * 0.05;
    color.b *= 1.0 - colorTemp * 0.05;

    fragColor = clampFragColor(color);
}
