// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translucent animated gradient panel shader for PhosphorShell.
// Samples the desktop wallpaper at SRB binding 11 (uWallpaper) when
// available, applies a 9-tap blur, and overlays an animated two-colour
// gradient on top. When no wallpaper is bound (textureSize <= 1×1
// transparent fallback) the gradient renders alone with tintOpacity
// alpha so the compositor's behind-color shows. Pre-multiplied alpha
// output. SDF mask uses hard cutoff to avoid fringe.
//
// customParams[0]: x=speed           y=baseAngle      z=tintOpacity    w=frostAmount
// customParams[1]: x=cornerRadius    y=frostScale
// customParams[2]: x=panelToScreenH  y=blurRadius
// customParams[3]: x=shadowFraction  y=shadowOpacity
//   - panelToScreenH: DPR-INDEPENDENT ratio of the panel's TOTAL
//     surface height (visible + shadow strip) over the screen's
//     height, both measured in the SAME unit system (logical or
//     physical — the ratio cancels DPR). The shader uses this
//     directly to compute the wallpaper-UV y position from the
//     panel's surface-local UV. Passing a physical-pixel screen
//     height with a DPR-adjusted shadowSize would be a DPR trap if
//     `Screen.devicePixelRatio` ever disagrees with the panel's
//     actual rendering DPR — the ratio form avoids the trap.
//   - blurRadius: blur kernel radius in WALLPAPER pixels, so the
//     visual blur stays consistent regardless of panel thickness.
//   - shadowFraction: shadowSize / (thickness + shadowSize). DPR-
//     independent ratio of "how much of the surface is shadow". 0
//     disables the shadow. The C++ side (PhosphorShell::PanelWindow::
//     shadowSize + ShellEngine::materializePanels) is responsible for
//     allocating the extra surface space; this param just tells the
//     shader where the panel-to-shadow split lives.
//   - shadowOpacity: alpha of the shadow at its strongest (right
//     below the panel edge); fades quadratically to 0 at the far end
//     of the strip. Default 0.5 looks right against most wallpapers.
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

layout(binding = 11) uniform sampler2D uWallpaper;

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

// 9-tap star-pattern blur of the wallpaper at the given UV. `texel` is
// the wallpaper's per-pixel UV step scaled by blurRadius. Weights are
// approximately gaussian — sum normalised to 1.
vec3 blurredWallpaper(vec2 uv, vec2 texel) {
    const vec2 offsets[9] = vec2[](
        vec2( 0.0,  0.0),
        vec2(-1.0,  0.0), vec2( 1.0,  0.0),
        vec2( 0.0, -1.0), vec2( 0.0,  1.0),
        vec2(-0.7, -0.7), vec2( 0.7, -0.7),
        vec2(-0.7,  0.7), vec2( 0.7,  0.7));
    const float weights[9] = float[](
        0.20,
        0.12, 0.12, 0.12, 0.12,
        0.08, 0.08, 0.08, 0.08);
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 9; i++) {
        sum += texture(uWallpaper, uv + offsets[i] * texel).rgb * weights[i];
        wsum += weights[i];
    }
    return sum / wsum;
}

void main() {
    if (iResolution.x <= 0.0 || iResolution.y <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    float speed        = customParams[0].x > 0.0 ? customParams[0].x : 0.5;
    float angle        = customParams[0].y;
    float tintOpacity  = customParams[0].z > 0.0 ? customParams[0].z : 0.7;
    float frostAmount  = customParams[0].w > 0.0 ? customParams[0].w : 0.1;
    // Lower-clamp at 1.0: the standard rounded-box SDF
    //   length(max(abs(p) - b + r, 0)) - r
    // degenerates at r == 0 — interior fragments return SDF=0 (not
    // negative), so the `1 - smoothstep(-1, 0, dist)` mask zeroes the
    // whole panel out. r=1 gives a sharp-looking corner at one pixel
    // (visually indistinguishable from "no rounding" at typical panel
    // thickness) and keeps the SDF properly negative on the interior.
    float radius       = max(1.0, customParams[1].x);
    float frostScale   = customParams[1].y > 0.0 ? customParams[1].y : 24.0;
    // panelToScreenH: panel surface height / screen height (logical or
    // physical, ratio cancels DPR). Default 1.0 → samples the whole
    // wallpaper height across the panel (visually wrong but won't NaN
    // when the QML side hasn't computed the ratio yet).
    float panelToScreenH = customParams[2].x > 0.0 ? customParams[2].x : 1.0;
    float blurRadius     = customParams[2].y > 0.0 ? customParams[2].y : 8.0;
    // shadowFraction: ratio of the surface that's the shadow strip
    // (0..1). 0 disables the shadow branch entirely.
    float shadowFraction = clamp(customParams[3].x, 0.0, 1.0);
    float shadowOpacity  = customParams[3].y > 0.0 ? customParams[3].y : 0.5;

    // Split the surface into the visible panel region (top) and the
    // drop-shadow strip (bottom). shadowFraction==0 collapses this to
    // "everything is panel" — shadowStartV becomes 1.0 and the shadow
    // branch never fires. DPR-independent: the QML side passes the
    // surface-height ratio directly, so a mismatch between
    // `Screen.devicePixelRatio` and the actual rendering DPR can't
    // misplace the split.
    float shadowStartV = 1.0 - shadowFraction;

    // SDF rounded rectangle mask for the SURFACE — gives AA at all
    // four edges of the wl_surface. The shadow branch below already
    // tapers its own alpha so the SDF AA only contributes at the
    // very corners.
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);
    float mask = 1.0 - smoothstep(-1.0, 0.0, dist);
    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // ─── Shadow strip ─────────────────────────────────────────────────
    // Quadratic alpha falloff: strongest at the panel-bottom edge
    // (right after shadowStartV), fading to 0 at the surface-bottom
    // edge. RGB is black; the compositor blends this over whatever's
    // behind, producing a real darken-the-region-below-the-panel
    // shadow.
    if (shadowFraction > 0.001 && uv.y > shadowStartV) {
        float shadowSpan = 1.0 - shadowStartV;
        float t = (uv.y - shadowStartV) / max(shadowSpan, 0.001);
        // Quadratic falloff SHAPE — peaks at 1.0 at the panel-bottom
        // edge and decays to 0 at the far end. Previous formulation
        // multiplied the falloff INTO the opacity then squared the
        // whole thing, giving peak alpha = opacity² (≈0.20 at
        // opacity=0.45) — way too subtle to read as a drop shadow.
        float falloff = 1.0 - t;
        falloff *= falloff;
        float a = shadowOpacity * falloff * mask;
        fragColor = vec4(0.0, 0.0, 0.0, a) * qt_Opacity;
        return;
    }

    // Remap uv.y to "panel-local" coordinates [0..1] so the gradient
    // direction rotates around the centre of the VISIBLE panel rather
    // than the centre of the (shadow-extended) surface. Wallpaper
    // sampling still uses the raw `uv` because the wallpaper's screen
    // y position is a function of the surface y, not panel-local y.
    vec2 panelUv = vec2(uv.x, uv.y / max(shadowStartV, 0.001));

    // ─── Wallpaper backdrop (blurred top strip) ───────────────────────
    // Auto-detect availability: the wallpaper sampler is bound to a 1×1
    // transparent texture when no real wallpaper is present, so the
    // textureSize check distinguishes "real wallpaper" from "fallback".
    vec3 wallpaperBg = vec3(0.0);
    bool hasWallpaper = false;
    vec2 wpSize = vec2(textureSize(uWallpaper, 0));
    if (wpSize.x > 1.0 && wpSize.y > 1.0) {
        hasWallpaper = true;
        // Panel UV → screen UV. Panel is at the top of the screen
        // spanning full width, so screenU == panelU and screenV ramps
        // 0 .. (panel total height / screen height) as panel UV ramps
        // 0 .. 1. The ratio is passed DPR-independently via
        // panelToScreenH so we don't have to reconcile QML's
        // Screen.devicePixelRatio with the panel's actual rendering DPR.
        vec2 screenUv = vec2(uv.x, uv.y * panelToScreenH);
        // Screen UV → wallpaper UV. KDE / GNOME / Hyprland default to
        // "scaled and cropped" wallpaper positioning; emulate center-
        // crop fit so the on-screen pixel maps to the right wallpaper
        // pixel.
        // Derive screen aspect from `panelToScreenH`: panel surface
        // is full screen width, so screen width = iResolution.x and
        // screen height = iResolution.y / panelToScreenH.
        float wpAspect = wpSize.x / max(wpSize.y, 1.0);
        float scrAspect = (iResolution.x * panelToScreenH) / max(iResolution.y, 1.0);
        vec2 wpUv = screenUv;
        if (wpAspect > scrAspect) {
            float scale = scrAspect / wpAspect;
            wpUv.x = (wpUv.x - 0.5) * scale + 0.5;
        } else {
            float scale = wpAspect / scrAspect;
            wpUv.y = (wpUv.y - 0.5) * scale + 0.5;
        }
        // blurRadius is in wallpaper pixels — divide by wpSize to get
        // the per-tap UV offset.
        vec2 texel = vec2(blurRadius) / wpSize;
        wallpaperBg = blurredWallpaper(wpUv, texel);
    }

    // ─── Animated gradient overlay ────────────────────────────────────
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
    // hasWallpaper: gradient overlay tinted at tintOpacity over the
    //   blurred wallpaper backdrop. The panel is visually opaque (mask
    //   only modulates the AA edge); the gradient adds chromatic
    //   character without obscuring the wallpaper underneath.
    // !hasWallpaper: gradient drawn translucently so the compositor's
    //   default background bleeds through.
    vec3 finalRgb;
    float finalAlpha;
    if (hasWallpaper) {
        finalRgb = mix(wallpaperBg, gradient, tintOpacity);
        finalAlpha = mask;
    } else {
        finalRgb = gradient;
        finalAlpha = tintOpacity * mask;
    }

    // Pre-multiplied alpha output: RGB * alpha before output
    fragColor = vec4(finalRgb * finalAlpha, finalAlpha) * qt_Opacity;
}
