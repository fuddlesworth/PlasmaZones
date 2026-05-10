// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted glass panel shader for PhosphorShell (self-contained).
// Output is pre-multiplied alpha. SDF mask uses hard cutoff to avoid fringe.
//
// customParams[0]: x=tintOpacity y=noiseAmount z=noiseScale w=animSpeed
// customParams[1]: x=cornerRadius (pixels, default 12.0)
// customColors[0]: tint color (customColor1 in QML)

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

// SDF for a rounded rectangle centered at origin
float roundedBoxSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) - r;
}

// High-quality hash - avoids directional artifacts
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Smooth value noise with quintic interpolation
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    // Quintic Hermite for smoother derivatives
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Crystalline Voronoi noise - simulates frosted glass grain structure
float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    float secondMin = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            // Stable 2D hash for the cell point.
            vec2 cellId = i + neighbor;
            float h1 = hash(cellId);
            float h2 = hash(cellId + vec2(127.1, 311.7));
            vec2 point = vec2(h1, h2);
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
    // Edge distance gives crystalline cell boundaries
    return sqrt(secondMin) - sqrt(minDist);
}

// Multi-octave crystalline texture
float frostedTexture(vec2 p, float time) {
    float crystal = voronoi(p);
    float fine = voronoi(p * 2.3 + vec2(time * 0.1, time * 0.07));
    float micro = noise(p * 8.0 + time * 0.2);
    // Blend: dominant crystal structure + fine detail + micro noise
    return crystal * 0.6 + fine * 0.3 + micro * 0.1;
}

void main() {
    // Guard against zero iResolution at shader cold-start (the panel may
    // not have been sized yet when the first frame is composited). Without
    // this the fragCoord/iResolution divide produces NaN and the SDF mask
    // path collapses to undefined behaviour.
    if (iResolution.x <= 0.0 || iResolution.y <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    float tintOpacity = customParams[0].x > 0.0 ? customParams[0].x : 0.7;
    float noiseAmount = customParams[0].y > 0.0 ? customParams[0].y : 0.03;
    float noiseScale = customParams[0].z > 0.0 ? customParams[0].z : 40.0;
    float animSpeed = customParams[0].w > 0.0 ? customParams[0].w : 0.3;
    float radius = customParams[1].x > 0.0 ? customParams[1].x : 12.0;

    // SDF rounded rectangle mask - hard cutoff eliminates fringe
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);

    // Smooth AA edge - premultiplied alpha prevents fringe
    float mask = 1.0 - smoothstep(-1.0, 0.0, dist);

    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 tintColor = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 1.0);

    // Crystalline frosted glass texture
    vec2 noiseUv = uv * noiseScale;
    float frost = frostedTexture(noiseUv, iTime * animSpeed);

    // Variation centered around zero. Allowed in both directions — clamping
    // only the positive side (as an earlier revision did) made the noise
    // visibly one-sided (darker speckles only) and the effect ended up
    // imperceptible at any reasonable noiseAmount. The 0..1 clamp at the
    // bottom keeps us in valid colour space.
    float variation = (frost - 0.5) * noiseAmount;
    vec3 color = clamp(tintColor.rgb + vec3(variation), 0.0, 1.0);

    // Vignette darkens edges — applied multiplicatively so it never
    // increases brightness, just deepens the corners for depth cues.
    float vignette = 1.0 - length((uv - 0.5) * vec2(0.3, 1.0)) * 0.15;
    vignette = clamp(vignette, 0.0, 1.0);
    color *= vignette;

    // Final alpha
    float alpha = tintOpacity * mask;

    // Pre-multiplied alpha output: RGB * alpha before output
    fragColor = vec4(color * alpha, alpha) * qt_Opacity;
}
