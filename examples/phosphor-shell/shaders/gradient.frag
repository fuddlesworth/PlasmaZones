// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translucent animated gradient panel/popup shader for PhosphorShell.
// Renders an animated two-colour gradient with crystalline (voronoi)
// grain, emitted at `tintOpacity` alpha so the compositor blends it
// over whatever sits behind the surface — no wallpaper sampling.
//
// The rounded-corner / concave-carve masking and the drop-shadow strip
// are factored into shared includes (corners.glsl, shadow.glsl) so
// other shaders can reuse them without copying the SDF math.
// Pre-multiplied alpha output.
//
// customParams[0]: x=speed             y=baseAngle      z=tintOpacity    w=frostAmount
// customParams[1]: x=cornerRadius      y=frostScale
// customParams[3]: x=shadowFraction    y=shadowOpacity
// customParams[4]: x=cornerCarveFraction
//   - shadowFraction: shadowSize / (thickness + shadowSize). DPR-
//     independent ratio of "how much of the surface is shadow". 0
//     disables the shadow.
//   - shadowOpacity: alpha of the shadow at its strongest (right below
//     the panel edge); fades quadratically to 0 at the far end.
//   - cornerCarveFraction: radius of the concave quarter-arc carved
//     into each bottom corner, as a fraction of total surface height.
//     0 disables the carve. The drop-shadow follows the same carved
//     outline (see corners.glsl / shadow.glsl).
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

void main() {
    if (iResolution.x <= 0.0 || iResolution.y <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    // Per-param "use default when QML didn't set it" convention:
    //   `>= 0.0`  — 0 is a meaningful value (no tint, no frost, …),
    //              so the fallback fires only for negative sentinels.
    //   `> 0.0`   — 0 is invalid (divide-by-zero / degenerate math),
    //              fallback fires to keep the shader producing valid output.
    float speed        = customParams[0].x >= 0.0 ? customParams[0].x : 0.5;
    float angle        = customParams[0].y;
    float tintOpacity  = customParams[0].z >= 0.0 ? customParams[0].z : 0.7;
    float frostAmount  = customParams[0].w >= 0.0 ? customParams[0].w : 0.1;
    // Lower-clamp at 1.0 — the rounded-box SDF degenerates at r == 0;
    // r=1 is visually indistinguishable from "no rounding".
    float radius       = max(1.0, customParams[1].x);
    float frostScale   = customParams[1].y > 0.0 ? customParams[1].y : 24.0;
    float shadowFraction = clamp(customParams[3].x, 0.0, 1.0);
    float shadowOpacity  = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;
    // Hard-clamp the carve to 0..0.5 so it can never eat the whole panel.
    float cornerCarveFraction = clamp(customParams[4].x, 0.0, 0.5);

    // Convert from Qt RHI's Y-up viewport coords (uv.y=0 at visual
    // BOTTOM) to top-down "panel surface" coords (visualUv.y=0 at visual
    // TOP) matching QML's QQuickItem coord system. The Y-asymmetric
    // logic below (shadow split, corner carve, panel-local gradient
    // remap) uses visualUv.
    vec2 visualUv = vec2(uv.x, 1.0 - uv.y);

    // Split the surface into the visible panel region (top) and the
    // drop-shadow strip (bottom). shadowFraction==0 collapses this to
    // "everything is panel".
    float shadowStartV = 1.0 - shadowFraction;
    float panelBottomYPx = iResolution.y * shadowStartV;

    // Antialiased rounded-box mask for the whole surface.
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

    // ─── Animated gradient ────────────────────────────────────────────
    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    float rotatedAngle = angle + iTime * speed * 1.0;
    vec2 dir = vec2(cos(rotatedAngle), sin(rotatedAngle));
    float t = dot(panelUv - 0.5, dir) + 0.5;
    float wave1 = sin(iTime * speed * 2.7) * 0.55;
    float wave2 = sin(iTime * speed * 1.7 + 1.57) * 0.35;
    t += wave1 + wave2;
    t = smoothstep(0.0, 1.0, t);
    vec3 gradient = mix(colorA, colorB, t).rgb;

    // Crystalline grain — slow drift on sample coords, centred around
    // zero so it darkens and lightens symmetrically.
    vec2 frostUv = panelUv * frostScale + vec2(iTime * speed * 0.08, iTime * speed * 0.05);
    float frost = voronoi(frostUv);
    gradient = clamp(gradient + vec3(frost - 0.5) * frostAmount, 0.0, 1.0);

    // ─── Composite ────────────────────────────────────────────────────
    // The gradient is drawn translucently at tintOpacity so the
    // compositor blends it over whatever is behind the surface.
    // Pre-multiplied alpha output.
    float finalAlpha = tintOpacity * mask;
    fragColor = vec4(gradient * finalAlpha, finalAlpha) * qt_Opacity;
}
