// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 330 core

// Aretha Shell - Ghost in the Shell / Aretha Dark Layered Shader
// Inspired by MonoBall's stacked shaders: NeonGrade -> HexGrid -> DataStream
// Three-layer composite: Color Grade + Hex Grid + Data Streams
// Ported to PlasmaZones zone overlay system

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
    vec4 iMouse;
    vec4 customParams[4];
    vec4 customColors[8];  // [0-7], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

// === PARAMETER ACCESS ===
// Slot 0-3: customParams[0].xyzw
// Slot 4-7: customParams[1].xyzw
// Slot 8-11: customParams[2].xyzw
// Slot 12-15: customParams[3].xyzw

float getSpeed()           { return customParams[0].x > 0.001 ? customParams[0].x : 0.08; }
float getGradeIntensity()  { return customParams[0].y > 0.001 ? customParams[0].y : 0.35; }
float getGradeSaturation() { return customParams[0].z > 0.1 ? customParams[0].z : 1.08; }
float getShimmerIntensity(){ return customParams[0].w > 0.001 ? customParams[0].w : 0.02; }
float getHexPixelSize()    { return customParams[1].x > 1.0 ? customParams[1].x : 35.0; }
float getHexLineThickness(){ return customParams[1].y > 0.001 ? customParams[1].y : 0.05; }
float getHexOpacity()      { return customParams[1].z > 0.001 ? customParams[1].z : 0.18; }
float getHexPulseSpeed()   { return customParams[1].w > 0.001 ? customParams[1].w : 1.5; }
float getHexScanSpeed()    { return customParams[2].x > 0.001 ? customParams[2].x : 1.0; }
float getStreamColumns()   { return customParams[2].y > 1.0 ? customParams[2].y : 35.0; }
float getStreamOpacity()   { return customParams[2].z > 0.001 ? customParams[2].z : 0.15; }
float getStreamSpeed()     { return customParams[2].w > 0.01 ? customParams[2].w : 0.8; }
float getTrailLength()     { return customParams[3].x > 0.01 ? customParams[3].x : 0.20; }

// === ARETHA DARK PALETTE (hardcoded base colors) ===
// Declare constants before they're used in functions
const vec3 arethaBg       = vec3(0.098, 0.153, 0.259);  // #192742 - deep dark blue
const vec3 arethaFg       = vec3(0.827, 0.855, 0.890);  // #d3dae3 - light gray
const vec3 arethaLavender = vec3(0.482, 0.388, 0.776);  // #7b63c6 - lavender
const vec3 arethaBlue     = vec3(0.200, 0.510, 0.816);  // #3382d0 - steel blue

// Background color from customColors[4] (slot 4)
// This can be set to match Ghostty's terminal background color
vec3 getBackgroundColor() {
    vec3 bg = customColors[4].rgb;
    // If set (non-zero), use it; otherwise use default arethaBg
    if (length(bg) > 0.01) {
        return bg;
    }
    return arethaBg;
}

// Colors with defaults
vec3 getArethaCyan() {
    vec3 c = customColors[0].rgb;
    return length(c) > 0.01 ? c : vec3(0.333, 0.667, 1.000);  // #55aaff
}
vec3 getArethaPink() {
    vec3 c = customColors[1].rgb;
    return length(c) > 0.01 ? c : vec3(1.000, 0.333, 0.498);  // #ff557f
}
vec3 getArethaTeal() {
    vec3 c = customColors[2].rgb;
    return length(c) > 0.01 ? c : vec3(0.000, 0.620, 0.741);  // #009ebd
}
vec3 getArethaPurple() {
    vec3 c = customColors[3].rgb;
    return length(c) > 0.01 ? c : vec3(0.333, 0.333, 1.000);  // #5555ff
}

// Color grade mapping (shadows -> midtones -> highlights)
const vec3 gradeShadow    = vec3(0.098, 0.153, 0.259);  // Deep blue (arethaBg)
const vec3 gradeMid       = vec3(0.000, 0.620, 0.741);  // Teal midtones
const vec3 gradeHighlight = vec3(0.333, 0.667, 1.000);  // Cyan highlights

// === UTILITIES ===
float hash(float n) { return fract(sin(n) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Signed distance to rounded rectangle
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// === LAYER 1: NEON COLOR GRADE ===
vec3 colorGrade(vec3 color, float t) {
    float GRADE_INTENSITY = getGradeIntensity();
    float GRADE_SATURATION = getGradeSaturation();
    
    float lum = luminance(color);

    // Three-way color grade based on luminance
    vec3 graded;
    if (lum < 0.33) {
        float blend = lum / 0.33;
        graded = mix(gradeShadow, gradeMid, blend);
    } else if (lum < 0.66) {
        float blend = (lum - 0.33) / 0.33;
        graded = mix(gradeMid, gradeHighlight, blend);
    } else {
        float blend = (lum - 0.66) / 0.34;
        graded = mix(gradeHighlight, vec3(1.0), blend);
    }

    // Blend graded color with original, preserving detail
    vec3 result = mix(color, graded * (color + 0.1), GRADE_INTENSITY);

    // Boost saturation slightly
    float gray = luminance(result);
    result = mix(vec3(gray), result, GRADE_SATURATION);

    return result;
}

// Shimmer effect on highlights
float shimmer(vec2 uv, float t, float lum) {
    float SHIMMER_INTENSITY = getShimmerIntensity();
    
    if (lum < 0.5) return 0.0;
    float s = sin(t * 3.0 + uv.x * 12.0 + uv.y * 8.0) * 0.5 + 0.5;
    return s * SHIMMER_INTENSITY * (lum - 0.5) * 2.0;
}

// === LAYER 2: HEX GRID ===
float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);
}

vec2 hexCoord(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

vec3 hexGrid(vec2 uv, float t, vec2 screenUV, vec2 zoneSize) {
    float HEX_PIXEL_SIZE = getHexPixelSize();
    float HEX_LINE_THICKNESS = getHexLineThickness();
    float HEX_OPACITY = getHexOpacity();
    float HEX_PULSE_SPEED = getHexPulseSpeed();
    float HEX_SCAN_SPEED = getHexScanSpeed();
    
    vec3 arethaCyan = getArethaCyan();
    vec3 arethaPurple = getArethaPurple();
    vec3 arethaTeal = getArethaTeal();
    
    // Use pixel coordinates so hex size stays constant regardless of zone size
    vec2 pixelCoord = uv * zoneSize;
    vec2 scaledUV = pixelCoord / HEX_PIXEL_SIZE;
    vec2 hex = hexCoord(scaledUV);
    float d = hexDist(hex);

    // Calculate hex center for effects
    vec2 hexCenter = scaledUV - hex;
    vec2 hexId = floor(hexCenter / vec2(1.0, 1.732));

    // Draw hex edges
    float edge = smoothstep(0.5 - HEX_LINE_THICKNESS, 0.5, d);
    edge *= smoothstep(0.5 + HEX_LINE_THICKNESS, 0.5, d);
    edge = 1.0 - edge;

    // Pulse effect based on hex position
    float pulse = sin(t * HEX_PULSE_SPEED + hexId.x * 0.5 + hexId.y * 0.3) * 0.5 + 0.5;

    // Animated scan line moving across hex grid (moving DOWN in screen coords)
    // In screen coords, Y increases downward, so we SUBTRACT t to make it move down (matches Ghostty)
    float scan = sin(hexCenter.y * 0.2 - t * HEX_SCAN_SPEED) * 0.5 + 0.5;
    scan = pow(scan, 8.0);

    // Combine base grid intensity
    float gridIntensity = edge * (0.3 + pulse * 0.5);
    gridIntensity += scan * 0.25;

    // Random hex highlights (blinking hexes)
    float hexRand = hash2(hexId + floor(t * 0.3));
    if (hexRand > 0.94) {
        float blink = sin(t * 5.0 + hexRand * 100.0) * 0.5 + 0.5;
        gridIntensity += blink * 0.6;
    }

    // Color shifts across the grid
    vec3 gridColor = mix(arethaCyan, arethaPurple, sin(t + screenUV.y * 2.0) * 0.5 + 0.5);
    gridColor = mix(gridColor, arethaTeal, pulse * 0.3);

    return gridColor * gridIntensity * HEX_OPACITY;
}

// === LAYER 3: DATA STREAMS ===
// uv is in screen coordinates: Y=0 at top, Y=1 at bottom
vec3 dataStream(vec2 uv, float t) {
    float STREAM_COLUMNS = getStreamColumns();
    float STREAM_OPACITY = getStreamOpacity();
    float STREAM_SPEED = getStreamSpeed();
    float TRAIL_LENGTH = getTrailLength();
    
    vec3 arethaCyan = getArethaCyan();
    vec3 arethaTeal = getArethaTeal();
    
    float streamEffect = 0.0;

    // Create multiple stream columns
    float columnWidth = 1.0 / STREAM_COLUMNS;
    float column = floor(uv.x / columnWidth);
    float columnX = fract(uv.x / columnWidth);

    // Each column has its own speed and phase
    float columnSeed = hash(column);
    float columnSpeed = 0.5 + columnSeed * 0.8;
    float columnPhase = columnSeed * 100.0;

    // Calculate falling position (inverted so rain falls top -> bottom, matching Ghostty)
    // In screen coords (Y=0 at top, Y=1 at bottom), falling DOWN means fallPos increases 0->1
    float fallPos = 1.0 - fract(t * STREAM_SPEED * columnSpeed + columnPhase);

    // Create the stream trail (trail ABOVE head = lower Y values in screen coords)
    // distFromHead > 0 when pixel is ABOVE head (lower Y), < 0 when BELOW head (higher Y)
    // Match Ghostty: uv.y - fallPos (positive when uv.y > fallPos, i.e., pixel is below head)
    float distFromHead = uv.y - fallPos;
    
    // Wrap around when head crosses bottom and reappears at top
    if (distFromHead < 0.0) distFromHead += 1.0;
    
    // Trail intensity: brightest at head (distFromHead=0), fading as we go up (distFromHead increases)
    float trail = 1.0 - distFromHead / TRAIL_LENGTH;
    trail = max(0.0, trail);
    trail = trail * trail; // Quadratic falloff

    // Only show in center of column (creates vertical lines)
    float columnMask = 1.0 - abs(columnX - 0.5) * 4.0;
    columnMask = max(0.0, columnMask);

    // Random "character" flicker along trail
    float charFlicker = hash(floor(uv.y * 50.0) + floor(t * 8.0) + column);
    if (trail > 0.1) {
        trail *= 0.7 + charFlicker * 0.3;
    }

    // Bright head (where distFromHead is close to 0, just at the leading edge)
    float head = smoothstep(0.025, 0.0, distFromHead);

    streamEffect = (trail * 0.5 + head * 1.2) * columnMask;

    // Some columns are brighter
    if (columnSeed > 0.7) {
        streamEffect *= 1.4;
    }

    // Random column activation (not all columns active)
    float columnActive = step(0.45, hash(column + floor(t * 0.4)));
    streamEffect *= columnActive;

    // Stream color with slight variation
    vec3 streamColor = mix(arethaCyan, arethaTeal, columnSeed);

    // Brighter heads
    if (head > 0.5) {
        streamColor = mix(streamColor, vec3(1.0), 0.4);
    }

    return streamColor * streamEffect * STREAM_OPACITY;
}

// === ADDITIONAL EFFECTS ===

// Horizontal scan line accent (from DataStream) - defined but not used in Ghostty mainImage
// Keeping for potential future use, but not calling it to match Ghostty exactly
float scanLine(vec2 uv, float t) {
    float scanY = 1.0 - fract(t * 0.25);  // top -> bottom
    return smoothstep(0.015, 0.0, abs(uv.y - scanY)) * 0.15;
}

// Ambient glow (matching Ghostty direction, increased intensity for visibility)
vec3 ambientGlow(vec2 uv, float t) {
    vec3 arethaTeal = getArethaTeal();
    float glow = sin(uv.x * 3.0 + t) * sin(uv.y * 2.0 - t * 0.7) * 0.5 + 0.5;
    return arethaTeal * glow * 0.05; // Increased from 0.03 to 0.05 for better visibility
}

// Enhanced cyberpunk glitch effect
vec3 glitch(vec2 uv, float t) {
    vec3 arethaCyan = getArethaCyan();
    vec3 arethaPink = getArethaPink();
    vec3 arethaPurple = getArethaPurple();
    
    vec3 glitchColor = vec3(0.0);
    float glitchT = floor(t * 3.0);
    float glitchSeed = hash(glitchT);

    // Only glitch occasionally (roughly 1 in 50 frames)
    if (glitchSeed > 0.98) {
        float intensity = (glitchSeed - 0.98) * 50.0; // 0-1 based on how "over" threshold

        // Block-based glitch regions
        float blockSize = 8.0 + hash(glitchT + 1.0) * 24.0; // Variable block height
        float block = floor(uv.y * blockSize);
        float blockRand = hash2(vec2(block, glitchT));

        if (blockRand > 0.7) {
            // RGB channel separation (chromatic aberration) - matching Ghostty
            float separation = (blockRand - 0.7) * 0.02 * intensity;
            vec2 rOffset = vec2(separation, 0.0);
            vec2 bOffset = vec2(-separation, 0.0);

            // Horizontal displacement for this block
            float hShift = (hash2(vec2(block + 100.0, glitchT)) - 0.5) * 0.03 * intensity;

            // Color corruption - blend between theme accent colors
            float colorShift = hash2(vec2(block + 200.0, glitchT));
            vec3 corruptColor;
            if (colorShift < 0.33) {
                corruptColor = arethaCyan;
            } else if (colorShift < 0.66) {
                corruptColor = arethaPink;
            } else {
                corruptColor = arethaPurple;
            }

            // Scanline interference within glitch blocks (matching Ghostty exactly)
            float scanIntensity = sin(uv.y * 200.0 + t * 50.0) * 0.5 + 0.5;
            scanIntensity = pow(scanIntensity, 4.0);

            // Combine effects (increased scanline visibility to match Ghostty)
            glitchColor += corruptColor * 0.08 * intensity;
            glitchColor += corruptColor * scanIntensity * 0.06 * intensity;

            // Occasional bright flash lines
            if (blockRand > 0.92) {
                float lineY = fract(uv.y * blockSize);
                if (lineY < 0.06 || lineY > 0.94) {
                    glitchColor += vec3(1.0) * 0.15 * intensity;
                }
            }
        }

        // Rare full-width interference band
        if (glitchSeed > 0.995) {
            float bandY = hash(glitchT + 50.0);
            float bandDist = abs(uv.y - bandY);
            if (bandDist < 0.02) {
                float bandIntensity = 1.0 - bandDist / 0.02;
                glitchColor += mix(arethaCyan, arethaPink, hash(glitchT + 60.0)) * bandIntensity * 0.12;
            }
        }
    }

    return glitchColor;
}

// Vignette
float vignette(vec2 uv) {
    return 1.0 - length(uv - 0.5) * 0.2;
}

// === ZONE RENDERING ===
vec4 renderArethaZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float SPEED = getSpeed();
    
    float borderRadius = max(params.x, 4.0);
    float borderWidth = max(params.y, 1.0);
    
    // Convert normalized coords to pixels (screen coords: Y=0 at top)
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    
    // Local UV within the zone (0-1), screen coords (Y=0 at top of zone)
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    localUV = clamp(localUV, 0.0, 1.0);
    
    // Signed distance to zone boundary
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    vec4 result = vec4(0.0);
    
    if (d < 0.0) {
        // Inside the zone - render effects
        float t = iTime * SPEED;
        vec3 fx = vec3(0.0);
        
        // Base color: use configured background color (matching Ghostty's approach)
        // This allows users to set the same background color as their terminal
        vec3 baseColor = getBackgroundColor();
        
        // Use zone fill color alpha for transparency (or default to 0.95 for Ghostty-like appearance)
        float bgAlpha = fillColor.a > 0.01 ? fillColor.a : 0.95;
        
        // Layer 1: Color Grade the background (matching Ghostty intensity)
        vec3 gradedBg = colorGrade(baseColor, t * 10.0);
        fx += (gradedBg - baseColor) * 0.5; // Match Ghostty's 0.5 multiplier
        
        // Layer 2: Hex Grid overlay
        fx += hexGrid(localUV, t * 10.0, localUV, rectSize);
        
        // Layer 3: Data Streams overlay
        fx += dataStream(localUV, t * 8.0);
        
        // Additional effects (matching Ghostty exactly - no scanLine call)
        fx += ambientGlow(localUV, t * 10.0);
        fx += glitch(localUV, iTime);
        
        // Shimmer on highlighted zones
        if (isHighlighted) {
            float lum = luminance(baseColor + fx);
            fx += getArethaCyan() * shimmer(localUV, t * 10.0, lum) * 2.0;
        }
        
        // Apply vignette and combine (matching Ghostty: bg + fx * vig)
        float vig = vignette(localUV);
        result.rgb = baseColor + fx * vig;
        result.a = bgAlpha;
        
        // Brighten highlighted zones
        if (isHighlighted) {
            result.rgb *= 1.15;
            result.a = min(result.a + 0.1, 1.0);
        }
    }
    
    // Border rendering
    float borderDist = abs(d);
    if (borderDist < borderWidth + 1.0) {
        float border = 1.0 - smoothstep(0.0, borderWidth, borderDist);
        
        vec3 edgeColor = borderColor.rgb;
        if (length(edgeColor) < 0.01) {
            edgeColor = getArethaCyan();
        }
        
        // Animate border
        float t = iTime * getSpeed() * 5.0;
        float pulse = sin(t * 2.0) * 0.15 + 0.85;
        edgeColor *= pulse;
        
        result.rgb = mix(result.rgb, edgeColor, border * 0.8);
        result.a = max(result.a, border * borderColor.a);
    }
    
    // Outer glow for highlighted zones
    if (isHighlighted && d > 0.0 && d < 20.0) {
        float glow = exp(-d / 8.0) * 0.4;
        result.rgb += getArethaCyan() * glow;
        result.a = max(result.a, glow * 0.6);
    }
    
    return result;
}

// === MAIN ===
void main() {
    // Use screen coordinates directly (Y=0 at top, matching Qt/PlasmaZones convention)
    vec2 fragCoord = vTexCoord * iResolution;
    vec4 color = vec4(0.0);
    
    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }
    
    // Render each zone
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderArethaZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted);
        
        // Alpha compositing
        color.rgb = mix(color.rgb, zoneColor.rgb, zoneColor.a);
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
