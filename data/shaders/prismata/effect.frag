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
 * - Bass: facet resonance (per-facet rhythm at unique frequencies), resonant node glow
 * - Mids: prismatic diffraction (rainbow hue splitting along facet edges)
 * - Treble: crystal fracture lines (bright crack-like flashes at facet boundaries)
 * - Border: harmonic shimmer (bass/mids/treble light different angular regions)
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
    float vitality = zoneVitality(isHighlighted);

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
        // Harmonic shimmer: bass drives large-scale time warp, treble adds fine jitter
        float bassWarp = hasAudio ? sin(iTime * 0.7) * bass * audioReact * 0.3 : 0.0;
        float trebleJitter = hasAudio ? noise1D(iTime * 12.0) * treble * audioReact * 0.08 : 0.0;
        float speedMod = 1.0 + bassWarp + trebleJitter + idleAnim * 0.4;
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

        // Prismatic diffraction: mids cause rainbow color splitting along facet edges
        float edgeFactor = exp(-edgeDist * facetSharpness * 4.0);
        // Per-facet diffraction phase so each edge refracts differently
        float diffractionPhase = cellId * TAU + edgeDist * 15.0;
        float edgePulse = hasAudio
            ? (0.6 + 0.3 * sin(diffractionPhase + iTime * 2.5) * mids * audioReact)
            : (0.6 + 0.2 * idleAnim);
        // Mouse: boost edges near cursor
        if (mouseInZone) {
            float cursorEdgeBoost = mouseInfluence * 0.4;
            edgeFactor = min(edgeFactor + cursorEdgeBoost * edgeFactor, 1.0);
            edgePulse += mouseInfluence * 0.3;
        }
        // Mids-driven hue shift along edges — prismatic rainbow refraction
        vec3 edgeTint = edgeClr;
        if (hasAudio && mids > 0.05) {
            float hueShift = diffractionPhase * 0.5 + edgeDist * 8.0;
            vec3 rainbow = vec3(
                sin(hueShift) * 0.5 + 0.5,
                sin(hueShift + 2.094) * 0.5 + 0.5,
                sin(hueShift + 4.189) * 0.5 + 0.5
            );
            edgeTint = mix(edgeClr, rainbow, mids * audioReact * 0.4);
        }
        vec3 edgeColor = mix(facetColor, edgeTint, edgeFactor * 0.5 * edgePulse);

        // Traveling light — ONE light that sweeps across entire overlay
        vec2 lightPos = vec2(0.5 + 0.35 * cos(time * 0.8), 0.5 + 0.35 * sin(time * 0.6));
        // Mouse: light gravitates toward cursor
        if (mouseInZone) {
            lightPos = mix(lightPos, mouseGlobal, mouseInfluence * 0.7);
        }
        vec2 toLight = lightPos - globalUV;
        float lightDist = length(toLight);
        float spec = pow(max(0.0, 1.0 - lightDist * 3.0), 4.0);
        // Facet resonance: each facet has its own bass-driven pulse rhythm
        float facetPhase = cellId * TAU;
        float facetResonance = sin(iTime * (1.5 + cellId * 2.0) + facetPhase);
        float specPulse = 0.5 + 0.5 * sin(time * 2.0);
        specPulse += hasAudio ? facetResonance * bass * audioReact * 0.4 : idleAnim * 0.3;
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

        // === VITALITY-SCALED EFFECTS (rich when highlighted, subtle when dormant) ===
        {
            // 1) Caustic pooling — light patterns inside zones
            float cau = caustics(globalUV, time);
            float cauMask = smoothstep(0.3, 0.7, cau);
            float fractureEdge = smoothstep(0.08, 0.02, edgeDist);
            float trebleSpike = hasAudio ? pow(treble * audioReact, 2.0) : 0.0;
            float fractureBright = fractureEdge * trebleSpike * 1.5;
            float cauBoost = 1.0 + (hasAudio ? fractureBright * 0.4 : idleAnim * 0.4);
            base += accent * cauMask * causticStr * vitalityScale(0.08, 0.35, vitality) * cauBoost;
            base += hlTint * cauMask * causticStr * vitalityScale(0.03, 0.15, vitality) * cauBoost;
            base += hlTint * fractureBright * vitalityScale(0.04, 0.2, vitality);

            // 2) Chromatic fracture — RGB dispersion on facet edges
            vec3 chroma = chromaticSample(1.0, edgeDist, chromaStr);
            base *= mix(vec3(1.0), chroma, edgeFactor * chromaStr * vitalityScale(0.3, 1.0, vitality));

            // 3) Resonant node glow — facet vertices pulse with bass
            float nodeFreq = 1.5 + cellId * 2.0;
            float nodePhase = cellId * TAU;
            float nodePulse = hasAudio
                ? (0.6 + 0.4 * sin(iTime * nodeFreq + nodePhase) * bass * audioReact)
                : (0.7 + 0.3 * sin(time * 3.0) + idleAnim * 0.2);
            float cellCenterGlow = exp(-cellDist * cellDist * 8.0);
            float resonance = cellCenterGlow * nodePulse * resonanceStr * vitalityScale(0.08, 0.35, vitality);
            base += accent * resonance;
            base += hlTint * resonance * vitalityScale(0.15, 0.5, vitality);

            // 4) Traveling crystallite — sharper sparkle when highlighted
            base += specular * vitalityScale(0.1, 0.6, vitality);
        }

        // Dormant desaturation and vitality brightness
        base = vitalityDesaturate(base, vitality);
        base *= vitalityScale(0.8, 1.08, vitality);

        result.rgb = base;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // Border — prismatic traveling light with bass-driven facet flash
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 borderClr = colorWithFallback(borderColor.rgb, accent);
        // Angle around the border perimeter
        float angle = atan(p.y, p.x);
        // Use integer lobe count (6) to avoid atan seam artifacts
        float flow = 0.5 + 0.5 * sin(angle * 6.0 + iTime * 2.0 + idleAnim * 2.0);
        if (hasAudio) {
            // Harmonic shimmer: different bands light up different angular regions
            // Bass lights broad lobes (2), mids light medium (4), treble lights fine (8)
            float bassLobe = sin(angle * 2.0 + iTime * 1.2) * bass * audioReact;
            float midsLobe = sin(angle * 4.0 + iTime * 2.0) * mids * audioReact;
            float trebleLobe = sin(angle * 8.0 + iTime * 3.5) * treble * audioReact;
            float harmonic = max(bassLobe * 0.3, 0.0) + max(midsLobe * 0.15, 0.0) + max(trebleLobe * 0.1, 0.0);
            borderClr += accent * harmonic;
        }
        borderClr = mix(borderClr, accent, flow * 0.3);
        borderClr = mix(borderClr, accent, vitalityScale(0.05, 0.4, vitality));
        borderClr *= vitalityScale(0.8, 1.1, vitality);
        borderClr = vitalityDesaturate(borderClr, vitality);
        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }

    // Outer glow (both states, vitality-modulated)
    float outerGlowR = vitalityScale(10.0, 28.0, vitality) + idleAnim * vitalityScale(3.0, 8.0, vitality);
    if (hasAudio) {
        float glowAngle = atan(p.y, p.x);
        float nodePattern = 0.5 + 0.5 * sin(glowAngle * 6.0 + iTime * 1.8);
        outerGlowR += nodePattern * bass * audioReact * vitalityScale(5.0, 18.0, vitality);
    }
    if (d > 0.0 && d < outerGlowR) {
        float outerStr = vitalityScale(0.15, 0.5, vitality) + (hasAudio ? mids * audioReact * vitalityScale(0.08, 0.3, vitality) : 0.0);
        float glow = expGlow(d, vitalityScale(5.0, 9.0, vitality), outerStr);
        vec3 glowCol = vitalityDesaturate(accent, vitality);
        result.rgb += glowCol * glow * vitalityScale(0.15, 0.4, vitality);
        result.a = max(result.a, glow * vitalityScale(0.2, 0.55, vitality));
    }

    return result;
}

// Crystal Etch — labels appear etched into the crystalline lattice with
// prismatic refraction, facet-edge flash, chromatic split, diamond sparkle.
vec4 compositePrismataLabels(vec4 color, vec2 fragCoord,
                              float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float etchSpread   = customParams[2].z >= 0.0 ? customParams[2].z : 3.0;
    float etchBright   = customParams[2].w >= 0.0 ? customParams[2].w : 2.0;
    float refractReact = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;

    vec3 accent = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.83, 1.0));
    vec3 hlTint = colorWithFallback(customColors[1].rgb, vec3(1.0));

    // Sample voronoi at label position — crystal facet geometry
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float cellScale = customParams[0].x >= 0.0 ? customParams[0].x : 12.0;
    float animSpeed = customParams[0].y >= 0.0 ? customParams[0].y : 0.6;
    float time = iTime * animSpeed;
    vec3 vor = voronoi(globalUV, cellScale, time);
    float edgeDist = vor.z;
    float cellId = vor.y;

    // ── Gaussian halo with crystal-edge amplification ──────────────
    float halo = 0.0;
    float totalW = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            float w = exp(-float(dx*dx + dy*dy) * 0.5);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * etchSpread).a * w;
            totalW += w;
        }
    }
    halo /= max(totalW, 0.001);
    // Facet edges near labels glow brighter (crystal-edge amplification)
    halo *= 1.0 + exp(-edgeDist * 6.0) * 0.6;
    float outline = max(0.0, halo - labels.a * 0.7);

    // ── Prismatic rainbow outline ──────────────────────────────────
    if (outline > 0.01) {
        // Phase from position + voronoi cell for per-facet spectral variation
        float prismPhase = globalUV.x * 15.0 + globalUV.y * 10.0
                         + cellId * TAU + time * 0.8;
        if (hasAudio) prismPhase += bass * refractReact * 1.5;
        vec3 rainbow = vec3(
            sin(prismPhase) * 0.5 + 0.5,
            sin(prismPhase + 2.094) * 0.5 + 0.5,
            sin(prismPhase + 4.189) * 0.5 + 0.5
        );
        vec3 prismClr = mix(accent, rainbow, 0.55);

        // Per-cell resonance: each facet pulses at its own bass frequency
        float resonance = hasAudio
            ? 1.0 + sin(iTime * (1.5 + cellId * 2.0) + cellId * TAU)
                  * bass * refractReact * 0.4
            : 1.0;
        // Mids shift hue balance between accent and rainbow
        float midsBlend = hasAudio ? smoothstep(0.05, 0.5, mids) * 0.3 : 0.0;
        prismClr = mix(prismClr, rainbow, midsBlend);

        color.rgb += prismClr * outline * 0.8 * resonance;
        color.a = max(color.a, outline * 0.6);
    }

    // ── Treble facet flash: crystal edges near labels catch light ───
    if (hasAudio && treble > 0.08) {
        float facetFlash = exp(-edgeDist * 8.0);
        float sparkPhase = fract(cellId * 7.0 + iTime * 3.0);
        float sparkPulse = pow(max(0.0, sin(sparkPhase * TAU)), 8.0);
        float flash = facetFlash * sparkPulse * treble * refractReact * 0.5;
        float labelProx = max(halo, labels.a);
        color.rgb += hlTint * flash * labelProx;
    }

    // ── Chromatic aberration core: prismatic RGB split ─────────────
    if (labels.a > 0.01) {
        float caAngle = time * 0.5 + cellId * TAU;
        vec2 caDir = vec2(cos(caAngle), sin(caAngle));
        float caAmount = (hasAudio ? 1.5 + bass * refractReact * 2.0 : 1.5)
                         * px.x * iResolution.x * 0.002;

        float rCh = texture(uZoneLabels, uv + caDir * caAmount).a;
        float gCh = labels.a;
        float bCh = texture(uZoneLabels, uv - caDir * caAmount).a;
        float maxCh = max(max(rCh, gCh), bCh);

        vec3 chromaticLabel = vec3(rCh, gCh, bCh);
        vec3 boosted = color.rgb * etchBright + chromaticLabel * accent * 0.5;

        // Diamond sparkle: bright peaks traveling across text surface
        float sparkle = pow(noise2D(globalUV * 80.0 + vec2(time * 2.0, 0.0)), 16.0) * 0.6;
        boosted += hlTint * sparkle * labels.a;

        color.rgb = mix(color.rgb, boosted, maxCh);
        color.a = max(color.a, maxCh);
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
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

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

    color = compositePrismataLabels(color, fragCoord, bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
