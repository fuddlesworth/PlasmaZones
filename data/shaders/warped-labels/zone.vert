// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#include <common.glsl>

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
