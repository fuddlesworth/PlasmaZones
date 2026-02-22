// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * PRISMATA - Unified Crystalline Prismatic Overlay
 *
 * Renders a SINGLE continuous crystalline field across the entire overlay.
 * All pattern/animation uses fragCoord (screen space) so facets flow seamlessly
 * across zone boundaries — no disjointed per-zone effects.
 *
 * Novel highlighting (not just color change):
 * - Caustic pooling: Light patterns that intensify inside highlighted zones
 * - Chromatic fracture: RGB dispersion on facet edges in highlighted zones
 * - Inner resonance: Pulsing refraction/glow from within facets when highlighted
 * - Traveling crystallite: Sweeping light creates sharper "diamond" sparkle on highlight
 *
 * Audio reactive (when CAVA/spectrum available):
 * - Bass: traveling light pulse, border flash, outer glow expansion
 * - Mids: facet edge brightness; Treble: caustic boost
 * - Idle: subtle pulse when no audio
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// === GLOBAL UNIFIED HELPERS (all use fragCoord / screen space) ===

// Voronoi: returns (dist to cell center, cell id hash, edge distance)
vec3 voronoi(vec2 uv, float scale, float time) {
    vec2 n = floor(uv * scale);
    vec2 f = fract(uv * scale);
    float md = 8.0;
    float md2 = 8.0;
    float mid = 0.0;

    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = hash22(n + g);
            o = 0.5 + 0.5 * sin(time + TAU * o);
            vec2 r = g + o - f;
            float d = length(r);
            if (d < md) {
                md2 = md;
                md = d;
                mid = hash21(n + g);
            } else if (d < md2) {
                md2 = d;
            }
        }
    }
    float edgeDist = md2 - md; // Distance to cell edge
    return vec3(md, mid, edgeDist);
}

// Caustic-like pattern (screen space, flows across zones)
float caustics(vec2 uv, float time) {
    vec2 p = uv * 8.0 + vec2(time * 0.3, time * 0.2);
    float v = 0.0;
    v += sin(p.x) * sin(p.y);
    p *= 2.0;
    v += sin(p.x + time * 0.5) * sin(p.y - time * 0.3) * 0.5;
    p *= 1.5;
    v += sin(p.x * 1.7) * cos(p.y * 1.3 + time) * 0.25;
    return v * 0.5 + 0.5;
}

// Chromatic dispersion offset
vec3 chromaticSample(float baseVal, float edgeDist, float strength) {
    float r = baseVal + sin(edgeDist * 20.0) * strength * 0.15;
    float g = baseVal;
    float b = baseVal - sin(edgeDist * 20.0 + 1.0) * strength * 0.15;
    return vec3(r, g, b);
}

// IQ palette
vec3 palette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

// === ZONE RENDER (uses global effect, applies zone tint + novel highlight) ===

vec4 renderPrismataZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                        vec4 params, bool isHighlighted,
                        float bass, float mids, float treble, float overall, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Params
    float cellScale = customParams[0].x >= 0.0 ? customParams[0].x : 12.0;
    float animSpeed = customParams[0].y >= 0.0 ? customParams[0].y : 0.6;
    float facetSharpness = customParams[0].z >= 0.0 ? customParams[0].z : 0.7;
    float zoneTintBlend = customParams[0].w >= 0.0 ? customParams[0].w : 0.35;
    float fillOpacity = customParams[1].x >= 0.0 ? customParams[1].x : 0.88;
    float causticStr = customParams[1].y >= 0.0 ? customParams[1].y : 0.5;
    float chromaStr = customParams[1].z >= 0.0 ? customParams[1].z : 0.4;
    float resonanceStr = customParams[1].w >= 0.0 ? customParams[1].w : 0.6;
    float audioReact = customParams[2].x >= 0.0 ? customParams[2].x : 1.0;
    float idlePulse = customParams[2].y >= 0.0 ? customParams[2].y : 0.8;

    float energy = hasAudio ? overall * audioReact : 0.0;
    float idleAnim = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 1.2 * PI)) * idlePulse;

    vec3 accent = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.83, 1.0));
    vec3 hlTint = colorWithFallback(customColors[1].rgb, vec3(1.0));
    vec3 cryst1 = colorWithFallback(customColors[2].rgb, vec3(0.2, 0.27, 0.4));
    vec3 cryst2 = colorWithFallback(customColors[3].rgb, vec3(0.27, 0.53, 0.8));
    vec3 cryst3 = colorWithFallback(customColors[4].rgb, vec3(0.53, 0.67, 0.87));
    vec3 cryst4 = colorWithFallback(customColors[5].rgb, vec3(0.67, 0.8, 1.0));
    vec3 edgeClr = colorWithFallback(customColors[6].rgb, accent);

    // Mouse interaction
    vec2 mouseLocal = zoneLocalUV(iMouse.xy, rectPos, rectSize);
    vec2 mouseGlobal = iMouse.xy / max(iResolution, vec2(1.0));
    bool mouseInZone = mouseLocal.x >= 0.0 && mouseLocal.x <= 1.0 &&
                       mouseLocal.y >= 0.0 && mouseLocal.y <= 1.0;

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        // === UNIFIED: Global UV — pattern flows across entire overlay ===
        vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
        float speedMod = 1.0 + energy * 0.8 + idleAnim * 0.4;
        float time = iTime * animSpeed * speedMod;

        float mouseDist = length(globalUV - mouseGlobal);
        float mouseInfluence = mouseInZone ? smoothstep(0.3, 0.0, mouseDist) : 0.0;

        // Voronoi cells in screen space — distort toward cursor when mouse is in zone
        vec2 vorUV = globalUV;
        if (mouseInZone && mouseInfluence > 0.01) {
            vec2 toCursor = mouseGlobal - globalUV;
            vorUV += toCursor * mouseInfluence * 0.05;
        }
        vec3 vor = voronoi(vorUV, cellScale, time);
        float cellDist = vor.x;
        float cellId = vor.y;
        float edgeDist = vor.z;

        // Facet color — cycles through Crystal 1→2→3→4 based on cell and time
        float t = fract(cellId + time * 0.1);
        vec3 facetColor = mix(
            mix(cryst1, cryst2, smoothstep(0.0, 0.5, t)),
            mix(cryst3, cryst4, smoothstep(0.5, 1.0, t)),
            smoothstep(0.25, 0.75, t)
        );

        // Facet edge — Edge Color brightens at cell boundaries (mids pulse the edge)
        float edgeFactor = exp(-edgeDist * facetSharpness * 4.0);
        float edgePulse = hasAudio ? (0.6 + 0.4 * mids * audioReact) : (0.6 + 0.2 * idleAnim);
        // Mouse: boost edges near cursor
        if (mouseInZone) {
            float cursorEdgeBoost = mouseInfluence * 0.4;
            edgeFactor = min(edgeFactor + cursorEdgeBoost * edgeFactor, 1.0);
            edgePulse += mouseInfluence * 0.3;
        }
        vec3 edgeColor = mix(facetColor, edgeClr, edgeFactor * 0.5 * edgePulse);

        // Traveling light — ONE light that sweeps across entire overlay (bass intensifies)
        vec2 lightPos = vec2(0.5 + 0.35 * cos(time * 0.8), 0.5 + 0.35 * sin(time * 0.6));
        // Mouse: light gravitates toward cursor
        if (mouseInZone) {
            lightPos = mix(lightPos, mouseGlobal, mouseInfluence * 0.7);
        }
        vec2 toLight = lightPos - globalUV;
        float lightDist = length(toLight);
        float spec = pow(max(0.0, 1.0 - lightDist * 3.0), 4.0);
        float specPulse = 0.5 + 0.5 * sin(time * 2.0);
        specPulse += hasAudio ? bass * audioReact * 0.6 : idleAnim * 0.3;
        spec *= specPulse;
        vec3 specular = accent * spec;

        // Base result
        vec3 base = edgeColor + specular;
        base = mix(base, fillColor.rgb, zoneTintBlend); // Zone tint

        // Mouse: cursor glow hotspot
        if (mouseInZone) {
            float cursorGlow = exp(-mouseDist * mouseDist * 80.0) * 0.15;
            base += accent * cursorGlow;
        }

        // === NOVEL HIGHLIGHT EFFECTS (not just color change) ===
        if (isHighlighted) {
            // 1) Caustic pooling — light patterns intensify in highlighted zones (treble boosts)
            float cau = caustics(globalUV, time);
            float cauMask = smoothstep(0.3, 0.7, cau);
            float cauBoost = hasAudio ? (1.0 + treble * audioReact * 0.8) : (1.0 + idleAnim * 0.4);
            base += accent * cauMask * causticStr * 0.35 * cauBoost;
            base += hlTint * cauMask * causticStr * 0.15 * cauBoost;

            // 2) Chromatic fracture — RGB dispersion on facet edges
            vec3 chroma = chromaticSample(1.0, edgeDist, chromaStr);
            base *= mix(vec3(1.0), chroma, edgeFactor * chromaStr);

            // 3) Inner resonance — pulsing refraction from within facets (bass-driven)
            float pulse = hasAudio
                ? (0.6 + 0.4 * sin(time * 3.0) + bass * audioReact * 0.5)
                : (0.7 + 0.3 * sin(time * 3.0) + idleAnim * 0.2);
            float radial = 1.0 - length(localUV - 0.5) * 1.5;
            float resonance = max(0.0, radial) * pulse * resonanceStr * 0.25;
            base += accent * resonance;
            base += hlTint * resonance * 0.5;

            // 4) Traveling crystallite — sharper diamond sparkle when light passes through
            spec *= 1.5 + resonanceStr;
            base += specular * 0.6;

            // Slight overall brighten
            base *= 1.08;
        }

        result.rgb = base;
        result.a = fillOpacity;
    }

    // Border (audio: bass flash, energy flow)
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 borderClr = colorWithFallback(borderColor.rgb, accent);
        float flow = 0.5 + 0.5 * sin(atan(p.y, p.x) * 6.0 + iTime * (2.0 + energy * 3.0) + idleAnim * 2.0);
        if (hasAudio && bass > 0.4) {
            float bassFlash = (bass - 0.4) * audioReact;
            borderClr = mix(borderClr, vec3(1.0), bassFlash * 0.25);
        }
        borderClr = mix(borderClr, accent, flow * 0.3);
        if (isHighlighted) {
            borderClr = mix(borderClr, accent, 0.4);
            borderClr *= 1.1;
        }
        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }

    // Outer glow for highlighted zones (bass expands glow)
    if (isHighlighted && d > 0.0) {
        float glowR = 28.0 + (hasAudio ? bass * audioReact * 20.0 : idleAnim * 8.0);
        if (d < glowR) {
            float glow = expGlow(d, 9.0, 0.5 * (1.0 + energy * 0.5));
            result.rgb += accent * glow * 0.4;
            result.a = max(result.a, glow * 0.55);
        }
    }

    return result;
}

// Custom label composite — labels get subtle prismatic outline
vec4 compositePrismataLabels(vec4 color, vec2 fragCoord) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 accent = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.83, 1.0));

    // Dilate for outline (8 samples)
    float dilated = labels.a;
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(-2.0,  0.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2( 2.0,  0.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(0.0, -2.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(0.0,  2.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(-1.4, -1.4) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2( 1.4, -1.4) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(-1.4,  1.4) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2( 1.4,  1.4) * px).a);
    float outline = max(0.0, dilated - labels.a);

    // Prismatic outline
    float phase = fragCoord.x * 0.02 + fragCoord.y * 0.015 + iTime * 0.5;
    vec3 prismOutline = vec3(
        sin(phase) * 0.5 + 0.5,
        sin(phase + 2.094) * 0.5 + 0.5,
        sin(phase + 4.189) * 0.5 + 0.5
    );
    prismOutline = mix(accent, prismOutline, 0.5);

    color.rgb = mix(color.rgb, prismOutline, outline * 0.8);
    color.a = max(color.a, outline * 0.7);

    if (labels.a > 0.01) {
        color.rgb = color.rgb * (1.0 - labels.a) + labels.rgb;
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

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderPrismataZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositePrismataLabels(color, fragCoord);

    fragColor = clampFragColor(color);
}
