// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translucent animated gradient panel/popup shader for PhosphorShell.
// Renders an animated two-colour gradient with crystalline (voronoi)
// grain, emitted at `tintOpacity` alpha so the compositor blends it
// over whatever sits behind the surface — no wallpaper sampling. SDF
// rounded-box mask with a hard cutoff to avoid fringing; an optional
// drop-shadow strip and concave corner carve for top-edge panels.
// Pre-multiplied alpha output.
//
// customParams[0]: x=speed             y=baseAngle      z=tintOpacity    w=frostAmount
// customParams[1]: x=cornerRadius      y=frostScale
// customParams[3]: x=shadowFraction    y=shadowOpacity
// customParams[4]: x=cornerCarveFraction
//   - cornerCarveFraction: radius of the concave quarter-arc carved
//     into each bottom corner of the visible panel, expressed as a
//     fraction of total surface height (DPR-independent). 0 disables
//     the carve. Each carve replaces the panel's straight bottom-
//     corner with a quarter-circle whose center is INWARD by
//     `carveRadius` pixels in both axes. The drop-shadow follows the
//     SAME curve — the shader computes a single signed-distance field
//     from the carved outline and uses it for both panel rendering and
//     shadow falloff, so the corner curve doesn't leave a rectangular
//     shadow strip behind. Produces the Quickshell/Noctalia "desktop
//     wraps around the panel" look.
//   - shadowFraction: shadowSize / (thickness + shadowSize). DPR-
//     independent ratio of "how much of the surface is shadow". 0
//     disables the shadow. The C++ side (PhosphorShell::PanelWindow::
//     shadowSize + ShellEngine::materializePanels) is responsible for
//     allocating the extra surface space; this param just tells the
//     shader where the panel-to-shadow split lives.
//   - shadowOpacity: alpha of the shadow at its strongest (right
//     below the panel edge); fades quadratically to 0 at the far end
//     of the strip.
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

float roundedBoxSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) - r;
}

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
    // Lower-clamp at 1.0: the standard rounded-box SDF
    //   length(max(abs(p) - b + r, 0)) - r
    // degenerates at r == 0 — interior fragments return SDF=0 (not
    // negative), so the `1 - smoothstep(-1, 0, dist)` mask zeroes the
    // whole panel out. r=1 gives a sharp-looking corner at one pixel
    // (visually indistinguishable from "no rounding" at typical panel
    // thickness) and keeps the SDF properly negative on the interior.
    float radius       = max(1.0, customParams[1].x);
    float frostScale   = customParams[1].y > 0.0 ? customParams[1].y : 24.0;
    // shadowFraction: ratio of the surface that's the shadow strip
    // (0..1). 0 disables the shadow branch entirely.
    float shadowFraction = clamp(customParams[3].x, 0.0, 1.0);
    float shadowOpacity  = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;
    // cornerCarveFraction: radius of the concave corner carve as a
    // fraction of surface height. Hard-clamp to 0..0.5 so the carve
    // can never eat the whole panel.
    float cornerCarveFraction = clamp(customParams[4].x, 0.0, 0.5);

    // Convert from Qt RHI's Y-up viewport coords (uv.y=0 at visual
    // BOTTOM, uv.y=1 at visual TOP) to top-down "panel surface" coords
    // (visualUv.y=0 at visual TOP, visualUv.y=1 at visual BOTTOM)
    // matching QML's QQuickItem coord system. The Y-asymmetric logic
    // below (shadow split, corner carve, panel-local gradient remap)
    // uses visualUv — without the flip the shadow strip would render
    // at the visual TOP of the surface instead of the bottom.
    //
    // Symmetric logic (SDF rounded-box mask, gradient direction
    // rotation centred on 0.5, voronoi grain) can use either uv or
    // visualUv — they're identical under the y-flip.
    vec2 visualUv = vec2(uv.x, 1.0 - uv.y);

    // Split the surface into the visible panel region (top) and the
    // drop-shadow strip (bottom). shadowFraction==0 collapses this to
    // "everything is panel" — shadowStartV becomes 1.0 and the shadow
    // branch never fires.
    float shadowStartV = 1.0 - shadowFraction;
    float panelBottomYPx = iResolution.y * shadowStartV;

    // SDF rounded rectangle mask for the SURFACE — gives AA at all
    // four outer edges of the wl_surface. Cheap to always evaluate.
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);
    float surfaceMask = 1.0 - smoothstep(-1.0, 0.0, dist);
    if (surfaceMask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // ─── Distance from the carved panel outline ───────────────────────
    // Single signed-distance field for the panel's bottom edge,
    // measured in pixels. Negative => inside panel. Positive => below
    // the outline (shadow region). Crucially this is a CONTINUOUS
    // field across the bottom corners: in the middle the outline is
    // the horizontal line y=panelBottomYPx, and in each corner column
    // the outline becomes the quarter-arc of radius `carveRpx`
    // centred inward. The shadow rendering reads this same value so
    // the shadow follows the curve at the corners instead of leaving
    // a rectangular strip behind a curved panel edge.
    vec2 vPx = visualUv * iResolution;
    float carveRpx = cornerCarveFraction * iResolution.y;
    float distFromOutline;
    if (carveRpx > 0.5 && vPx.y > panelBottomYPx - carveRpx) {
        // Bottom-left arc
        if (vPx.x < carveRpx) {
            vec2 arcCenter = vec2(carveRpx, panelBottomYPx - carveRpx);
            distFromOutline = length(vPx - arcCenter) - carveRpx;
        }
        // Bottom-right arc
        else if (vPx.x > iResolution.x - carveRpx) {
            vec2 arcCenter = vec2(iResolution.x - carveRpx, panelBottomYPx - carveRpx);
            distFromOutline = length(vPx - arcCenter) - carveRpx;
        }
        // Middle column — flat bottom edge
        else {
            distFromOutline = vPx.y - panelBottomYPx;
        }
    } else {
        // Above the carve band (or carve disabled): always the flat
        // horizontal bottom edge.
        distFromOutline = vPx.y - panelBottomYPx;
    }

    // ─── Shadow region (below the carved outline) ─────────────────────
    // distFromOutline > 0.5 — fully outside the panel, can skip the
    // expensive panel-material work. The 0.5-px slack lets the panel
    // branch handle outline AA on the inside.
    float shadowSizePx = iResolution.y - panelBottomYPx;
    if (distFromOutline > 0.5) {
        if (shadowFraction <= 0.001 || shadowSizePx <= 0.0 || distFromOutline >= shadowSizePx) {
            fragColor = vec4(0.0);
            return;
        }
        float t = distFromOutline / shadowSizePx;
        float falloff = 1.0 - t;
        falloff *= falloff;
        float a = shadowOpacity * falloff * surfaceMask;
        fragColor = vec4(0.0, 0.0, 0.0, a) * qt_Opacity;
        return;
    }

    // Save mask for the panel branch below — both surface AA and a
    // 0.5-px smoothstep across the outline so the curved edge is AA'd
    // against the shadow underneath.
    float outlineFade = 1.0 - smoothstep(-0.5, 0.5, distFromOutline);
    float mask = surfaceMask * outlineFade;

    // Remap visualUv.y to "panel-local" coordinates [0..1] so the
    // gradient direction rotates around the centre of the VISIBLE
    // panel rather than the centre of the (shadow-extended) surface.
    // visualUv.y is already in top-down convention, so the panel zone
    // is [0, shadowStartV] and dividing by shadowStartV gives a clean
    // 0..1 panel-local range.
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
    // compositor blends it over whatever is behind the surface. `mask`
    // folds in surface AA and the carved-outline AA, so the material
    // fades cleanly to 0 across the curved edge and the shadow region
    // below picks up from there. Pre-multiplied alpha output.
    float finalAlpha = tintOpacity * mask;
    fragColor = vec4(gradient * finalAlpha, finalAlpha) * qt_Opacity;
}
