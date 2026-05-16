// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Neon City — Buffer Pass (full-screen audio-reactive cityscape)
//
// DDA-style ray cast through an infinite grid of pixel buildings with
// neon signs and drifting street lights. Adapted from
// "Neon City" by nikitamashchenko (Shadertoy, CC-BY-NC-SA 3.0).
//
// Y is flipped from Shadertoy: shadertoy fragCoord.y = 0 at bottom,
// PlasmaZones vFragCoord.y = 0 at top. We flip once in main().
//
// Audio mapping:
//   bass    — window brightness + probability, street-light intensity,
//             fog lift, overall brightness lift
//   mids    — window palette shift, fog tint toward window color
//   treble  — per-row window flicker, sign flash rate, character flicker

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float oDepth;

#include <common.glsl>
#include <audio.glsl>

// ─── Shadertoy-style hashes (fract-based) ────────────────────────────────────
// Renamed to nHash* to avoid clashing with integer hash11/hash21/hash22 in
// common.glsl. These are not cross-compiler identical, which is fine for this
// non-feedback shader — the original aesthetic depends on the exact output.

float nHash1(float p) {
    vec3 p3 = fract(p * vec3(5.3983, 5.4427, 6.9371));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

float nHash1(vec2 p2) {
    p2 = fract(p2 * vec2(5.3983, 5.4427));
    p2 += dot(p2.yx, p2.xy + vec2(21.5351, 14.3137));
    return fract(p2.x * p2.y * 95.4337);
}

float nHash1(vec2 p2, float p) {
    vec3 p3 = fract(vec3(5.3983 * p2.x, 5.4427 * p2.y, 6.9371 * p));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 nHash2(vec2 p2, float p) {
    vec3 p3 = fract(vec3(5.3983 * p2.x, 5.4427 * p2.y, 6.9371 * p));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 nHash3(vec2 p2) {
    vec3 p3 = fract(vec3(p2.xyx) * vec3(5.3983, 5.4427, 6.9371));
    p3 += dot(p3, p3.yxz + 19.19);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

vec4 nHash4(vec2 p2) {
    vec4 p4 = fract(p2.xyxy * vec4(5.3983, 5.4427, 6.9371, 7.1283));
    p4 += dot(p4, p4.yxwz + 19.19);
    return fract((p4.xxxy + p4.yyzz + p4.zwww) * p4.wzyx);
}

float nNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(nHash1(i + vec2(0.0, 0.0)),
                   nHash1(i + vec2(1.0, 0.0)), u.x),
               mix(nHash1(i + vec2(0.0, 1.0)),
                   nHash1(i + vec2(1.0, 1.0)), u.x), u.y);
}

// ─── Scene config ────────────────────────────────────────────────────────────
// In the original, these were #ifdef-switched compile-time constants.  We pack
// them into a struct and select at runtime so the same compiled shader can
// toggle between the slow/default and fast-descent modes.

struct Scene {
    vec3 cameraDir;
    float cameraDist;
    float speed;
    float zoom;
    vec3 windowColorA;
    vec3 windowColorB;
    float fogOffset;
    float fogDensity;
    vec3 fogColor;
    float lightHeight;
    float lightSpeed;
    vec3 lightColorA;
    vec3 lightColorB;
    vec3 signColorA;
    vec3 signColorB;
};

// ─── Ray cast ────────────────────────────────────────────────────────────────

vec4 castRay(vec3 eye, vec3 ray, vec2 worldCenter, Scene sc) {
    vec2 block = floor(eye.xy);
    vec3 ri = 1.0 / ray;
    vec3 rs = sign(ray);
    vec3 side = 0.5 + 0.5 * rs;
    vec2 ris = ri.xy * rs.xy;
    vec2 dis = (block - eye.xy + 0.5 + rs.xy * 0.5) * ri.xy;

    // 20 iterations: matches/exceeds the Shadertoy's 16-step reach. The height
    // formula 3h-1+1.5d-0.1d² peaks around d≈7.5 so buildings exist roughly
    // for d∈[0,15]; 20 DDA steps at |cameraDir.xy|·step≈0.7 advances d by ~14,
    // just enough to reach the far edge of the visible band.
    for (int i = 0; i < 20; ++i) {
        float d = dot(block - worldCenter, sc.cameraDir.xy);
        float height = 3.0 * nHash1(block) - 1.0 + 1.5 * d - 0.1 * d * d;

        vec2 lo0 = vec2(block);
        vec2 loX = vec2(0.45, 0.45);
        vec2 hi0 = vec2(block + 0.55);
        vec2 hiX = vec2(0.45, 0.45);

        float dist = 500.0;
        float face = 0.0;

        // Sign billboard (one per block, oriented randomly on X or Y wall)
        {
            vec4 signHash = nHash4(block);
            vec2 sCenter = vec2(0.2, -0.4) + vec2(0.6, -0.8) * signHash.xy;
            float width = 0.06 + 0.1 * signHash.w;

            vec3 lo = vec3(sCenter.x - width, 0.55, -100.0);
            vec3 hi = vec3(sCenter.x + width, 0.99, sCenter.y + width + height);

            float s = step(0.5, signHash.z);
            lo = vec3(block, 0.0) + mix(lo, lo.yxz, s);
            hi = vec3(block, 0.0) + mix(hi, hi.yxz, s);

            vec3 wall = mix(hi, lo, side);
            vec3 t = (wall - eye) * ri;

            vec3 dim = step(t.zxy, t) * step(t.yzx, t);
            float maxT = dot(dim, t);
            float maxFace = dim.x - dim.y;

            vec3 p = eye + maxT * ray;
            dim += step(lo, p) * step(p, hi);

            if (dim.x * dim.y * dim.z > 0.5) {
                dist = maxT;
                face = maxFace;
            }
        }

        // Five stacked building box layers — each one shrunk slightly
        for (int j = 0; j < 5; ++j) {
            float top = height - 0.4 * float(j);
            vec3 lo = vec3(lo0 + loX * nHash2(block, float(j)), -100.0);
            vec3 hi = vec3(hi0 + hiX * nHash2(block, float(j) + 0.5), top);

            vec3 wall = mix(hi, lo, side);
            vec3 t = (wall - eye) * ri;

            vec3 dim = step(t.zxy, t) * step(t.yzx, t);
            float maxT = dot(dim, t);
            float maxFace = dim.x - dim.y;

            vec3 p = eye + maxT * ray;
            dim += step(lo, p) * step(p, hi);

            if (dim.x * dim.y * dim.z > 0.5 && maxT < dist) {
                dist = maxT;
                face = maxFace;
            }
        }

        if (dist < 400.0) {
            return vec4(dist, height, face, 1.0);
        }

        // DDA step (the unused t/p/g locals from the original are dropped)
        vec2 dim = step(dis.xy, dis.yx);
        dis += dim * ris;
        block += dim * rs.xy;
    }

    return vec4(100.0, 0.0, 0.0, 1.0);
}

// ─── Surface shading ─────────────────────────────────────────────────────────

vec3 windowColor(float z, vec2 pos, vec2 id,
                 float aBass, float aMids, float aTreble,
                 vec3 windowBoost, Scene sc) {
    float windowSize = 0.03 + 0.12 * nHash1(id + 0.1);
    float windowProb = 0.3 + 0.8 * nHash1(id + 0.2);

    // Bass shifts the probability threshold up so dim rows light up on the
    // beat — the city visibly "wakes up" with the kick.
    windowProb += aBass * 0.18;

    float depth = z / windowSize;
    float level = floor(depth);
    vec3 cA = mix(sc.windowColorA, sc.windowColorB, nHash3(id));
    vec3 cB = mix(sc.windowColorA, sc.windowColorB, nHash3(id + 0.1));
    vec3 color = mix(cA, cB, nHash1(id, level));
    // Mids shift the palette toward accent/boost
    color = mix(color, windowBoost, aMids * 0.35);
    color *= 0.3 + 0.7 * smoothstep(0.1, 0.5, nNoise(20.0 * pos + 100.0 * nHash1(level)));
    color *= smoothstep(windowProb - 0.2, windowProb + 0.2, nHash1(id, level + 0.1));

    // Treble-driven per-row flicker. Each window row has its own unique
    // seed (id+level); adding a time-quantized slot reshuffles the pattern
    // — higher treble → faster reshuffle and lower threshold → more rows
    // flashing on at once. Result reads like a club billboard blinking on
    // the hi-hat.
    if (aTreble > 0.01) {
        float slot = floor(iTime * (3.0 + aTreble * 10.0));
        float blink = nHash1(id + 0.3, level + slot * 0.07);
        float flashMask = step(0.55 - aTreble * 0.35, blink);
        color *= 1.0 + flashMask * aTreble * 0.9;
    }

    // Bass pumps overall window brightness — windows pulse with the kick
    // even when they're steady-state lit.
    color *= 1.0 + aBass * 0.5;

    return color * (0.5 - 0.5 * cos(TAU * depth));
}

vec3 addLight(vec3 eye, vec3 ray, float res, float time, float height,
              float bassBoost, Scene sc) {
    vec2 q = eye.xy + ((height - eye.z) / ray.z) * ray.xy;

    float row = floor(q.x + 0.5);
    time += nHash1(row);
    float col = floor(0.125 * q.y - time);

    float pos = 0.4 + 0.4 * cos(time + TAU * nHash1(vec2(row, col)));
    vec3 lightPos = vec3(row, 8.0 * (col + time + pos), height);
    vec3 lightDir = vec3(0.0, 1.0, 0.0);

    // http://geomalgorithms.com/a07-_distance.html
    vec3 w = eye - lightPos;
    float a = dot(ray, ray);
    float b = dot(ray, lightDir);
    float c = dot(lightDir, lightDir);
    float d = dot(ray, w);
    float e = dot(lightDir, w);
    // Guard against degenerate geometry: when ray is parallel to lightDir
    // (±Y), D = a*c - b*b → 0, and the subsequent divisions produce NaN.
    // Four axis-swapped addLight calls multiply the chance of hitting this.
    float D = max(a * c - b * b, 1e-6);
    float s = (b*e - c*d) / D;
    float t = (a*e - b*d) / D;

    t = max(t, 0.0);
    float dist = distance(eye + s * ray, lightPos + t * lightDir);

    float mask = smoothstep(res + 0.1, res, s);
    float light = min(1.0 / pow(200.0 * dist * dist / t + 20.0 * t * t, 0.8), 2.0);
    float fog = exp(-sc.fogDensity * max(s - sc.fogOffset, 0.0));
    vec3 color = mix(sc.lightColorA, sc.lightColorB, nHash3(vec2(row, col)));
    return mask * light * fog * color * (1.0 + bassBoost);
}

vec3 addSign(vec3 color, vec3 pos, float side, vec2 id, float trebleBoost, Scene sc) {
    vec4 signHash = nHash4(id);
    float s = step(0.5, signHash.z);
    if ((s - 0.5) * side < 0.1)
        return color;

    vec2 sCenter = vec2(0.2, -0.4) + vec2(0.6, -0.8) * signHash.xy;
    vec2 p = mix(pos.xz, pos.yz, s);
    float halfWidth = 0.04 + 0.06 * signHash.w;

    float charCount = floor(1.0 + 8.0 * nHash1(id + 0.5));
    if (sCenter.y - p.y > 2.0 * halfWidth * (charCount + 1.0)) {
        sCenter.y -= 2.0 * halfWidth * (charCount + 1.5 + 5.0 * nHash1(id + 0.6));
        charCount = floor(2.0 + 12.0 * nHash1(id + 0.7));
        id += 0.05;
    }

    vec3 signColor    = mix(sc.signColorA, sc.signColorB, nHash3(id + 0.5));
    vec3 outlineColor = mix(sc.signColorA, sc.signColorB, nHash3(id + 0.6));
    float flash = 6.0 - 24.0 * nHash1(id + 0.8);
    flash *= step(3.0, flash);
    // Treble speeds up the flash rate
    flash *= 1.0 + trebleBoost * 0.75;
    flash = smoothstep(0.1, 0.5, 0.5 + 0.5 * cos(flash * iTime));

    vec2 halfSize = vec2(halfWidth, halfWidth * charCount);
    sCenter.y -= halfSize.y;
    float outline = length(max(abs(p - sCenter) - halfSize, 0.0)) / halfWidth;
    color *= smoothstep(0.1, 0.4, outline);

    vec2 charPos = 0.5 * (p - sCenter + halfSize) / halfWidth;
    vec2 charId = id + 0.05 + 0.1 * floor(charPos);
    float flicker = nHash1(charId);
    // Treble lowers the flicker threshold → more characters blinking
    flicker = step(0.93 - trebleBoost * 0.08, flicker);
    flicker = 1.0 - flicker * step(0.96, nHash1(charId, iTime));

    float ch = -3.5 + 8.0 * nNoise(id + 6.0 * charPos);
    charPos = fract(charPos);
    ch *= smoothstep(0.0, 0.4, charPos.x) * smoothstep(1.0, 0.6, charPos.x);
    ch *= smoothstep(0.0, 0.4, charPos.y) * smoothstep(1.0, 0.6, charPos.y);
    color = mix(color, signColor, flash * flicker * step(outline, 0.01) * clamp(ch, 0.0, 1.0));

    outline = smoothstep(0.0, 0.2, outline) * smoothstep(0.5, 0.3, outline);
    return mix(color, outlineColor, flash * outline);
}

// ─── Main ────────────────────────────────────────────────────────────────────

void main() {
    // Y-flip: Shadertoy fragCoord has Y=0 at bottom; PlasmaZones vFragCoord is
    // Y=0 at top. Flipping once here fixes the camera up-vector and all screen
    // coordinates in one place.
    vec2 fragCoord = vec2(vFragCoord.x, iResolution.y - vFragCoord.y);

    // ── Parameters ──────────────────────────────────────────
    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float speedMul     = customParams[0].y >= 0.0 ? customParams[0].y : 1.0;
    float lightSpdMul  = customParams[0].z >= 0.0 ? customParams[0].z : 1.0;
    // Bool params from metadata arrive as 0.0 (false) / 1.0 (true); >0.5 picks "true".
    float fastMode     = customParams[0].w;

    float cameraDistP  = customParams[1].x;  // < 0 = scene default
    float zoomP        = customParams[1].y;
    float fogDensityP  = customParams[1].z;
    float fogOffsetP   = customParams[1].w;

    float bassImpact   = customParams[2].x >= 0.0 ? customParams[2].x : 1.5;
    float trebleImpact = customParams[2].y >= 0.0 ? customParams[2].y : 1.5;
    float midsImpact   = customParams[2].z >= 0.0 ? customParams[2].z : 1.0;
    // customParams[2].w = idleSpeed (consumed in effect pass only)

    // ── Scene selection ────────────────────────────────────
    Scene sc;
    if (fastMode > 0.5) {
        sc.cameraDir   = normalize(vec3(-2.0, -1.0, -4.0));
        sc.cameraDist  = 5.0;
        sc.speed       = 3.0;
        sc.zoom        = 2.5;
        sc.fogOffset   = 2.5;
        sc.fogDensity  = 0.6;
        sc.lightHeight = 0.5;
        sc.lightSpeed  = 0.2;
    } else {
        sc.cameraDir   = normalize(vec3(-2.0, -1.0, -2.0));
        sc.cameraDist  = 9.0;
        sc.speed       = 1.0;
        sc.zoom        = 3.5;
        sc.fogOffset   = 7.0;
        sc.fogDensity  = 0.7;
        sc.lightHeight = 0.0;
        sc.lightSpeed  = 0.15;
    }
    if (cameraDistP > 0.0) sc.cameraDist = cameraDistP;
    if (zoomP       > 0.0) sc.zoom       = zoomP;
    if (fogDensityP > 0.0) sc.fogDensity = fogDensityP;
    if (fogOffsetP  >= 0.0) sc.fogOffset = fogOffsetP;

    sc.speed      *= speedMul;
    sc.lightSpeed *= lightSpdMul;

    // ── Colors ──────────────────────────────────────────────
    vec3 primary = colorWithFallback(customColors[0].rgb, vec3(0.25, 0.00, 0.30));  // fog
    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(0.50, 1.50, 2.00));  // window B / accent
    vec3 bassCol = colorWithFallback(customColors[2].rgb, vec3(0.00, 0.00, 1.50));  // window A / sign A
    vec3 lightC  = colorWithFallback(customColors[3].rgb, vec3(0.80, 0.45, 0.18));  // street light

    sc.fogColor     = primary;
    sc.windowColorA = bassCol;
    sc.windowColorB = accent;
    sc.signColorA   = bassCol;
    sc.signColorB   = vec3(3.0);                   // pure bright neon white
    sc.lightColorA  = lightC * 0.75;
    sc.lightColorB  = mix(lightC, vec3(1.0, 0.75, 0.45), 0.5);

    // ── Audio ───────────────────────────────────────────────
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBass();
    float mids     = getMids();
    float treble   = getTreble();

    float aBass   = hasAudio ? bass   * reactivity * bassImpact   : 0.0;
    float aMids   = hasAudio ? mids   * reactivity * midsImpact   : 0.0;
    float aTreble = hasAudio ? treble * reactivity * trebleImpact : 0.0;

    // Bass lifts fog (clearer on beat); floor at 0.3x density so it never clears completely
    sc.fogDensity *= clamp(1.0 - aBass * 0.3, 0.3, 1.0);
    sc.fogColor   = mix(sc.fogColor, bassCol * 0.65, aMids * 0.25);

    // ── Camera ──────────────────────────────────────────────
    vec2 worldCenter = -sc.speed * iTime * sc.cameraDir.xy;
    vec3 eye = vec3(worldCenter, 0.0) - sc.cameraDist * sc.cameraDir;

    vec3 forward = normalize(sc.cameraDir);
    vec3 right = normalize(cross(forward, vec3(0.0, 0.0, 1.0)));
    vec3 up = cross(right, forward);
    vec2 xy = 2.0 * fragCoord - iResolution;
    vec3 ray = normalize(xy.x * right + xy.y * up + sc.zoom * forward * iResolution.y);

    vec4 res = castRay(eye, ray, worldCenter, sc);
    vec3 p = eye + res.x * ray;

    vec2 block = floor(p.xy);
    vec3 windowBoost = mix(sc.windowColorB, accent * 1.2, 0.5);
    vec3 col = windowColor(p.z - res.y, p.xy, block,
                           aBass, aMids, aTreble, windowBoost, sc);

    col = addSign(col, vec3(p.xy - block, p.z - res.y), res.z, block, aTreble, sc);
    col = mix(vec3(0.0), col, abs(res.z));

    // Height-based dissolve: the Shadertoy height formula
    //   height = 3h - 1 + 1.5d - 0.1d²
    // means buildings at the lateral/far edges of the visible band shrink
    // to height≈0 and then pop once height goes negative (AABB collapses).
    // Fade the building toward fog only in the last ~1 world-unit of
    // height so the pop is hidden but regular short buildings stay vivid.
    // `res.x < 99` skips misses (castRay returns dist=100 on miss).
    if (res.x < 99.0) {
        float heightFade = smoothstep(-0.3, 0.7, res.y);
        col = mix(sc.fogColor, col, heightFade);
    }

    float fog = exp(-sc.fogDensity * max(res.x - sc.fogOffset, 0.0));
    col = mix(sc.fogColor, col, fog);

    float time = sc.lightSpeed * iTime;
    float lightBoost = aBass * 0.6;
    col += addLight(eye.xyz, ray.xyz, res.x, time, sc.lightHeight - 0.6, lightBoost, sc);
    col += addLight(eye.yxz, ray.yxz, res.x, time, sc.lightHeight - 0.4, lightBoost, sc);
    col += addLight(vec3(-eye.xy, eye.z), vec3(-ray.xy, ray.z), res.x, time,
                    sc.lightHeight - 0.2, lightBoost, sc);
    col += addLight(vec3(-eye.yx, eye.z), vec3(-ray.yx, ray.z), res.x, time,
                    sc.lightHeight, lightBoost, sc);

    // Subtle bass-driven brightness lift
    col *= 1.0 + aBass * 0.08;

    fragColor = vec4(col, 1.0);

    // Normalized depth for DOF in the effect pass. castRay returns 100.0 on
    // miss; real hits are < ~40 at the far edge of visibility given the DDA
    // budget (20 iters). Normalize by 40.
    oDepth = clamp(res.x * (1.0 / 40.0), 0.0, 1.0);
}
