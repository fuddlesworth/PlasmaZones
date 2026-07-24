// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <audio.glsl>

/*
 * NEON VENOM — Organic Bioluminescent Overlay
 * Inspired by VEX / The Toxictress / Neon Venom album
 *
 * Organic venomous veins pulse with bioluminescent light through acid pools.
 * Rising toxic bubbles, venomous mist, and living vein networks create a
 * biological/organic feel distinct from digital circuit aesthetics.
 *
 * Parameters:
 *   p_veinScale = veinScale         — Vein network scale
 *   p_veinSpeed = veinSpeed         — Pulse animation speed (all effects)
 *   p_veinSharpness = veinSharpness     — How sharp/defined veins are
 *   p_veinWarp = veinWarp          — Domain warp intensity
 *   p_poolIntensity = poolIntensity     — Acid pool surface effect
 *   p_bubbleCount = bubbleCount       — Number of rising bubbles
 *   p_bubbleSpeed = bubbleSpeed       — Bubble rise speed
 *   p_fillOpacity = fillOpacity       — Zone fill opacity
 *   p_glowStrength = glowStrength      — Neon glow intensity
 *   p_mistDensity = mistDensity       — Atmospheric venom mist
 *   p_audioReactivity = audioReactivity   — Audio response strength
 *   p_sparkIntensity = sparkIntensity    — Audio spark strength
 *   p_labelGlowSpread = labelGlowSpread
 *   p_labelBrightness = labelBrightness
 *   p_labelAudioReact = labelAudioReact
 *   p_edgeGlow = edgeGlow
 *   p_warpOctaves = warpOctaves
 *   p_showLabels = showLabels
 *   p_poolSpeed = poolSpeed         — Pool/caustic animation speed
 *   p_surgeThreshold = surgeThreshold    — Bass level to trigger vein surge
 *   p_mouseInfluence = mouseInfluence    — Cursor interaction strength
 *   p_bubbleSize = bubbleSize        — Bubble size multiplier
 *   p_veinFineDetail = veinFineDetail    — Fine vein detail blend
 *   p_venomColor   — Venom green (default #39FF14)
 *   p_acidColor   — Acid purple (default #BF00FF)
 *   p_glowColor   — Glow highlight (default #CCFF00)
 *   p_mistColor   — Mist tint (default #0D001A)
 */

// ═══════════════════════════════════════════════════════════════════════
// FBM noise with domain warping for organic vein networks
// ═══════════════════════════════════════════════════════════════════════

float fbm(vec2 p, int octaves) {
    float f = 0.0;
    float amp = 0.5;
    mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    // Capped at 8 like common.glsl's shared fbm(). `octaves` comes straight
    // from a user parameter, and this runs on the compositor path.
    for (int i = 0; i < octaves && i < 8; i++) {
        f += amp * noise2D(p);
        p = rot * p * 2.0;
        amp *= 0.5;
    }
    return f;
}

// Domain-warped FBM: creates organic branching vein patterns
float warpedFbm(vec2 p, float warpAmount, int octaves, float t) {
    vec2 q = vec2(
        fbm(p + vec2(0.0, 0.0) + t * 0.1, octaves),
        fbm(p + vec2(5.2, 1.3) - t * 0.08, octaves)
    );
    vec2 r = vec2(
        fbm(p + 4.0 * q + vec2(1.7, 9.2) + t * 0.15, octaves),
        fbm(p + 4.0 * q + vec2(8.3, 2.8) - t * 0.12, octaves)
    );
    return fbm(p + warpAmount * 4.0 * r, octaves);
}

// ═══════════════════════════════════════════════════════════════════════
// Vein network: creates branching organic vein patterns via ridged noise
// ═══════════════════════════════════════════════════════════════════════

float veinPattern(vec2 uv, float scale, float sharpness, float warp,
                  int octaves, float t, float fineDetail) {
    vec2 p = uv * scale;
    float w = warpedFbm(p, warp, octaves, t);

    // Ridged noise: 1 - abs(noise) creates vein-like ridges
    float ridge = 1.0 - abs(w * 2.0 - 1.0);
    ridge = pow(ridge, sharpness);

    // Secondary finer veins (strength configurable)
    float fine = 1.0 - abs(fbm(p * 2.3 + t * 0.2, octaves) * 2.0 - 1.0);
    fine = pow(fine, sharpness * 1.5) * fineDetail;

    return clamp(ridge + fine, 0.0, 1.0);
}

// ═══════════════════════════════════════════════════════════════════════
// Acid pool: caustic-like surface distortion (speed configurable)
// ═══════════════════════════════════════════════════════════════════════

float acidPool(vec2 uv, float t, float intensity, float speed) {
    if (intensity <= 0.0) return 0.0;
    float st = t * speed;
    vec2 p = uv * 6.0;
    float c1 = sin(p.x * 3.1 + st * 0.7) * cos(p.y * 2.7 - st * 0.5);
    float c2 = sin(p.x * 2.3 - st * 0.4) * cos(p.y * 3.5 + st * 0.6);
    float c3 = noise2D(p * 1.5 + st * 0.3);
    float caustic = abs(c1 + c2) * 0.5;
    caustic = pow(caustic, 1.5) + c3 * 0.15;
    return caustic * intensity;
}

// ═══════════════════════════════════════════════════════════════════════
// Rising toxic bubbles (size configurable)
// ═══════════════════════════════════════════════════════════════════════

float toxicBubble(vec2 uv, vec2 center, float radius) {
    float d = length(uv - center);
    // Soft glowing sphere with bright rim and inner refraction highlight
    float outer = smoothstep(radius * 1.1, radius, d);
    float rim = smoothstep(radius * 0.9, radius * 0.7, d)
              * (1.0 - smoothstep(radius * 0.7, radius * 0.4, d));
    // Specular highlight offset toward top-left
    float spec = smoothstep(radius * 0.5, radius * 0.15,
                            length(uv - center + radius * vec2(0.25, 0.3)));
    return outer * 0.3 + rim * 0.8 + spec * 0.6;
}

float risingBubbles(vec2 uv, float t, int count, float speed,
                    float bass, float sizeMul) {
    if (count <= 0) return 0.0;
    float bubbles = 0.0;
    for (int i = 0; i < count && i < 20; i++) {
        float fi = float(i);
        float phase = hash11(fi * 17.3) * TAU;
        float xOff = hash11(fi * 31.7) * 0.8 + 0.1;
        float ySpeed = (0.5 + hash11(fi * 43.1) * 0.5) * speed;
        // Size: base * user multiplier, bass makes them swell
        float radius = (0.015 + hash11(fi * 7.9) * 0.025) * sizeMul
                      * (1.0 + bass * 0.5);

        // Y position: rises and wraps
        float y = fract(-t * ySpeed * 0.3 + hash11(fi * 53.0));
        // Wider wobble
        float x = xOff + sin(t * 1.5 + phase) * 0.06;

        bubbles += toxicBubble(uv, vec2(x, y), radius);
    }
    return clamp(bubbles, 0.0, 1.0);
}

// ═══════════════════════════════════════════════════════════════════════
// Venomous mist / atmospheric haze (speed follows veinSpeed)
// ═══════════════════════════════════════════════════════════════════════

float venomMist(vec2 uv, float t, float density) {
    if (density <= 0.0) return 0.0;
    vec2 p = uv * 3.0;
    float m = fbm(p + vec2(t * 0.05, t * 0.03), 4);
    float m2 = fbm(p * 1.5 + vec2(-t * 0.04, t * 0.06), 3);
    // Mist rises from bottom
    float gradient = smoothstep(0.0, 0.6, 1.0 - uv.y);
    return (m * 0.6 + m2 * 0.4) * gradient * density;
}

// ═══════════════════════════════════════════════════════════════════════
// Mouse interaction: veins intensify near cursor
// ═══════════════════════════════════════════════════════════════════════

float mouseProximity(vec2 fragCoord, float influence) {
    if (influence <= 0.0) return 0.0;
    // iMouse.xy = pixel coords, iMouse.zw = normalized 0-1
    vec2 mousePos = iMouse.xy;
    if (mousePos.x < 0.0 || mousePos.y < 0.0) return 0.0;
    float d = length(fragCoord - mousePos);
    // zoneLen(), not pxScale(): this radius is measured against fragCoord in
    // the same device-px space as the rim falloff it ends up scaling.
    float radius = zoneLen(120.0);
    return smoothstep(radius, 0.0, d) * influence;
}

// ═══════════════════════════════════════════════════════════════════════
// Zone rendering
// ═══════════════════════════════════════════════════════════════════════

vec4 renderNeonVenomZone(
    vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
    bool isHighlighted,
    float veinScale, float veinSpeed, float veinSharpness, float veinWarp,
    float poolIntensity, int bubbleCount, float bubbleSpeed,
    float fillOpacity, float glowStr, float mistDensity,
    float audioReact, float sparkIntensity, float edgeGlow, int warpOctaves,
    float poolSpeed, float surgeThreshold, float mouseInfluence,
    float bubbleSize, float veinFineDetail,
    vec3 venomCol, vec3 acidCol, vec3 glowCol, vec3 mistCol,
    float bass, float mids, float treble
) {
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    // This pack used to scale by pxScale() (a 1080p-relative factor) rather
    // than the display scale, so its corners tracked resolution instead of
    // DPI and diverged from both its sibling packs and the decorations.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);
    float borderWidth = zoneBorderWidth(params.y);
    float sdf = zoneShape.d;

    float vitality = zoneVitality(isHighlighted);
    float t = iTime * veinSpeed;

    // Audio modulation — strong, punchy response
    float audioPulse = bass * audioReact * vitality;
    float audioMid = mids * audioReact * vitality;
    float audioShimmer = treble * audioReact * vitality;

    // Mouse proximity — intensifies veins and glow near cursor
    float mouseFx = mouseProximity(fragCoord, mouseInfluence) * vitality;

    // Early-out, placed after the audio and mouse terms because the rim glow's
    // reach depends on them. The rim below decays over rimFalloff, so bound at
    // four falloff lengths and the border band, which leaves the rim at ~2%
    // where it is cut. The old bound was a flat 60 logical px, sized for the
    // resting falloff of 10. audioReactivity (max 2) and mouseInfluence (max 4)
    // push rimFalloff to 80, where a bound of 60 sliced the gradient at ~47%
    // and left a hard ring. These terms are a few cheap ALU ops, well below the
    // vein FBM the early-out protects. rimFalloff is zoneLen() so it shares a
    // basis with borderWidth in the bound below, which zoneBorderWidth() scales.
    float rimFalloff = zoneLen(10.0 + audioPulse * 15.0 + mouseFx * 10.0);
    if (sdf > borderWidth * 2.0 + rimFalloff * 4.0) return vec4(0.0);

    // ─── Vein network ────────────────────────────────────────────────
    // Bass + mouse warp the vein domain for a breathing/throbbing effect
    float warpWithAudio = veinWarp + audioPulse * 0.3 + mouseFx * 0.15;
    float veins = veinPattern(localUV, veinScale, veinSharpness, warpWithAudio,
                              warpOctaves, t, veinFineDetail);
    // Dramatic pulse: veins breathe 40-140% intensity with bass and sine
    float pulse = 0.4 + 0.6 * (0.5 + 0.5 * sin(t * 3.0 + localUV.x * 4.0 + localUV.y * 3.0));
    veins *= pulse + audioPulse * 0.8 + mouseFx * 0.4;

    // ─── Acid pool caustics (speed from poolSpeed param) ─────────────
    // Pools react to mids — caustics intensify with audio
    float poolAudio = poolIntensity * vitality * (1.0 + audioMid * 0.6);
    float pool = acidPool(localUV, iTime, poolAudio, poolSpeed);

    // ─── Rising bubbles (speed follows veinSpeed, size from param) ───
    float bubbles = risingBubbles(localUV, iTime * veinSpeed, bubbleCount,
                                  bubbleSpeed, bass, bubbleSize) * vitality;

    // ─── Venom mist (speed follows veinSpeed) ───────────────────────
    float mist = venomMist(localUV, iTime * veinSpeed, mistDensity * vitality);

    // ─── Compose color ──────────────────────────────────────────────
    // Base: blend zone fill color with mist tint (fill color has real influence)
    vec3 baseCol = zoneFillHue(fillColor) * 0.3 + mistCol * 0.35;

    // Veins: green-purple gradient, shifts toward glow on treble hits
    float veinHue = veins * 0.5 + sin(t * 1.5 + localUV.y * 3.0) * 0.3;
    vec3 veinCol = mix(venomCol, acidCol, veinHue);
    veinCol = mix(veinCol, glowCol, audioShimmer * 0.6 * veins);

    // Layer composition
    vec3 col = baseCol;
    col += pool * mix(acidCol, venomCol, 0.3) * 0.5;          // Acid pools
    col += veins * veinCol * glowStr * vitality;               // Glowing veins
    col += mist * mistCol * 2.0;                               // Atmospheric mist
    col += bubbles * mix(glowCol, venomCol, 0.3) * vitality;   // Bubbles glow

    // Mouse glow: venom bloom near cursor
    if (mouseFx > 0.01) {
        col += mouseFx * venomCol * 0.5;
    }

    // Audio sparks along veins — smooth noise
    if (sparkIntensity > 0.0 && audioPulse > 0.05) {
        float sparkNoise = noise2D(localUV * 25.0 + iTime * 6.0 * veinSpeed);
        float sparkThreshold = 0.7 - audioPulse * 0.15;
        float spark = smoothstep(sparkThreshold, sparkThreshold + 0.1, sparkNoise) * veins;
        col += spark * glowCol * sparkIntensity * audioPulse;
    }

    // Bass surge — veins flare bright, surrounding tissue gets a color wash
    if (audioPulse > surgeThreshold) {
        float surge = (audioPulse - surgeThreshold) / max(1.0 - surgeThreshold, 0.01);
        surge *= surge; // quadratic for punchy onset
        // Veins flare white-hot, surrounding tissue gets a color wash
        float veinFlare = veins * surge * 1.5;
        float tissueWash = (1.0 - veins) * surge * 0.3;
        col += veinFlare * mix(glowCol, vec3(1.0), surge * 0.5);
        col += tissueWash * venomCol;
    }

    col = vitalityDesaturate(col, vitality);

    // ─── Zone shape masking ─────────────────────────────────────────
    float inside = smoothstep(1.0, -1.0, sdf);
    float borderFactor = softBorder(sdf, borderWidth);

    // Edge glow (bioluminescent rim) — pulses strongly with bass + mouse
    float rimDist = abs(sdf);
    // rimFalloff is computed up at the early-out, which bounds itself by this
    // same reach — keep it one definition so the two cannot drift apart.
    float rim = exp(-rimDist / rimFalloff) * edgeGlow * vitality;
    col += rim * mix(venomCol, glowCol, audioPulse * 0.5) * (1.0 + audioPulse);

    // Border: blend zone borderColor with venom palette (borderColor has real influence)
    vec3 borderTint = mix(venomCol, acidCol, sin(t * 1.5) * 0.3 + 0.5 + audioMid * 0.3);
    vec3 borderFinal = mix(borderTint, borderColor.rgb, 0.3) * (1.0 + audioPulse * 0.6);
    col = mix(col, borderFinal * glowStr, borderFactor);

    // The pack's own fillOpacity is the sole fill alpha, catalog-wide.
    // The zone's activeOpacity arrives in fillColor.a, but only four packs
    // ever multiplied it in, so it was inert in the other 23 and the split
    // just made the same setting behave differently per pack.
    float alpha = inside * fillOpacity;
    alpha = max(alpha, borderFactor * borderColor.a);
    alpha = max(alpha, rim * 0.5);

    return vec4(col, alpha);
}

vec4 pImage(vec2 fragCoord) {
    // ─── Read parameters ────────────────────────────────────────────
    float veinScale      = p_veinScale;
    float veinSpeed      = p_veinSpeed;
    float veinSharpness  = p_veinSharpness;
    float veinWarp       = p_veinWarp;
    float poolIntensity  = p_poolIntensity;
    int   bubbleCount    = int(p_bubbleCount);
    float bubbleSpeed    = p_bubbleSpeed;
    float fillOpacity    = p_fillOpacity;
    float glowStr        = p_glowStrength;
    float mistDensity    = p_mistDensity;
    float audioReact     = p_audioReactivity;
    float sparkIntensity = p_sparkIntensity;
    float labelSpread    = p_labelGlowSpread;
    float labelBright    = p_labelBrightness;
    float labelReact     = p_labelAudioReact;
    float edgeGlow       = p_edgeGlow;
    int   warpOctaves    = int(p_warpOctaves);
    bool  showLabels     = p_showLabels > 0.5;
    float poolSpeed      = p_poolSpeed;
    float surgeThreshold = p_surgeThreshold;
    float mouseInfluence = p_mouseInfluence;
    float bubbleSize     = p_bubbleSize;
    float veinFineDetail = p_veinFineDetail;

    vec3 venomCol = p_venomColor.rgb;
    vec3 acidCol  = p_acidColor.rgb;
    vec3 glowCol  = p_glowColor.rgb;
    vec3 mistCol  = p_mistColor.rgb;

    // Audio bands
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();

    vec4 result = vec4(0.0);

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect       = zoneRects[i];
        // Skip degenerate rects, as every sibling pack's loop does. A zero-size
        // zone collapses zoneSdf() to a point, and the rim glow around it still
        // renders, so an unguarded loop paints a stray blob where the others
        // draw nothing.
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        vec4 fillColor  = zoneFillColors[i];
        vec4 borderCol  = zoneBorderColors[i];
        vec4 params     = zoneParams[i];
        bool highlighted = params.z > 0.5;

        vec4 zone = renderNeonVenomZone(
            fragCoord, rect, fillColor, borderCol, params, highlighted,
            veinScale, veinSpeed, veinSharpness, veinWarp,
            poolIntensity, bubbleCount, bubbleSpeed,
            fillOpacity, glowStr, mistDensity,
            audioReact, sparkIntensity, edgeGlow, warpOctaves,
            poolSpeed, surgeThreshold, mouseInfluence,
            bubbleSize, veinFineDetail,
            venomCol, acidCol, glowCol, mistCol,
            bass, mids, treble
        );

        result = blendOver(result, zone);
    }

    // ─── Labels: Venomous Bioluminescent Tubes ─────────────────────
    if (showLabels) {
        vec2 luv = labelsUv(fragCoord);
        vec2 texelSize = 1.0 / max(iResolution, vec2(1.0));
        vec4 labels = texture(uZoneLabels, luv);
        float spread = labelSpread * pxScale();
        float t = iTime * p_veinSpeed; // veinSpeed

        bool hasAudio = iAudioSpectrumSize > 0;
        float bassMod = hasAudio ? bass * labelReact : 0.0;
        float trebleMod = hasAudio ? treble * labelReact : 0.0;

        // ── Multi-layer halo sampling ────────────────────────────────
        float haloTight = 0.0, haloWide = 0.0, haloVWide = 0.0;
        float haloR = 0.0, haloG = 0.0, haloB = 0.0;
        // Chromatic offset: green/purple venom split
        vec2 chromOff = vec2(texelSize.x * 2.5, texelSize.y * 0.5);
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                vec2 off = vec2(float(dx), float(dy)) * texelSize;
                float r2 = float(dx * dx + dy * dy);
                float wTight = exp(-r2 * 0.5);
                float wWide = exp(-r2 * 0.2);
                float wVWide = exp(-r2 * 0.1);

                float s = texture(uZoneLabels, luv + off * spread).a;
                haloTight += s * wTight;
                haloWide += s * wWide;
                haloVWide += s * wVWide;

                // No haloG fetch: green samples the same texel with the same
                // weight as haloWide above, so it is copied out after the loop.
                haloR += texture(uZoneLabels, luv + off * spread + chromOff).a * wWide;
                haloB += texture(uZoneLabels, luv + off * spread - chromOff).a * wWide;
            }
        }
        haloTight /= 10.0;
        haloWide /= 16.5;
        haloVWide /= 22.0;
        // The chroma channels accumulate on the same wWide kernel as haloWide,
        // so they need the same divisor. Left raw they carried the full weight
        // sum (~16.5x) into the additive bloom below and bleached every label
        // halo to a flat clamped colour. The shared gatherLabelHalo() this loop
        // duplicates divides by 16.5 for exactly this reason.
        haloR /= 16.5;
        haloG = haloWide;  // same texel, same weight, same divisor
        haloB /= 16.5;

        // Bioluminescent flicker: organic irregular pulsing
        float bioFlicker = 0.8 + 0.12 * sin(t * 5.7 + luv.x * 20.0)
                              + 0.08 * sin(t * 3.1 + luv.y * 15.0);
        bioFlicker *= (1.0 + bassMod * 0.4);

        if (haloWide > 0.003) {
            float haloEdge = haloWide * (1.0 - labels.a);
            float haloEdgeTight = haloTight * (1.0 - labels.a);
            float haloEdgeVWide = haloVWide * (1.0 - labels.a);

            // ── Vein tendrils radiating from label edges ─────────────
            float angle = atan(luv.y - 0.5, luv.x - 0.5);
            float tendrilNoise = noise2D(vec2(angle * 6.0, haloEdge * 10.0 + t * 0.4));
            float tendril = smoothstep(0.3, 0.75, tendrilNoise) * haloEdgeVWide;
            vec3 tendrilCol = mix(venomCol, acidCol, 0.5 + 0.5 * sin(angle * 2.0 + t));
            result.rgb += tendrilCol * tendril * 0.6 * bioFlicker;

            // ── Toxic inner glow: tight hot-green core ───────────────
            vec3 coreGlow = mix(venomCol, glowCol, 0.3) * 1.2;
            result.rgb += coreGlow * haloEdgeTight * 0.7 * bioFlicker;

            // ── Chromatic venom bloom: green/purple split channels ────
            vec3 chromHalo = vec3(haloR, haloG, haloB) * (1.0 - labels.a);
            vec3 chromCol = chromHalo * mix(venomCol, acidCol, 0.4) * 0.5 * bioFlicker;
            chromCol.r *= venomCol.r * 1.3;
            chromCol.b *= acidCol.b * 1.3;
            result.rgb += chromCol;

            // ── Wide atmospheric venom haze ───────────────────────────
            vec3 hazeCol = mix(venomCol * 0.4, acidCol * 0.3, 0.5 + 0.5 * sin(t * 0.3));
            result.rgb += hazeCol * haloEdgeVWide * 0.3 * bioFlicker;

            // ── Acid drip: venom bleeding downward from text ─────────
            float dripSample = texture(uZoneLabels, luv + vec2(0.0, -texelSize.y * spread * 5.0)).a;
            float drip = dripSample * (1.0 - labels.a) * 0.4;
            if (drip > 0.01) {
                float dripNoise = noise2D(vec2(luv.x * 40.0, t * 0.5));
                vec3 dripCol = mix(venomCol, acidCol, dripNoise * 0.6) * drip;
                float dripFade = smoothstep(0.0, 0.15, luv.y);
                result.rgb += dripCol * dripFade * bioFlicker;
            }

            // ── Bass sparks along halo edge ──────────────────────────
            if (hasAudio && bass > 0.1) {
                float sparkNoise = noise2D(luv * 60.0 + t * 4.0);
                float spark = smoothstep(0.6, 0.9, sparkNoise) * bass * 2.0 * labelReact;
                result.rgb += glowCol * haloEdge * spark * bioFlicker;
            }

            result.a = max(result.a, haloEdge * 0.5);
        }

        // ── Label text body: venomous bioluminescent tubes ───────────
        if (labels.a > 0.01) {
            // Color sweep: venom green ↔ acid purple cycling through each character
            float venomWave = sin(fragCoord.x * 0.15 - t * 3.0 + fragCoord.y * 0.08) * 0.5 + 0.5;
            vec3 tubeColor = mix(venomCol, acidCol, venomWave * 0.4);
            // Add glow highlight at peaks
            tubeColor = mix(tubeColor, glowCol, pow(venomWave, 3.0) * 0.3);

            // Internal vein texture within the text characters
            float veinTex = noise2D(luv * 80.0 + t * 0.8);
            tubeColor = mix(tubeColor, glowCol * 1.2, veinTex * 0.25);

            // Stroke edge rim: bioluminescent tubes glow brightest at edges
            float aL = texture(uZoneLabels, luv + vec2(-texelSize.x, 0.0)).a;
            float aR = texture(uZoneLabels, luv + vec2( texelSize.x, 0.0)).a;
            float aU = texture(uZoneLabels, luv + vec2(0.0, -texelSize.y)).a;
            float aD = texture(uZoneLabels, luv + vec2(0.0,  texelSize.y)).a;
            float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

            // Combine: venom tube body + bright white-green rim
            vec3 textCol = tubeColor * 0.65 + mix(glowCol, vec3(1.0), 0.4) * rim * 0.55;
            textCol *= labelBright * bioFlicker;
            textCol *= (1.0 + bassMod * 0.5);

            // Treble: acid corrosion flashes across text
            if (hasAudio && treble > 0.1) {
                float corrode = step(0.82, fract(fragCoord.y * 0.12 + t * 6.0));
                textCol = mix(textCol, acidCol * labelBright * 1.8, corrode * trebleMod * 0.5);
            }

            // Gentle tonemap to prevent blowout
            textCol = textCol / (0.5 + textCol);

            result.rgb = mix(result.rgb, textCol, labels.a);
            result.a = max(result.a, labels.a);
        }
    }

    return result;
}
