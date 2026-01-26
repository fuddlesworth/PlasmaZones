// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 330 core

// Vertex inputs
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

// Vertex outputs
out vec2 vTexCoord;

// Uniform block matching fragment shader (std140 layout, binding 0)
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
    vec4 customColors[4];  // [0-3], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

/**
 * NEON GLOW VERTEX SHADER
 * Simple pass-through vertex shader
 */

void main() {
    // Pass through texture coordinates
    vTexCoord = texCoord;
    
    // Transform position by the Qt matrix
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
