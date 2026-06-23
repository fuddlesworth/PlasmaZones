// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Static Fade transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/static-fade). TV-static
// interference reveal — random RGB static panels overlay the surface,
// ramping with intensity at midpoint.
//
// Niri's static-fade ships symmetric close.glsl/open.glsl — bodies are
// identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// with `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)`
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. Niri's `sf_rnd`,
// `sf_static`, and `sf_intensity` helpers lift to file scope unchanged.

float sf_rnd(vec2 st) {
    return fract(sin(dot(st.xy, vec2(10.5302340293, 70.23492931))) * 12345.5453123);
}

vec4 sf_static(vec2 st, float offset, float lum) {
    float r = lum * sf_rnd(st * vec2(offset * 2.0, offset * 3.0));
    float g = lum * sf_rnd(st * vec2(offset * 3.0, offset * 5.0));
    float b = lum * sf_rnd(st * vec2(offset * 5.0, offset * 7.0));
    return vec4(r, g, b, 1.0);
}

float sf_intensity(float t) {
    float tp = abs(2.0 * (t - 0.5));
    return min(1.0, 1.2 * (1.0 - tp) - 0.1);
}

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `pixelGrid` means "static cells across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cell count to the
    // fraction of the screen this surface covers, so cell pixel size
    // stays constant across popup vs. maximized windows. Matches niri's
    // reference on full-screen (multiplier = 1.0 there).
    vec2 cellsAcross = vec2(p_pixelGrid) * max(iAnchorSize, vec2(1.0))
                                       / max(iSurfaceScreenPos.zw, vec2(1.0));
    vec2 uvStatic = floor(uv * cellsAcross) / cellsAcross;
    // Gate the procedural static by the captured card alpha so it cannot
    // paint the rounded-corner / transparent margin opaque. Without this
    // the sf_static() opacity-1 vec4 wins through the mix() below and
    // emits opaque RGB outside the visible card silhouette.
    vec4 staticColor = sf_static(uvStatic, p, p_staticBrightness) * win.a;
    float staticThresh = sf_intensity(p);
    float staticMix = step(sf_rnd(uvStatic), staticThresh);

    float reveal = step(0.5, p);
    vec4 base = win * reveal;
    vec4 result = mix(base, staticColor, staticMix * smoothstep(0.0, 1.0, p));

    float in_bounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    return result * in_bounds;
}
