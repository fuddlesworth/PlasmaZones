// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translucent animated gradient panel/popup shader for PhosphorShell.
// Renders an animated two-colour gradient with crystalline (voronoi)
// grain, emitted at `tintOpacity` alpha so the compositor blends it
// over whatever sits behind the surface — no wallpaper sampling.
//
// Two surface modes, selected by whether shadowMargin is set:
//   - Panel mode (shadowMargin == 0): a panel flush with a screen edge.
//     Optional bottom drop-shadow strip (shadowFraction) and concave
//     corner carve (cornerCarveFraction).
//   - Popup mode (shadowMargin > 0): a floating surface oversized by
//     shadowMargin on every side; the visible popup is the inset
//     rounded box and the margin ring is a soft all-around drop shadow.
//
// The rounded-corner / carve SDF and the shadow falloff are factored
// into shared includes (corners.glsl, shadow.glsl) so other shaders can
// reuse them without copying the math. Pre-multiplied alpha output.
//
// customParams[0]: x=speed             y=baseAngle      z=tintOpacity    w=frostAmount
// customParams[1]: x=cornerRadius      y=frostScale
// customParams[2]: x=shadowMargin (px; >0 selects popup mode)
// customParams[3]: x=shadowFraction    y=shadowOpacity
// customParams[4]: x=cornerCarveFraction
//   - shadowFraction: shadowSize / (thickness + shadowSize). Panel-mode
//     ratio of "how much of the surface is the bottom shadow strip".
//   - shadowOpacity: alpha of the shadow at its strongest; fades
//     quadratically to 0. Used by both modes.
//   - cornerCarveFraction: radius of the concave quarter-arc carved
//     into each bottom corner, as a fraction of total surface height
//     (panel mode). The strip shadow follows the carved outline.
// customColors[0]: gradient start color (customColor1 in QML)
// customColors[1]: gradient end color   (customColor2 in QML)

#version 450

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int appField0;
    int appField1;
    vec4 iMouse;
    vec4 iDate;
    vec4 customParams[8];
    vec4 customColors[16];
};

layout(location = 0) out vec4 fragColor;

#include "corners.glsl"
#include "shadow.glsl"

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    float secondMin = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            vec2 point = vec2(hash(cellId), hash(cellId + vec2(127.1, 311.7)));
            vec2 diff = neighbor + point - f;
            float dist = dot(diff, diff);
            if (dist < minDist) {
                secondMin = minDist;
                minDist = dist;
            } else if (dist < secondMin) {
                secondMin = dist;
            }
        }
    }
    return sqrt(secondMin) - sqrt(minDist);
}

// Animated two-colour gradient with crystalline grain, evaluated at
// panel-local UV [0..1]. Reads its inputs straight from the UBO so both
// surface modes can call it with just the local coordinate.
vec3 computeGradient(vec2 panelUv) {
    float speed       = customParams[0].x >= 0.0 ? customParams[0].x : 0.5;
    float angle       = customParams[0].y;
    float frostAmount = customParams[0].w >= 0.0 ? customParams[0].w : 0.1;
    float frostScale  = customParams[1].y > 0.0 ? customParams[1].y : 24.0;
    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    float rotatedAngle = angle + iTime * speed;
    vec2 dir = vec2(cos(rotatedAngle), sin(rotatedAngle));
    float t = dot(panelUv - 0.5, dir) + 0.5;
    t += sin(iTime * speed * 2.7) * 0.55 + sin(iTime * speed * 1.7 + 1.57) * 0.35;
    t = smoothstep(0.0, 1.0, t);
    vec3 gradient = mix(colorA, colorB, t).rgb;

    // Crystalline grain — slow drift, centred on zero so it darkens and
    // lightens symmetrically.
    vec2 frostUv = panelUv * frostScale + vec2(iTime * speed * 0.08, iTime * speed * 0.05);
    float frost = voronoi(frostUv);
    return clamp(gradient + vec3(frost - 0.5) * frostAmount, 0.0, 1.0);
}

void main() {
    if (iResolution.x <= 0.0 || iResolution.y <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;
    // Qt RHI's viewport is Y-up (uv.y=0 at visual BOTTOM); flip to a
    // top-down "surface" coordinate matching QML's QQuickItem space.
    vec2 visualUv = vec2(uv.x, 1.0 - uv.y);

    // Per-param "use default when QML didn't set it" convention:
    //   `>= 0.0` — 0 is meaningful (no tint, no shadow); fallback only
    //              fires for the negative "unset" sentinel.
    //   `> 0.0`  — 0 is invalid (degenerate math); fallback always fires.
    float tintOpacity  = customParams[0].z >= 0.0 ? customParams[0].z : 0.7;
    // Lower-clamp the corner radius at 1.0 — the rounded-box SDF
    // degenerates at r == 0; r=1 looks like "no rounding".
    float radius       = max(1.0, customParams[1].x);
    float shadowMargin = max(customParams[2].x, 0.0);
    float shadowFraction = clamp(customParams[3].x, 0.0, 1.0);
    float shadowOpacity  = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;
    float cornerCarveFraction = clamp(customParams[4].x, 0.0, 0.5);

    // ─── Popup mode: floating surface with an all-around drop shadow ──
    // The surface is oversized by shadowMargin px on every side. The
    // visible popup is the inset rounded box; the margin ring is a soft
    // drop shadow that follows the rounded corners.
    if (shadowMargin > 0.0) {
        vec2 innerHalf = max(iResolution.xy * 0.5 - vec2(shadowMargin), vec2(1.0));
        float sdf = roundedBoxSDF(fragCoord - iResolution.xy * 0.5, innerHalf, radius);
        // Panel coverage: AA'd across the 1-px band just inside the edge.
        float panelMask = 1.0 - smoothstep(-1.0, 0.0, sdf);
        // Shadow rings the box at full strength right at the edge, then
        // attenuated by the panel's own coverage: it vanishes smoothly
        // under the solid interior (no darkening of the popup) yet still
        // backs the edge AA band so the panel fades onto the shadow with
        // neither a transparent seam nor a 1-px alpha step.
        float baseShadowA = (sdf <= 0.0) ? shadowOpacity
                                         : dropShadowAlpha(sdf, shadowMargin, shadowOpacity);
        float shadowA = baseShadowA * (1.0 - panelMask);

        vec3 rgb = vec3(0.0);
        float panelAlpha = 0.0;
        if (panelMask > 0.0) {
            vec2 panelUv = (visualUv * iResolution - vec2(shadowMargin))
                           / max(iResolution - 2.0 * shadowMargin, vec2(1.0));
            panelAlpha = tintOpacity * panelMask;
            rgb = computeGradient(panelUv) * panelAlpha; // pre-multiplied
        }
        // Shadow is pure black — it adds alpha but no colour.
        float a = panelAlpha + shadowA * (1.0 - panelAlpha);
        fragColor = vec4(rgb, a) * qt_Opacity;
        return;
    }

    // ─── Panel mode: edge-flush panel with a bottom shadow strip ──────
    // Split the surface into the visible panel (top) and the drop-shadow
    // strip (bottom). shadowFraction==0 collapses this to "all panel".
    float shadowStartV = 1.0 - shadowFraction;
    float panelBottomYPx = iResolution.y * shadowStartV;

    float surfaceMask = roundedSurfaceMask(fragCoord, iResolution, radius);
    if (surfaceMask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // Signed distance from the panel's (optionally carved) bottom
    // outline. Shared with the shadow so both follow the same curve.
    vec2 vPx = visualUv * iResolution;
    float carveRpx = cornerCarveFraction * iResolution.y;
    float distFromOutline = carvedOutlineDistance(vPx, iResolution, panelBottomYPx, carveRpx);

    // Below the outline: drop-shadow strip (transparent black falloff).
    if (distFromOutline > 0.5) {
        float shadowSizePx = iResolution.y - panelBottomYPx;
        float a = shadowStripAlpha(distFromOutline, shadowSizePx, shadowOpacity) * surfaceMask;
        fragColor = vec4(0.0, 0.0, 0.0, a) * qt_Opacity;
        return;
    }

    // Panel material. `mask` folds surface AA together with a 0.5-px
    // smoothstep across the outline so the carved edge is AA'd against
    // the shadow underneath.
    float outlineFade = 1.0 - smoothstep(-0.5, 0.5, distFromOutline);
    float mask = surfaceMask * outlineFade;

    // Panel-local coords [0..1] so the gradient rotates around the
    // centre of the VISIBLE panel, not the shadow-extended surface.
    vec2 panelUv = vec2(visualUv.x, visualUv.y / max(shadowStartV, 0.001));

    // The gradient is drawn translucently at tintOpacity so the
    // compositor blends it over whatever is behind the surface.
    float finalAlpha = tintOpacity * mask;
    fragColor = vec4(computeGradient(panelUv) * finalAlpha, finalAlpha) * qt_Opacity;
}
