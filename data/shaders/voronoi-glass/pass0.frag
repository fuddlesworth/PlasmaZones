// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Voronoi Stained Glass — Buffer Pass 0: 3D Raymarched Glass Panel
// Renders a stained glass window using raymarching through a glass slab
// with raised lead came at Voronoi cell edges. 3D normals enable proper
// backlighting, specular highlights, and Fresnel reflections.
// Output to iChannel0: RGB = rendered scene, A = 1.0.

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

#define MAX_STEPS 80
#define SURF_DIST 0.001

// --- Voronoi 2D ---
// Returns vec3(d1, d2, cellIdHash).
vec3 voronoi(vec2 uv, float jit, float anim) {
    vec2 ic = floor(uv), fc = fract(uv);
    float d1 = 8.0, d2 = 8.0;
    vec2 cid = vec2(0.0);
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
        vec2 nb = vec2(float(i), float(j));
        vec2 cc = ic + nb;
        vec2 rn = hash22(cc);
        vec2 pt = mix(vec2(0.5), 0.5 + 0.5 * sin(anim + TAU * rn), jit);
        float d = length(nb + pt - fc);
        if (d < d1) { d2 = d1; d1 = d; cid = cc; }
        else if (d < d2) d2 = d;
    }
    return vec3(d1, d2, hash21(cid));
}

// Map cell ID hash to one of four glass palette colors.
vec3 cellColor(float h) {
    vec3 c1 = colorWithFallback(customColors[0].rgb, vec3(0.267, 0.533, 0.933));
    vec3 c2 = colorWithFallback(customColors[1].rgb, vec3(0.867, 0.200, 0.267));
    vec3 c3 = colorWithFallback(customColors[2].rgb, vec3(0.200, 0.733, 0.333));
    vec3 c4 = colorWithFallback(customColors[3].rgb, vec3(0.933, 0.733, 0.200));
    return h < 0.25 ? c1 : h < 0.5 ? c2 : h < 0.75 ? c3 : c4;
}

// Scene parameters (set in main, read by map/calcNormal).
float gScale, gJit, gAnim, gLeadW, gCameH, gThick;

// Scene SDF: glass slab with raised lead came at Voronoi edges.
// The came profile is a smooth bump above the glass surface.
float map(vec3 p) {
    vec3 v = voronoi(p.xy * gScale, gJit, gAnim);
    float edge = (v.y - v.x) / gScale; // world-space edge distance
    // Came: flat-topped ridge at Voronoi edges (H-came cross-section)
    float came = smoothstep(gLeadW, gLeadW * 0.15, edge);
    float top = gThick + came * gCameH;
    // Slab SDF: front face at z=top (variable), back face at z=-gThick
    return max(p.z - top, -(p.z + gThick));
}

vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.002, 0.0);
    return normalize(vec3(
        map(p + e.xyy) - map(p - e.xyy),
        map(p + e.yxy) - map(p - e.yxy),
        map(p + e.yyx) - map(p - e.yyx)
    ));
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / min(iResolution.x, iResolution.y);

    // Load parameters
    gScale = customParams[0].x > 0.5 ? customParams[0].x : 5.0;
    float animSpd = customParams[0].y;
    gJit   = customParams[0].z > 0.01 ? customParams[0].z : 0.8;
    gLeadW = customParams[1].x > 0.001 ? customParams[1].x : 0.04;
    float leadDark = customParams[1].y > 0.1 ? customParams[1].y : 0.85;
    gThick = customParams[1].z > 0.001 ? customParams[1].z : 0.04;
    gCameH = gThick * 0.5;
    float lightAngle = customParams[2].x;
    float lightInt   = customParams[2].y > 0.001 ? customParams[2].y : 0.6;
    float specStr    = customParams[2].z > 0.001 ? customParams[2].z : 0.4;

    vec3 leadClr  = colorWithFallback(customColors[4].rgb, vec3(0.102, 0.102, 0.118));
    vec3 lightClr = colorWithFallback(customColors[5].rgb, vec3(1.0, 0.961, 0.878));
    gAnim = iTime * animSpd;

    // Camera: gentle sway for subtle 3D parallax
    float sway = 0.15;
    vec3 ro = vec3(sin(iTime * 0.1) * sway, cos(iTime * 0.07) * sway * 0.7, 2.5);
    vec3 fwd = normalize(-ro);
    vec3 rt  = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up  = cross(rt, fwd);
    vec3 rd  = normalize(fwd + rt * uv.x + up * uv.y);

    // Analytical near-skip: advance ray to just above the glass panel
    float t = 0.0;
    if (rd.z < -0.0001)
        t = max(0.0, (ro.z - gThick - gCameH - 0.2) / (-rd.z));

    // Raymarch (0.7 relaxation for height-field stability)
    float d;
    for (int i = 0; i < MAX_STEPS; i++) {
        d = map(ro + rd * t);
        if (d < SURF_DIST) break;
        t += d * 0.7;
        if (t > 12.0) break;
    }

    vec3 col = vec3(0.01); // near-black background

    if (d < SURF_DIST * 2.0) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);

        // Material query at hit point
        vec3 v = voronoi(p.xy * gScale, gJit, gAnim);
        float edge = (v.y - v.x) / gScale;
        float leadMask = smoothstep(gLeadW * 0.7, gLeadW * 0.1, edge);
        vec3 glass = cellColor(v.z) * (0.95 + 0.1 * hash21(vec2(v.z, 7.3)));

        // --- Back-light: warm light shining through the glass ---
        float laDrift = sin(iTime * 0.15) * 8.0;
        float la = radians(lightAngle + laDrift);
        vec3 backDir = normalize(vec3(sin(la) * 0.4, 0.3, -1.0));
        float transmission = pow(max(0.0, dot(n, -backDir)), 0.45) * lightInt;
        vec3 glassLit = glass * lightClr * transmission + glass * 0.25;

        // Fresnel reflection on glass surface
        float fresnel = pow(1.0 - abs(dot(n, -rd)), 4.0);
        glassLit += lightClr * fresnel * 0.08;

        // --- Front fill light on lead came ---
        vec3 fillDir = normalize(vec3(-0.2, 0.4, 1.0));
        float diff = max(0.0, dot(n, fillDir));
        vec3 leadLit = leadClr * (0.25 + 0.75 * diff);

        // Blinn-Phong specular on came
        vec3 hv = normalize(-rd + fillDir);
        float spec = pow(max(0.0, dot(n, hv)), 48.0);
        leadLit += lightClr * spec * specStr * 0.5;

        // Catch-light at glass-lead boundary
        float catchLight = leadMask * (1.0 - leadMask) * 4.0;
        leadLit += lightClr * catchLight * 0.1;

        // Composite glass and lead
        col = mix(glassLit, leadLit, leadMask * leadDark);

        // Static glass grain (manufacturing imperfections)
        col += (hash21(p.xy * 50.0) - 0.5) * 0.015 * (1.0 - leadMask);
    }

    fragColor = vec4(max(col, 0.0), 1.0);
}
