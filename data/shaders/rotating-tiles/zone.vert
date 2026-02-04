// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Vertex inputs
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

// Vertex outputs
layout(location = 0) out vec2 vTexCoord;

// Uniform block matching fragment shader (std140 layout)
layout(std140, binding = 0) uniform ZoneUniforms {
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
    vec4 customColors[8];
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
