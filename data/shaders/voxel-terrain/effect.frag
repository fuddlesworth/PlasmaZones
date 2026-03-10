// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * VOXEL TERRAIN — Infinite 3D Voxel World with Audio-Reactive Glow
 *
 * DDA voxel raycasting with Quilez-style edge detection and AO.
 * Camera flies a winding path; terrain is carved clear around the path
 * so the camera always has geometry at a comfortable viewing distance.
 * Audio modulates glow, wireframe, emission — geometry stays stable.
 * Screen-space: one shared camera, zones are windows into the world.
 */

// ─── Fast 3D noise (integer hash, no sin/trig) ──────────────────────
// ihash_i takes ivec3 directly — avoids redundant floor() in hot loop.

float ihash_i(ivec3 ip) {
    ip &= 0xFF;
    int n = ip.x + ip.y * 157 + ip.z * 113;
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return float(n & 0x7fffffff) * (1.0 / 2147483647.0);
}

// Convenience wrapper for non-integer inputs (used in sparkle etc.)
float ihash(vec3 p) { return ihash_i(ivec3(floor(p))); }

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    ivec3 ip = ivec3(i);
    float a  = ihash_i(ip);
    float b  = ihash_i(ip + ivec3(1, 0, 0));
    float c  = ihash_i(ip + ivec3(0, 1, 0));
    float d  = ihash_i(ip + ivec3(1, 1, 0));
    float a2 = ihash_i(ip + ivec3(0, 0, 1));
    float b2 = ihash_i(ip + ivec3(1, 0, 1));
    float c2 = ihash_i(ip + ivec3(0, 1, 1));
    float d2 = ihash_i(ip + ivec3(1, 1, 1));

    float z0 = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    float z1 = mix(mix(a2, b2, f.x), mix(c2, d2, f.x), f.y);
    return mix(z0, z1, f.z);
}

// ─── Terrain ─────────────────────────────────────────────────────────

struct TerrainParams {
    float terrainScale;
    float time;
    float idleSpeed;
    float maxHeight;
};

// Raw terrain noise — just the FBM, no carve-out. Used for neighbor
// lookups where the carve value is nearly identical to the hit cell.
// 2-octave version for DDA hot path; 3rd octave adds little at voxel scale.
float terrainNoise(vec3 p, TerrainParams tp) {
    vec3 sp = p * tp.terrainScale;
    sp.xz *= 0.7;
    vec3 drift = vec3(0.0, 1.2 * tp.time * tp.idleSpeed * 0.15, 0.0);

    float f = 0.0;
    f += 0.50 * noise3D(sp + drift);
    f += 0.25 * noise3D(sp * 2.02 + drift * 1.5);
    return f - 0.34;  // threshold adjusted for 2 octaves (~30% solid)
}

// Full terrain with camera carve-out. < 0 = solid, > 0 = empty.
// Carve radius is large enough to give comfortable viewing distance.
float mapTerrain(vec3 p, vec3 camPos, TerrainParams tp) {
    float density = terrainNoise(p, tp);

    // Camera carve-out using squared distance (avoids sqrt per DDA step).
    // smoothstep on squared thresholds: 625=25², 100=10².
    vec3 dv = p - camPos;
    float camDist2 = dot(dv, dv);
    density += smoothstep(625.0, 100.0, camDist2);

    // Height slab: terrain fades out beyond maxHeight above/below camera
    float dy = dv.y;
    density += smoothstep(tp.maxHeight, tp.maxHeight * 2.5, abs(dy)) * 0.5;

    return density;
}

// Voxel solid test — full version with carve (for DDA)
float voxelSolid(vec3 c, vec3 camPos, TerrainParams tp) {
    return step(mapTerrain(c + 0.5, camPos, tp), 0.0);
}

// Voxel solid test — noise only (for 16-neighbor surface analysis).
// Uses single octave: binary solid/empty classification doesn't need
// FBM detail, and this halves the cost of 16 neighbor lookups.
float voxelSolidLocal(vec3 c, TerrainParams tp) {
    vec3 p = (c + 0.5) * tp.terrainScale;
    p.xz *= 0.7;
    vec3 drift = vec3(0.0, 1.2 * tp.time * tp.idleSpeed * 0.15, 0.0);
    float f = 0.50 * noise3D(p + drift);
    return step(f, 0.22);  // adjusted threshold for 1 octave (~30% solid)
}

// ─── DDA raycaster ───────────────────────────────────────────────────

struct HitInfo {
    bool hit;
    float t;
    vec3 pos;
    vec3 vos;
    vec3 dir;
    vec3 normal;
    float volGlow;
};

HitInfo dda_raycast(vec3 ro, vec3 rd, vec3 camPos, TerrainParams tp) {
    HitInfo info;
    info.hit = false;
    info.volGlow = 0.0;

    // Skip past the camera carve-out zone: first ~10 voxels are guaranteed
    // empty so advance the ray origin to t=8 before starting DDA.
    vec3 startRo = ro + rd * 8.0;

    vec3 pos = floor(startRo);
    vec3 ri = 1.0 / rd;
    vec3 rs = sign(rd);
    vec3 dis = (pos - startRo + 0.5 + rs * 0.5) * ri;
    vec3 mm = vec3(0.0);

    // Max distance before fog makes geometry invisible (~1/fogDensity)
    float maxDist = 60.0;

    for (int i = 0; i < 64; i++) {
        if (voxelSolid(pos, camPos, tp) > 0.5) {
            info.hit = true;
            break;
        }

        mm = step(dis.xyz, dis.yzx) * step(dis.xyz, dis.zxy);
        dis += mm * rs * ri;
        pos += mm * rs;

        // Early termination: past fog visibility distance (cheap L∞ norm)
        vec3 delta = abs(pos - ro);
        if (max(delta.x, max(delta.y, delta.z)) > maxDist) break;
    }

    if (!info.hit) {
        // Cheap volumetric estimate — avoid per-step noise sampling
        info.volGlow = 0.4;
        return info;
    }

    info.normal = -mm * rs;
    info.vos = pos;
    info.dir = mm;

    vec3 mini = (pos - startRo + 0.5 - 0.5 * rs) * ri;
    float localT = max(mini.x, max(mini.y, mini.z));
    info.t = localT + 8.0;  // add back the skip distance
    info.pos = ro + rd * info.t;

    // Volumetric glow: based on hit distance (cheap analytical estimate)
    info.volGlow = 0.5 * smoothstep(5.0, 40.0, info.t);

    return info;
}

// ─── 16-neighbor surface analysis ───────────────────────────────────

struct SurfaceInfo {
    float edge;
    float ao;
    float glow;
};

float maxcomp4(vec4 v) {
    return max(max(v.x, v.y), max(v.z, v.w));
}

// wireThresh: base edge width (Quilez uses ~0.15 = 15% of face).
// audioEdgeBoost: extra width from audio (bass widens edges on beat).
SurfaceInfo computeSurface(vec3 vos, vec3 nor, vec3 dir, vec3 uvw,
                           float wireThresh, float audioEdgeBoost,
                           TerrainParams tp) {
    SurfaceInfo si;
    vec3 tanU = dir.yzx;
    vec3 tanV = dir.zxy;
    vec2 uv = clamp(vec2(dot(tanU, uvw), dot(tanV, uvw)), 0.0, 1.0);
    vec2 st = 1.0 - uv;

    vec4 vc = vec4(
        voxelSolidLocal(vos + nor + tanU, tp), voxelSolidLocal(vos + nor - tanU, tp),
        voxelSolidLocal(vos + nor + tanV, tp), voxelSolidLocal(vos + nor - tanV, tp));
    vec4 vd = vec4(
        voxelSolidLocal(vos + nor + tanU + tanV, tp), voxelSolidLocal(vos + nor - tanU + tanV, tp),
        voxelSolidLocal(vos + nor - tanU - tanV, tp), voxelSolidLocal(vos + nor + tanU - tanV, tp));
    vec4 va = vec4(
        voxelSolidLocal(vos + tanU, tp), voxelSolidLocal(vos - tanU, tp),
        voxelSolidLocal(vos + tanV, tp), voxelSolidLocal(vos - tanV, tp));
    vec4 vb = vec4(
        voxelSolidLocal(vos + tanU + tanV, tp), voxelSolidLocal(vos - tanU + tanV, tp),
        voxelSolidLocal(vos - tanU - tanV, tp), voxelSolidLocal(vos + tanU - tanV, tp));

    // Edge detection — tight crisp lines at voxel boundaries.
    // wireThresh controls base width; audioEdgeBoost widens on beat.
    float edgeW = wireThresh + audioEdgeBoost;
    float lo = 1.0 - edgeW;
    float hi = lo + 0.04;  // 4% transition — sharp edge, not soft glow

    // Side edges: visible where face meets empty or convex neighbor
    vec4 ew = smoothstep(lo, hi, vec4(uv.x, st.x, uv.y, st.y))
            * (1.0 - va + va * vc);
    // Corner edges
    vec4 cw = smoothstep(lo, hi, vec4(
        uv.x * uv.y, st.x * uv.y, st.x * st.y, uv.x * st.y))
            * (1.0 - vb + vd * vb);
    si.edge = 1.0 - maxcomp4(max(ew, cw));

    // AO — bilinear from neighbor occupancy
    vec4 aoE = vec4(uv.x, st.x, uv.y, st.y) * vc;
    vec4 aoC = vec4(uv.x * uv.y, st.x * uv.y, st.x * st.y, uv.x * st.y)
             * vd * (1.0 - vc.xzyw) * (1.0 - vc.zywx);
    float aoSum = aoE.x + aoE.y + aoE.z + aoE.w + aoC.x + aoC.y + aoC.z + aoC.w;
    si.ao = 1.0 - aoSum * 0.125;
    si.ao = si.ao * si.ao;

    // Edge glow — moderate bleed for multi-layer glow system
    vec4 ge = smoothstep(0.5, 0.0, vec4(st.x, uv.x, st.y, uv.y)) * (1.0 - va * (1.0 - vc));
    vec4 gc = smoothstep(0.5, 0.0, vec4(st.x * st.y, uv.x * st.y, uv.x * uv.y, st.x * uv.y))
            * (1.0 - vb * (1.0 - vd));
    si.glow = ge.x + ge.y + ge.z + ge.w + gc.x + gc.y + gc.z + gc.w;

    return si;
}

// ─── Camera path ────────────────────────────────────────────────────

vec3 cameraPath(float t) {
    vec2 p = 80.0 * sin(0.024 * t * vec2(1.0, 1.27) + vec2(0.1, 0.9));
    p += 40.0 * sin(0.051 * t * vec2(1.33, 1.0) + vec2(1.0, 4.5));
    float y = 6.0 * sin(0.037 * t) + 3.0 * sin(0.071 * t);
    return vec3(p.x, y, p.y);
}

// ─── Per-zone rendering ─────────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float terrainScale = customParams[0].y >= 0.0 ? customParams[0].y : 0.1;
    float maxHeight    = customParams[0].z >= 0.0 ? customParams[0].z : 6.0;
    float edgeGlow     = customParams[0].w >= 0.0 ? customParams[0].w : 1.5;
    float cameraHeight = customParams[1].x >= 0.0 ? customParams[1].x : 8.0;
    float scrollSpeed  = customParams[1].y >= 0.0 ? customParams[1].y : 2.0;
    float cameraTilt   = customParams[1].z >= 0.0 ? customParams[1].z : 0.35;
    float fillOpacity  = customParams[1].w >= 0.0 ? customParams[1].w : 0.92;
    float wireThresh   = customParams[2].x >= 0.0 ? customParams[2].x : 0.10;
    float fogDensity   = customParams[2].y >= 0.0 ? customParams[2].y : 0.05;
    float bassImpact   = customParams[2].z >= 0.0 ? customParams[2].z : 2.0;
    float idleSpeed    = customParams[2].w >= 0.0 ? customParams[2].w : 1.0;

    vec3 primary   = colorWithFallback(customColors[0].rgb, vec3(0.06, 0.08, 0.18));
    vec3 accent    = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.83, 1.0));
    vec3 bassCol   = colorWithFallback(customColors[2].rgb, vec3(0.9, 0.0, 0.67));
    vec3 wireColor = colorWithFallback(customColors[3].rgb, vec3(0.6, 0.7, 0.9));

    float energy = hasAudio ? overall * reactivity : 0.0;

    TerrainParams tp;
    tp.terrainScale = terrainScale;
    tp.time = iTime;
    tp.idleSpeed = idleSpeed;
    tp.maxHeight = maxHeight;

    float vitality = zoneVitality(isHighlighted);
    if (!isHighlighted) {
        primary = vitalityDesaturate(primary, 0.3);
        accent = vitalityDesaturate(accent, 0.3);
        bassCol = vitalityDesaturate(bassCol, 0.3);
        wireColor *= 0.5;
        edgeGlow *= 0.4;
        reactivity *= 0.5;
    }

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        vec2 screenUV = (2.0 * fragCoord - iResolution) / min(iResolution.x, iResolution.y);
        screenUV.y = -screenUV.y;

        // Camera: winding path through the world
        float dt = iTime * scrollSpeed + 50.0;
        float roll = 0.15 * cos(0.08 * iTime);
        vec3 ro = cameraPath(dt) + vec3(0.0, cameraHeight, 0.0);
        vec3 ta = cameraPath(dt + 10.0) + vec3(0.0, cameraHeight - cameraTilt * 4.0, 0.0);

        // Look-at matrix
        vec3 cw = normalize(ta - ro);
        vec3 cp = vec3(sin(roll), cos(roll), 0.0);
        vec3 cu = normalize(cross(cw, cp));
        vec3 cv = normalize(cross(cu, cw));

        vec3 rd = normalize(cu * screenUV.x + cv * screenUV.y + cw * 2.0);

        vec3 camPos = ro;

        // ── DDA raymarch ──────────────────────────────────

        HitInfo hit = dda_raycast(ro, rd, camPos, tp);

        vec3 col = vec3(0.0);
        float alpha = 0.0;

        float volG = min(hit.volGlow, 3.0);

        if (hit.hit) {
            vec3 nor = hit.normal;
            vec3 uvw = hit.pos - hit.vos;

            // Audio-reactive edge width: bass makes edges physically WIDER
            float audioEdgeBoost = hasAudio ? bass * bassImpact * 0.15 : 0.0;

            SurfaceInfo surf = computeSurface(
                hit.vos, nor, hit.dir, uvw,
                wireThresh, audioEdgeBoost, tp);

            float edgeLine = 1.0 - surf.edge;

            // ═══════════════════════════════════════════════════
            // Quilez-exact compositing:
            // 1. Face: textured, lit, AO^4 → range 0.5-5.0
            // 2. Edge emission: massive HDR additive → 30-80+
            // 3. Flat cap: min(0.1, exp(-fog*t))
            //    — preserves color ratios (no Reinhard!)
            // 4. Gamma 0.45 + hard clamp → saturated neon
            // ═══════════════════════════════════════════════════

            float ao2 = surf.ao * surf.ao;
            float ao4 = ao2 * ao2;

            // ── Position-based palette variation ────────────
            float palT = dot(hit.vos, vec3(0.07, 0.11, 0.13)) + iTime * 0.03;
            vec3 edgeCol = mix(accent, bassCol, 0.5 + 0.5 * sin(palT));

            // ── Face: noise-textured, Quilez-scale brightness ─
            // Quilez face values: ~0.7-1.5 before lighting.
            // We use primary * 8.0 + tint to reach similar range.
            float faceNoise = noise3D(hit.vos * 0.4 + vec3(iTime * 0.02));
            col = primary * 8.0 + vec3(0.08, 0.1, 0.15);
            col *= 0.5 + 0.5 * faceNoise;  // detail texture variation
            col += edgeCol * 0.04;  // subtle accent bleed into faces

            // ── Lighting (Quilez-scale multipliers) ─────────
            float dif = clamp(dot(nor, normalize(vec3(-0.4, 0.3, 0.7))), 0.0, 1.0);
            float sky = 0.5 + 0.5 * nor.y;
            float bac = clamp(dot(nor, normalize(vec3(0.4, -0.3, -0.7))), 0.0, 1.0);
            vec3 lin = vec3(0.0);
            lin += 4.0 * dif * vec3(0.85, 0.9, 1.0) * (0.5 + 0.5 * ao2);
            lin += 0.4 * bac * vec3(0.15, 0.1, 0.2) * ao2;
            lin += 1.5 * sky * vec3(0.25, 0.3, 0.45) * ao2;

            // Bass dims faces, pumps edges (contrast)
            if (hasAudio) lin *= mix(1.0, 0.5, bass);

            col *= lin;

            // AO^2 on faces (softer than ^4, prevents black crush)
            col *= ao2;

            // Internal wireframe darkening (Quilez vvv pattern)
            // Only applies on faces (surf.edge=1), NOT on exposed edges
            vec3 wir = smoothstep(vec3(0.4), vec3(0.5), abs(uvw - 0.5));
            float vvv = (1.0 - wir.x * wir.y) * (1.0 - wir.x * wir.z) * (1.0 - wir.y * wir.z);
            col *= 1.0 - 0.45 * (1.0 - vvv) * surf.edge;

            // ── Edge emission: massive HDR additive ─────────
            // linCol: accent * 5.0, modulated by gentle AO
            vec3 linCol = edgeCol * 5.0;
            float aoEdge = 0.5 + 0.5 * ao2;  // gentle — never goes to zero
            linCol *= aoEdge;

            float edgePower;
            if (hasAudio) {
                edgePower = 1.0 + bass * bassImpact * 1.5;
                linCol = mix(linCol, bassCol * 5.0 * aoEdge, bass * 0.4);
            } else {
                float pulse = 0.6 + 0.4 * sin(iTime * idleSpeed);
                edgePower = 0.7 + pulse * 0.5;
            }

            // Distance color shift for depth: close=accent, far=cooler
            float distFade = smoothstep(12.0, 50.0, hit.t);
            vec3 edgeTint = mix(vec3(1.0, 1.1, 1.0), vec3(0.7, 0.9, 1.2), distFade);

            // Glow into face: subtle brightening near edges
            // Quilez multiplies this into lighting, keeping it modest.
            // Must stay well below 1.0 after ×0.1 cap to avoid blowout.
            col += 0.3 * surf.glow * linCol * edgePower;

            // Primary additive: THE neon edge (only on hard edgeLine mask)
            col += 8.0 * edgePower * linCol * edgeTint * edgeLine;

            // Subtle ambient glow
            col += 0.05 * surf.glow * linCol * edgePower;

            // Grid lines: wire color tinted by edge color
            vec3 gridDist = abs(fract(uvw) - 0.5);
            float gridLine = 1.0 - smoothstep(0.0, 0.05, min(gridDist.x, min(gridDist.y, gridDist.z)));
            vec3 gridCol = mix(wireColor, edgeCol, 0.3) * 0.5;
            if (hasAudio) {
                gridCol *= 0.5 + treble * reactivity * 0.5;
            }
            col += gridCol * gridLine;

            // edgeGlow param scales emission
            col *= mix(0.3, 1.0, edgeGlow / 1.5);

            // ── Quilez distance cap ─────────────────────────
            // FLAT min(0.1, exp(-fog*t)) — NOT Reinhard.
            // Preserves channel ratios perfectly: edges stay
            // saturated because 80*0.1=8.0, gamma clips hot.
            // Faces stay visible: 2.0*0.1=0.2, gamma lifts.
            float fogAtten = min(0.1, exp(-fogDensity * hit.t));
            if (hasAudio) fogAtten += bass * 0.02;  // bass lifts cap slightly
            col *= fogAtten;

            // ── Post-cap emissive effects ────────────────────
            // Sparkle and shimmer bypass the cap for maximum pop
            if (hasAudio) {
                float sparkle = ihash(hit.vos);
                float sparkleThresh = 0.93 - treble * 0.15;
                if (sparkle > sparkleThresh) {
                    vec3 sparkCol = mix(accent, bassCol, bass * 0.5);
                    col += sparkCol * 0.4 * treble * reactivity;
                }
            } else {
                float shimmer = ihash(hit.vos + vec3(floor(iTime * 2.0)));
                if (shimmer > 0.96) {
                    col += edgeCol * 0.06 * (0.5 + 0.5 * sin(iTime * 3.0 + shimmer * 20.0));
                }
            }

            alpha = fillOpacity;
        } else {
            // ── Negative space: atmospheric depth ──────────────

            // Radial depth gradient — not flat black
            vec2 screenCenter = screenUV;
            float radialDark = 1.0 - length(screenCenter) * 0.15;
            col = primary * 0.04 * radialDark;
            alpha = fillOpacity;

            // Subtle FBM atmosphere (moving nebula-like haze)
            float atmoNoise = noise3D(vec3(rd.xz * 3.0, iTime * 0.05));
            col += mix(primary, accent, atmoNoise * 0.3) * 0.015 * atmoNoise;

            // ── Fog / atmosphere gradient ────────────────────────
            float horizon = 1.0 - abs(rd.y);
            float hBand = pow(horizon, 4.0);
            float hWide = pow(horizon, 1.5);

            vec3 fogCol = hasAudio
                ? mix(primary * 0.6, accent * 0.3, bass * 0.5)
                : mix(primary * 0.4, accent * 0.15, 0.3);
            col += fogCol * hBand * 0.8;
            col += mix(primary, bassCol * 0.1, 0.2) * 0.04 * hWide;

            // ── Infinite ground plane grid ───────────────────────
            // Two planes: one below, one above — always one visible
            float planeBelow = camPos.y - 18.0;
            float planeAbove = camPos.y + 18.0;
            float tDown = (rd.y < -0.001) ? (planeBelow - ro.y) / rd.y : -1.0;
            float tUp   = (rd.y >  0.001) ? (planeAbove - ro.y) / rd.y : -1.0;
            float planeT = -1.0;
            if (tDown > 0.0 && (tUp < 0.0 || tDown < tUp)) planeT = tDown;
            else if (tUp > 0.0) planeT = tUp;

            if (planeT > 0.0 && planeT < 150.0) {
                vec3 gp = ro + rd * planeT;
                vec2 gUV = gp.xz;

                // Fine grid (1-unit cells)
                vec2 fg = abs(fract(gUV) - 0.5);
                float fLine = 1.0 - smoothstep(0.0, 0.07, min(fg.x, fg.y));

                // Coarse grid (8-unit cells)
                vec2 cg = abs(fract(gUV * 0.125) - 0.5);
                float cLine = 1.0 - smoothstep(0.0, 0.05, min(cg.x, cg.y));

                // Distance fade — fine grid fades faster than coarse
                float fadeFine   = exp(-0.03 * planeT);
                float fadeCoarse = exp(-0.008 * planeT);

                vec3 fineCol  = wireColor * 0.08 * fLine * fadeFine;
                vec3 coarseCol = accent * 0.2 * cLine * fadeCoarse;

                // Audio pulse on the coarse grid
                if (hasAudio) {
                    coarseCol *= 0.7 + bass * bassImpact * 0.6;
                    coarseCol += bassCol * 0.06 * cLine * fadeCoarse * bass;
                } else {
                    float pulse = 0.7 + 0.3 * sin(iTime * idleSpeed * 0.5);
                    coarseCol *= pulse;
                }

                col += fineCol + coarseCol;

                // Plane fog — grid area has a faint colored fog layer
                col += fogCol * 0.04 * fadeCoarse;
            }

            // ── Star field ───────────────────────────────────────
            // Project ray direction onto a dome; two density layers
            vec2 starUV = rd.xz / (abs(rd.y) + 0.15) * 50.0;
            vec2 starCell = floor(starUV);
            int sx = int(starCell.x);
            int sy = int(starCell.y);
            float starHash = ihash_i(ivec3(sx, sy, 73));

            // ~15% of cells get a star (denser field)
            if (starHash > 0.85) {
                vec2 starOff = vec2(
                    ihash_i(ivec3(sx, sy, 31)),
                    ihash_i(ivec3(sx, sy, 59)));
                float sDist = length(fract(starUV) - starOff);
                float sBright = smoothstep(0.25, 0.0, sDist);

                // Twinkle
                float twinkle = 0.4 + 0.6 * sin(iTime * (1.5 + starHash * 5.0) + starHash * 40.0);
                sBright *= twinkle;

                // Color: mostly wire color, some accent, rare bass color
                vec3 sCol = wireColor;
                if (starHash > 0.96) sCol = accent;
                else if (starHash > 0.93) sCol = mix(wireColor, primary * 2.0, 0.5);

                // Audio: stars brighten with treble
                float starMul = hasAudio ? 0.3 + treble * reactivity * 0.4 : 0.3;
                col += sCol * sBright * starMul;
            }

            // ── Distant glow from nearby terrain ─────────────────
            // Use volGlow to hint at terrain just out of reach
            col += fogCol * volG * 0.08;
        }

        // ── Volumetric haze (tiny additive, doesn't lift darks) ──
        vec3 volColor;
        float volMul;
        if (hasAudio) {
            volColor = mix(accent * 0.3, bassCol * 0.2, bass * 0.4);
            volMul = 0.02 + energy * 0.06;
        } else {
            volColor = accent * 0.1;
            volMul = max(0.01 + sin(iTime * 0.6) * 0.005 * idleSpeed, 0.0);
        }
        col += volG * volColor * volMul;

        // ── Gamma + clamp (Quilez-exact) ────────────────
        // No tonemapping. Edges overdrive intentionally — the
        // flat 0.1 cap preserves channel ratios, gamma lifts
        // darks, clamp clips the hot channels to neon.
        col = pow(max(col, vec3(0.0)), vec3(0.45));

        result.rgb = col;
        result.a = max(alpha, min(volG * volMul * 0.2, fillOpacity));

        // Inner edge glow
        float innerGlow = exp(d / mix(30.0, 14.0, vitality)) * mix(0.03, 0.1, vitality);
        innerGlow *= edgeGlow * (0.4 + energy * 0.3);
        result.rgb += primary * innerGlow;

        // Zone labels — holographic voxel HUD style
        if (customParams[3].x > 0.5) {
            vec2 labelUv = fragCoord / max(iResolution, vec2(0.001));
            vec2 texel = 1.0 / max(iResolution, vec2(1.0));
            vec4 labelSample = texture(uZoneLabels, labelUv);
            float labelAlpha = labelSample.a;

            // Gaussian halo with wider spread
            float halo = 0.0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    float w = exp(-float(dx * dx + dy * dy) * 0.3);
                    halo += texture(uZoneLabels, labelUv + vec2(float(dx), float(dy)) * texel * 3.5).a * w;
                }
            }
            halo /= 16.5;

            if (halo > 0.003) {
                float haloEdge = halo * (1.0 - labelAlpha);

                // Chromatic edge fringe — R and B channels offset
                float rH = texture(uZoneLabels, labelUv + vec2(texel.x * 2.5, 0.0)).a;
                float bH = texture(uZoneLabels, labelUv - vec2(texel.x * 2.5, 0.0)).a;
                vec3 chromaFringe = vec3(rH, 0.0, bH) * (1.0 - labelAlpha) * accent * 0.3;
                result.rgb += chromaFringe;

                // Palette-cycling halo (shifts accent→bassCol over time)
                float haloT = length(labelUv - 0.5) + iTime * 0.08;
                vec3 haloCol = mix(accent, bassCol, 0.5 + 0.5 * sin(haloT * 3.0));
                float haloBright = haloEdge * edgeGlow * 0.5;

                // Holographic scanlines through the halo
                float scan = 0.7 + 0.3 * sin(fragCoord.y * 0.8 + iTime * 4.0);
                haloBright *= scan;

                // Audio: bass pulses the halo, treble sparks
                if (hasAudio) {
                    haloBright *= 1.0 + bass * bassImpact * 0.4;
                    // Treble sparkle noise in the halo
                    float sparkNoise = ihash(vec3(floor(labelUv * 40.0), floor(iTime * 6.0)));
                    float spark = smoothstep(0.8, 0.95, sparkNoise) * treble * 1.5;
                    result.rgb += mix(vec3(1.0), accent, 0.3) * haloEdge * spark;
                }

                result.rgb += haloCol * haloBright;
                result.a = max(result.a, haloEdge * 0.4);
            }

            if (labelAlpha > 0.01) {
                // Core: bright holographic text with accent color
                vec3 core = result.rgb * 2.0 + accent * 0.15;

                // Data-stream flicker (irregular, not regular pulse)
                float flicker = 0.9 + 0.1 * sin(iTime * 17.0) * sin(iTime * 23.0);
                core *= flicker;

                // Audio: bass pumps brightness, treble adds glitch bands
                if (hasAudio) {
                    core *= 1.0 + bass * 0.4;
                    float band = step(0.88, fract(fragCoord.y * 0.12 + iTime * 5.0));
                    core = mix(core, bassCol * 1.5, band * treble * 0.3);
                } else {
                    // Idle: gentle pulse
                    core *= 0.9 + 0.1 * sin(iTime * idleSpeed * 2.0);
                }

                result.rgb = mix(result.rgb, core, labelAlpha);
                result.a = max(result.a, labelAlpha);
            }
        }
    }

    // ── Border ────────────────────────────────────────────

    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        float borderAngle = atan(p.x, -p.y) / TAU + 0.5;
        float borderEnergy = 1.0 + energy * mix(0.2, 0.8, vitality);
        vec3 coreColor = primary * edgeGlow * borderEnergy;

        float flowSpeed = mix(0.3, 1.5, vitality);
        float flow = angularNoise(borderAngle, 12.0, -iTime * flowSpeed);
        float segments = fract(borderAngle * 16.0 - iTime * flowSpeed);
        float segPulse = smoothstep(0.0, 0.1, segments) * smoothstep(1.0, 0.9, segments);
        coreColor *= mix(0.6, 1.2, flow) * mix(0.8, 1.0, segPulse);

        if (isHighlighted) {
            float breathe = 0.85 + 0.15 * sin(iTime * 2.5 + energy * 3.0);
            coreColor *= breathe;
            float sparkle = pow(max(sin(borderAngle * TAU * 8.0 + iTime * 3.0), 0.0), 8.0);
            coreColor = mix(coreColor, accent * edgeGlow * borderEnergy, sparkle * 0.5);
        }

        coreColor = mix(coreColor, wireColor, core * mix(0.2, 0.5, vitality));

        if (hasAudio && bass > 0.5) {
            float flash = (bass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, bassCol * 2.0, flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core);
    }

    // ── Outer glow ────────────────────────────────────────

    float baseGlowR = mix(6.0, 16.0, vitality);
    float glowRadius = baseGlowR + (hasAudio ? bass * reactivity * 5.0 : sin(iTime * 0.8) * 2.0);
    glowRadius += energy * 4.0;
    if (d > 0.0 && d < glowRadius) {
        float glow1 = expGlow(d, glowRadius * 0.2, edgeGlow * mix(0.08, 0.25, vitality));
        float glow2 = expGlow(d, glowRadius * 0.5, edgeGlow * mix(0.03, 0.08, vitality));

        vec3 glowColor = primary;
        if (isHighlighted) {
            float glowAngle = atan(p.x, -p.y) / TAU + 0.5;
            glowColor = mix(primary, accent, angularNoise(glowAngle, 5.0, iTime * 0.6) * 0.5);
        }
        if (hasAudio && bass > 0.3) {
            glowColor = mix(glowColor, bassCol, (bass - 0.3) * vitality);
        }

        result.rgb += glowColor * (glow1 + glow2);
        result.a = max(result.a, (glow1 + glow2) * 0.4);
    }

    return result;
}

// ─── Main ───────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    bool  hasAudio = iAudioSpectrumSize > 0;
    // Raw audio (not soft-dampened) — this is an audio visualizer,
    // we want the full dynamic range of the signal.
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    // Vignette
    vec2 q = fragCoord / max(iResolution, vec2(1.0));
    float vig = 0.5 + 0.5 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.1);
    color.rgb *= vig;

    fragColor = clampFragColor(color);
}
