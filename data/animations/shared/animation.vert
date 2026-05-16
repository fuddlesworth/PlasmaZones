// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
