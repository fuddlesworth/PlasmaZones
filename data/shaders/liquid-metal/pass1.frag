// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Metal — Buffer Pass 1: Normal Reconstruction + Environment Map
// Reads the height field from pass0 (iChannel0), reconstructs surface normals
// via finite differences, then renders the metallic surface with environment
// reflections and specular highlights.
// Output: RGB = lit metal surface, A = 1.0.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

// Procedural environment map: high-contrast studio lighting look
vec3 envMap(vec3 dir, vec3 warmClr, vec3 coolClr, float rotation) {
    // Rotate reflection direction around Y axis
    float c = cos(rotation);
    float s = sin(rotation);
    vec3 rd = vec3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);

    // Dark base — makes the bright areas pop
    vec3 sky = vec3(0.03);

    // Vertical gradient bands: alternating warm/cool strips (like studio lighting)
    float skyGrad = rd.y * 0.5 + 0.5;
    sky += mix(warmClr * 0.4, coolClr * 0.6, smoothstep(0.3, 0.7, skyGrad));

    // Bright horizon band (ground plane reflection — key visual feature)
    float horizon = exp(-rd.y * rd.y * 8.0);
    sky += warmClr * horizon * 1.2;

    // Upper fill light (cool)
    float upper = smoothstep(0.5, 0.9, skyGrad);
    sky += coolClr * upper * 0.5;

    // Primary sun — sharp highlight
    vec3 sunDir = normalize(vec3(0.5, 0.8, 0.3));
    float sunDot = max(0.0, dot(rd, sunDir));
    sky += vec3(1.0, 0.95, 0.8) * pow(sunDot, 128.0) * 3.0;
    sky += vec3(1.0, 0.9, 0.7) * pow(sunDot, 16.0) * 0.5;

    // Secondary light (opposite side, cooler)
    vec3 fillDir = normalize(vec3(-0.6, 0.3, -0.5));
    float fillDot = max(0.0, dot(rd, fillDir));
    sky += coolClr * pow(fillDot, 32.0) * 1.5;

    return sky;
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec2 uv = fragCoord / iResolution;
    vec2 texel = 1.0 / max(iResolution, vec2(1.0));

    // Read height field from pass0
    float hC = texture(iChannel0, uv).r * 2.0 - 1.0;
    float hR = texture(iChannel0, uv + vec2(texel.x, 0.0)).r * 2.0 - 1.0;
    float hU = texture(iChannel0, uv + vec2(0.0, texel.y)).r * 2.0 - 1.0;
    float caustic = texture(iChannel0, uv).b;

    // Load parameters
    float reflStr = customParams[1].x > 0.001 ? customParams[1].x : 0.85;
    float fresnelPow = customParams[1].y > 0.5 ? customParams[1].y : 3.0;
    float specSharp = customParams[1].z > 4.0 ? customParams[1].z : 64.0;
    float envRotSpeed = customParams[2].x;

    vec3 metalClr = colorWithFallback(customColors[0].rgb, vec3(0.69, 0.75, 0.77));
    vec3 warmClr = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.83, 0.63));
    vec3 coolClr = colorWithFallback(customColors[2].rgb, vec3(0.376, 0.565, 0.753));

    // Reconstruct normal from height differences — higher scale = more visible ripples
    float normalScale = 0.55;
    vec3 normal = normalize(vec3(
        (hC - hR) / texel.x * normalScale,
        (hC - hU) / texel.y * normalScale,
        1.0
    ));

    // View direction (straight down for a flat overlay)
    vec3 viewDir = vec3(0.0, 0.0, 1.0);

    // Reflection vector
    vec3 refl = reflect(-viewDir, normal);

    // Fresnel: more reflection at glancing angles (metals have high base reflectivity)
    float fresnel = pow(1.0 - max(0.0, dot(normal, viewDir)), fresnelPow);
    fresnel = mix(0.4, 1.0, fresnel);

    // Environment reflection
    float envRot = iTime * envRotSpeed;
    vec3 envClr = envMap(refl, warmClr, coolClr, envRot);

    // Key light — broad specular
    vec3 lightDir = normalize(vec3(0.3, 0.5, 1.0));
    vec3 halfVec = normalize(viewDir + lightDir);
    float spec = pow(max(0.0, dot(normal, halfVec)), specSharp);

    // Rim/back light — catches edges for definition
    vec3 rimDir = normalize(vec3(-0.4, -0.3, 0.6));
    vec3 rimHalf = normalize(viewDir + rimDir);
    float rimSpec = pow(max(0.0, dot(normal, rimHalf)), specSharp * 0.5) * 0.6;

    // Compose metal surface
    vec3 col = metalClr * 0.08; // dark ambient
    col += metalClr * envClr * fresnel * reflStr; // reflection
    col += vec3(1.0, 0.98, 0.94) * spec * 2.0; // key specular
    col += coolClr * rimSpec; // rim specular
    col += metalClr * caustic * 0.3; // caustic shimmer

    // Edge darkening in valleys (Fresnel-based concavity)
    float curvature = length(vec2(hC - hR, hC - hU)) * 30.0;
    col += warmClr * curvature * 0.08;

    fragColor = vec4(max(col, 0.0), 1.0);
}
