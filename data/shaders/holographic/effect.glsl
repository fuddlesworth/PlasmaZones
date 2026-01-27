// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 330 core

in vec2 vTexCoord;
out vec4 fragColor;

layout(std140) uniform ZoneUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1)
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    vec4 customColors[8];  // [0-7], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

/*
 * HOLOGRAPHIC - Sci-Fi Hologram Projection Effect
 * 
 * Features: RGB separation, projection lines, data streams,
 * holographic shimmer, glitch distortion, edge projection glow
 * 
 * Parameters:
 *   customParams[0].x = rgbSeparation (3.0-15.0) - Chromatic aberration
 *   customParams[0].y = projectionLines (0.2-0.8) - Horizontal line intensity
 *   customParams[0].z = glitchAmount (0.0-0.5) - Glitch distortion
 *   customParams[0].w = shimmerSpeed (1.0-4.0) - Iridescent shimmer speed
 *   customParams[1].x = baseOpacity (0.5-0.9) - Hologram opacity
 *   customParams[1].y = dataStreamIntensity (0.0-1.0) - Flowing data effect
 */

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float hash(float n) { return fract(sin(n) * 43758.5453123); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Smooth noise for data streams
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x),
        mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x),
        f.y
    );
}

vec4 renderHolographicZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 4.0);
    float borderWidth = max(params.y, 2.0);
    
    float rgbSeparation = customParams[0].x > 0.5 ? customParams[0].x : 8.0;
    float projectionLines = customParams[0].y > 0.05 ? customParams[0].y : 0.4;
    float glitchAmount = customParams[0].z > 0.001 ? customParams[0].z : 0.2;
    float shimmerSpeed = customParams[0].w > 0.1 ? customParams[0].w : 2.0;
    float baseOpacity = customParams[1].x > 0.1 ? customParams[1].x : 0.75;
    float dataStreamIntensity = customParams[1].y > 0.01 ? customParams[1].y : 0.6;
    
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    
    // Glitch displacement
    float glitchTime = floor(iTime * 15.0);
    float glitchTrigger = hash(glitchTime);
    vec2 glitchOffset = vec2(0.0);
    
    if (glitchTrigger > 0.7 && glitchAmount > 0.01) {
        float glitchY = hash(glitchTime + 50.0);
        float glitchHeight = 0.05 + hash(glitchTime + 100.0) * 0.1;
        if (abs(localUV.y - glitchY) < glitchHeight) {
            glitchOffset.x = (hash(glitchTime + 200.0) - 0.5) * glitchAmount * rectSize.x * 0.1;
        }
    }
    
    vec2 p = (fragCoord + glitchOffset) - center;
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Hologram base color
    vec3 holoColor = customColors[0].rgb;
    if (length(holoColor) < 0.01) holoColor = fillColor.rgb;
    if (length(holoColor) < 0.01) holoColor = vec3(0.0, 1.0, 0.8);  // #00ffcc
    
    vec3 accentColor = customColors[1].rgb;
    if (length(accentColor) < 0.01) accentColor = vec3(1.0, 0.0, 1.0);  // #ff00ff magenta
    
    if (isHighlighted) {
        holoColor = mix(holoColor, accentColor, 0.5);
        baseOpacity += 0.1;
        rgbSeparation *= 1.5;
    }
    
    vec4 result = vec4(0.0);
    
    // Main hologram body with RGB separation
    if (d < 0.0) {
        // Calculate RGB offsets for chromatic aberration
        float sepX = rgbSeparation / rectSize.x;
        float sepY = rgbSeparation * 0.3 / rectSize.y;
        
        // Red channel - offset left and slightly up
        vec2 uvR = localUV + vec2(-sepX, sepY * 0.5);
        float dR = sdRoundedBox((uvR - 0.5) * rectSize, rectSize * 0.5, borderRadius);
        
        // Green channel - center (no offset)
        float dG = d;
        
        // Blue channel - offset right and slightly down
        vec2 uvB = localUV + vec2(sepX, -sepY * 0.5);
        float dB = sdRoundedBox((uvB - 0.5) * rectSize, rectSize * 0.5, borderRadius);
        
        // Sample each channel
        float r = dR < 0.0 ? 1.0 : 0.0;
        float g = dG < 0.0 ? 1.0 : 0.0;
        float b = dB < 0.0 ? 1.0 : 0.0;
        
        vec3 rgb = vec3(r, g, b) * holoColor;
        
        // Holographic shimmer/iridescence
        float shimmer = sin(localUV.x * 30.0 + localUV.y * 20.0 + iTime * shimmerSpeed) * 0.5 + 0.5;
        shimmer = pow(shimmer, 2.0);
        vec3 shimmerColor = mix(holoColor, accentColor, shimmer * 0.4);
        rgb = mix(rgb, shimmerColor, 0.3);
        
        // Projection lines (horizontal bands)
        float lineY = localUV.y * 150.0 + iTime * 30.0;
        float projLine = sin(lineY) * 0.5 + 0.5;
        projLine = pow(projLine, 8.0); // Sharp lines
        rgb *= 1.0 - projLine * projectionLines * 0.5;
        
        // Subtle vertical scanlines
        float scanV = sin(localUV.x * 400.0) * 0.5 + 0.5;
        rgb *= 0.95 + scanV * 0.05;
        
        // Data stream effect - flowing characters/noise
        if (dataStreamIntensity > 0.01) {
            float streamY = localUV.y + iTime * 0.5;
            float stream = noise(vec2(localUV.x * 20.0, streamY * 30.0));
            stream = step(0.7, stream) * 0.8;
            
            // Vertical data columns
            float col = sin(localUV.x * 50.0) * 0.5 + 0.5;
            col = step(0.8, col);
            
            float dataEffect = stream * col * dataStreamIntensity;
            rgb += holoColor * dataEffect * 0.5;
        }
        
        // Edge glow (brighter near edges)
        float edgeDist = -d;
        float edgeGlow = exp(-edgeDist / 30.0) * 0.6;
        rgb += holoColor * edgeGlow;
        
        // Flicker
        float flicker = 0.9 + 0.1 * sin(iTime * 17.0) * sin(iTime * 23.0);
        rgb *= flicker;
        
        // Occasional bright flash lines
        float flashLine = hash(floor(iTime * 8.0));
        if (flashLine > 0.92) {
            float flashY = hash(floor(iTime * 8.0) + 300.0);
            if (abs(localUV.y - flashY) < 0.02) {
                rgb += vec3(0.5);
            }
        }
        
        result.rgb = rgb;
        result.a = baseOpacity * max(max(r, g), b);
    }
    
    // Holographic border with strong RGB separation
    float borderDist = abs(d);
    if (borderDist < borderWidth + rgbSeparation) {
        float border = 1.0 - smoothstep(0.0, borderWidth, borderDist);
        
        // RGB border with offset
        float bR = 1.0 - smoothstep(0.0, borderWidth, abs(sdRoundedBox(p + vec2(rgbSeparation * 0.5, 0.0), rectSize * 0.5, borderRadius)));
        float bG = border;
        float bB = 1.0 - smoothstep(0.0, borderWidth, abs(sdRoundedBox(p - vec2(rgbSeparation * 0.5, 0.0), rectSize * 0.5, borderRadius)));
        
        vec3 borderRGB;
        borderRGB.r = holoColor.r * bR * 1.8;
        borderRGB.g = holoColor.g * bG * 1.8;
        borderRGB.b = holoColor.b * bB * 1.8;
        
        // Add white core
        float whiteCore = border * border;
        borderRGB = mix(borderRGB, vec3(1.0), whiteCore * 0.5);
        
        float borderAlpha = max(max(bR, bG), bB);
        result.rgb = mix(result.rgb, borderRGB, borderAlpha);
        result.a = max(result.a, borderAlpha * 0.95);
    }
    
    // Projection cone effect - light rays from bottom
    if (d > 0.0 && d < 50.0) {
        // Bottom edge glow (projector source)
        float bottomProximity = 1.0 - localUV.y;
        if (bottomProximity > 0.9 && localUV.x > 0.1 && localUV.x < 0.9) {
            float projGlow = (bottomProximity - 0.9) / 0.1;
            projGlow *= exp(-d / 20.0);
            result.rgb += holoColor * projGlow * 0.4;
            result.a = max(result.a, projGlow * 0.3);
        }
        
        // Side edge glow
        float edgeGlow = exp(-d / 15.0) * 0.4;
        result.rgb += holoColor * edgeGlow * 0.3;
        result.a = max(result.a, edgeGlow * 0.3);
    }
    
    return result;
}

void main() {
    vec2 fragCoord = vec2(vTexCoord.x, 1.0 - vTexCoord.y) * iResolution;
    vec4 color = vec4(0.0);
    
    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }
    
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        vec4 zoneColor = renderHolographicZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        // Additive blend for hologram
        color.rgb += zoneColor.rgb * zoneColor.a;
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
