// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Canvas -- Final composite pass
// Pass 0: flow field (displacement map) -> iChannel0
// Pass 1: texture with flow distortion   -> iChannel1
// Pass 2: bloom + edge glow              -> iChannel2
// This pass: zone masking, borders, iridescence, vitality, labels.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>


// ---- Parameters ----

float getFillOpacity()  { return customParams[3].x >= 0.0 ? customParams[3].x : 0.92; }
float getZoneTint()     { return customParams[3].y >= 0.0 ? customParams[3].y : 0.15; }
float getIridescence()  { return customParams[3].z >= 0.0 ? customParams[3].z : 0.2; }
float getChannelMix()   { return customParams[3].w >= 0.0 ? customParams[3].w : 0.5; }
float getAudioReact()   { return customParams[0].w >= 0.0 ? customParams[0].w : 1.0; }


// Thin-film iridescence approximation
vec3 iridescent(float angle, float intensity) {
    // Simulate thin-film interference: color shifts with viewing angle
    float phase = angle * 6.0 + intensity * 2.0;
    return vec3(
        0.5 + 0.5 * cos(phase),
        0.5 + 0.5 * cos(phase + 2.094), // +2pi/3
        0.5 + 0.5 * cos(phase + 4.189)  // +4pi/3
    );
}


vec4 sampleCanvas(vec2 fragCoord, float iridStr) {
    vec4 c1 = texture(iChannel1, channelUv(1, fragCoord));
    vec4 c2 = texture(iChannel2, channelUv(2, fragCoord));
    vec4 flow = texture(iChannel0, channelUv(0, fragCoord));

    float mixVal = getChannelMix();
    vec3 col = mix(c1.rgb, c2.rgb, mixVal);

    // Iridescent highlights based on flow direction
    if (iridStr > 0.01) {
        float flowAngle = atan(flow.g - 0.5, flow.r - 0.5);
        float flowMag = flow.b * 2.0;
        vec3 irid = iridescent(flowAngle, flowMag);
        // Apply iridescence more strongly at high-flow regions
        col += irid * iridStr * flowMag * 0.5;
    }

    return vec4(clamp(col, 0.0, 1.0), 1.0);
}


vec4 renderCanvasZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                      vec4 params, bool isHighlighted,
                      float bass, float mids, float treble, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth  = max(params.y, 2.0);
    float fillOpacity  = getFillOpacity();
    float iridStr      = getIridescence();
    float audioReact   = getAudioReact();

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.4, 0.6, 0.9));

    float vitality = isHighlighted ? 1.0 : 0.3;

    // ---- Inside zone ----
    if (d < 0.0) {
        vec4 canvas = sampleCanvas(fragCoord, isHighlighted ? iridStr * 1.5 : iridStr);

        // Zone fill color tint
        float tintAmount = getZoneTint();
        result.rgb = mix(canvas.rgb, canvas.rgb * fillColor.rgb, tintAmount);
        result.a = fillOpacity;

        // Vitality: saturation and brightness
        float saturation = mix(0.5, 1.4, vitality);
        float brightness = mix(0.7, 1.2, vitality);
        float lum = luminance(result.rgb);
        result.rgb = mix(vec3(lum), result.rgb, saturation);
        result.rgb *= brightness;

        if (isHighlighted) {
            result.a = min(result.a + 0.05, 1.0);
        }

        // Inner glow: flow-intensity-driven radial pulse
        float pulseSpeed = mix(0.5, 3.0, vitality);
        float pulseBase = 0.5 + 0.5 * sin(iTime * pulseSpeed);
        float glowStrength = mix(0.05, 0.25, vitality);
        float radialDist = length(localUV - 0.5) * 2.0;
        float innerGlow = max(0.0, 1.0 - radialDist) * pulseBase * glowStrength;

        // Bass: inner glow surge -- zone position hash determines resonance
        if (hasAudio && bass > 0.05) {
            float nodeFreq = hash21(rectPos * 0.01);
            float resonance = max(0.0, 1.0 - abs(nodeFreq - bass * 2.0) * 3.0) * bass * bass;
            innerGlow *= 1.0 + resonance * audioReact * 1.5;
        }

        // Glow color from border with mids-driven hue shift
        vec3 glowClr = borderClr;
        if (hasAudio && mids > 0.05) {
            float shift = mids * audioReact * 0.5;
            vec3 warm = vec3(1.0, 0.7, 0.4);
            glowClr = mix(borderClr, warm, shift);
        }

        result.rgb += glowClr * innerGlow;
        result.rgb = clamp(result.rgb, 0.0, 1.0);
    }

    // ---- Border ----
    float angle = atan(p.y, p.x);
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        // Animated liquid flow along border
        float flowSpeed = mix(0.3, 2.0, vitality);
        float flowRange = mix(0.1, 0.35, vitality);
        float flow = angularNoise(angle, 8.0, -iTime * flowSpeed) * flowRange + (1.0 - flowRange * 0.5);

        // Bass: wave pulse traveling along perimeter
        float pulsePos = hasAudio ? fract(iTime * 1.8) * TAU : 0.0;
        float angularDist = 1.0 - abs(mod(angle - pulsePos + PI, TAU) - PI) / PI;
        float borderEnergy = 1.0 + (hasAudio ? angularDist * bass * 0.7 : 0.0);

        float borderAlpha = border * mix(0.85, 0.95, vitality);
        vec3 flowColor = borderClr * flow * borderEnergy;

        // Iridescent border shimmer
        if (iridStr > 0.01) {
            vec3 irid = iridescent(angle * 0.5 + iTime * 0.3, vitality);
            flowColor = mix(flowColor, flowColor * irid, iridStr * 0.5);
        }

        if (isHighlighted) {
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            flowColor = mix(flowColor, fillColor.rgb * borderEnergy, accentTrace * 0.25);
        }

        result.rgb = mix(result.rgb, flowColor, borderAlpha);
        result.a = max(result.a, border * 0.98);
    }

    // ---- Outer glow ----
    if (d > 0.0 && d < 22.0) {
        float glowRadius = mix(5.0, 9.0, vitality);
        float glowFalloff = mix(0.3, 0.55, vitality);

        // Bass: expanding glow wavefront
        if (hasAudio) {
            float waveCycle = fract(iTime * 1.0);
            float waveRadius = waveCycle * 16.0;
            float waveBand = exp(-abs(d - waveRadius) * 0.5) * (1.0 - waveCycle);
            glowRadius += waveBand * bass * bass * 6.0;
            glowFalloff += waveBand * bass * 0.3;
        }

        float glow = expGlow(d, glowRadius, glowFalloff);
        glow *= mix(0.3, 1.0, vitality);

        result.rgb += borderClr * glow;
        result.a = max(result.a, glow * 0.6);
    }

    // ---- Treble: paint splatter sparks near inner edge ----
    if (hasAudio && treble > 0.06 && d > -borderWidth * 3.0 && d < 0.0) {
        float edgeProx = smoothstep(-borderWidth * 3.0, 0.0, d);
        float spark = 0.0;
        for (int si = 0; si < 3; si++) {
            float sparkSpeed = 2.5 + float(si) * 1.5;
            float sparkPos = fract(iTime * sparkSpeed * 0.3 + float(si) * 0.333);
            float sparkAngle = sparkPos * TAU;
            float aDist = abs(mod(angle - sparkAngle + PI, TAU) - PI);
            spark += exp(-aDist * 8.0) * (0.5 + 0.5 * sin(iTime * sparkSpeed));
        }
        spark = min(spark, 1.0);
        vec3 sparkClr = colorWithFallback(customColors[2].rgb, vec3(0.4, 0.8, 0.73));
        result.rgb += sparkClr * edgeProx * spark * treble * audioReact * 0.5;
    }

    return result;
}


// ---- Custom Label Composite ----

vec4 compositeCanvasLabels(vec4 color, vec2 fragCoord,
                           float bass, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.0;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    // Gaussian halo — sum of exp(-r²*0.3) weights over 5×5 kernel ≈ 9.51
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * labelGlowSpread).a * w;
        }
    }
    halo /= 9.51;

    // Flow-tinted halo outline
    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);
        vec3 glowClr = colorWithFallback(customColors[1].rgb, vec3(0.8, 0.4, 0.67));

        // Iridescent label glow: color shifts with position
        float iridPhase = length(uv - 0.5) * 4.0 + iTime * 0.5;
        vec3 iridGlow = iridescent(iridPhase, 1.0);
        float iridStr = customParams[3].z >= 0.0 ? customParams[3].z : 0.2;
        glowClr = mix(glowClr, iridGlow, iridStr * 0.4);

        float haloBright = haloEdge * (0.5 + (hasAudio ? bass * 0.5 * labelAudioReact : 0.0));
        color.rgb += glowClr * haloBright;

        // Treble sparkles near labels
        if (hasAudio && treble > 0.1) {
            float sparkNoise = noise2D(uv * 50.0 + iTime * 3.0);
            float spark = smoothstep(0.7, 0.95, sparkNoise) * treble * 2.0 * labelAudioReact;
            color.rgb += glowClr * haloEdge * spark;
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // Core label brightening
    if (labels.a > 0.01) {
        vec3 lens = color.rgb * labelBrightness;
        float bassPulse = hasAudio ? 1.0 + bass * 0.4 * labelAudioReact : 1.0;
        lens *= bassPulse;
        color.rgb = mix(color.rgb, lens, labels.a);
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
    float bass     = getBassSoft();
    float mids     = getMidsSoft();
    float treble   = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderCanvasZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[4].w > 0.5)
        color = compositeCanvasLabels(color, fragCoord, bass, treble, hasAudio);
    fragColor = clampFragColor(color);
}
