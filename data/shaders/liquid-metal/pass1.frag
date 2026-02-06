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

// Procedural environment map: studio strip lights against dark background.
// Strip lights create the classic chrome look — bright bands that visibly
// warp and slide across the deforming surface.
vec3 envMap(vec3 dir, vec3 warmClr, vec3 coolClr, float rotation) {
    // Rotate reflection direction around Y axis
    float c = cos(rotation);
    float s = sin(rotation);
    vec3 rd = vec3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);

    float y = rd.y;    // vertical angle — drives horizontal bands on surface
    float x = rd.x;    // horizontal angle — drives vertical variation

    // Very dark base — essential for contrast
    vec3 sky = vec3(0.02);

    // Strip light 1: broad warm horizon band (the dominant visual feature)
    sky += warmClr * 1.8 * exp(-y * y * 12.0);

    // Strip light 2: narrow bright white band above horizon
    sky += vec3(1.2) * exp(-pow((y - 0.35) * 8.0, 2.0));

    // Strip light 3: cool band higher up
    sky += coolClr * 1.2 * exp(-pow((y - 0.7) * 7.0, 2.0));

    // Strip light 4: subtle warm accent from below (ground bounce)
    sky += warmClr * 0.5 * exp(-pow((y + 0.5) * 6.0, 2.0));

    // Vertical accent: angled fill strip for left-right asymmetry
    float diag = (x + y) * 0.7;
    sky += coolClr * 0.3 * exp(-pow((diag - 0.2) * 6.0, 2.0));

    // Pinpoint sun highlight
    vec3 sunDir = normalize(vec3(0.4, 0.7, 0.3));
    float sunDot = max(0.0, dot(rd, sunDir));
    sky += vec3(1.5, 1.4, 1.2) * pow(sunDot, 256.0) * 3.0;

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

    // Reconstruct normal — normalScale drives how dramatically the surface
    // distorts reflections. Higher = more visible ripples/bands.
    float normalScale = 1.0;
    vec3 normal = normalize(vec3(
        (hC - hR) / texel.x * normalScale,
        (hC - hU) / texel.y * normalScale,
        1.0
    ));

    // View direction (straight down for a flat overlay)
    vec3 viewDir = vec3(0.0, 0.0, 1.0);

    // Reflection vector
    vec3 refl = reflect(-viewDir, normal);

    // Fresnel: steep edges go bright, flat areas stay darker
    float NdotV = max(0.0, dot(normal, viewDir));
    float fresnel = pow(1.0 - NdotV, fresnelPow);
    fresnel = mix(0.5, 1.0, fresnel); // high base reflectivity (it's metal)

    // Environment reflection — this is where the strip lights show up
    float envRot = iTime * envRotSpeed;
    vec3 envClr = envMap(refl, warmClr, coolClr, envRot);

    // Key light specular
    vec3 lightDir = normalize(vec3(0.3, 0.5, 1.0));
    vec3 halfVec = normalize(viewDir + lightDir);
    float spec = pow(max(0.0, dot(normal, halfVec)), specSharp);

    // Compose metal surface: reflection is primary, everything else accents it
    vec3 col = metalClr * envClr * fresnel * reflStr;
    col += vec3(1.0, 0.98, 0.94) * spec * 2.5; // specular punch
    col += metalClr * caustic * 0.4; // caustic shimmer

    // Darken concave areas (valleys between ripples)
    float valley = 1.0 - smoothstep(0.0, 0.15, abs(hC));
    col *= mix(1.0, 0.6, valley * 0.3);

    fragColor = vec4(max(col, 0.0), 1.0);
}
