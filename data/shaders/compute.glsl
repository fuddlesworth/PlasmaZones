// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compute shader helpers for PlasmaZones particle systems.
// Include in .comp shaders. Declares SSBO and particle image.

#ifndef PLASMAZONES_COMPUTE_GLSL
#define PLASMAZONES_COMPUTE_GLSL

struct Particle {
    vec2 pos;
    vec2 vel;
    float life;
    float age;
    float seed;
    float size;
    vec4 color;
};

layout(std430, binding = 14) buffer ParticleBuffer {
    Particle particles[];
};

layout(binding = 13, rgba8) uniform image2D uParticleImage;

// Simple hash for particle randomness
float particleHash(float seed, float salt) {
    return fract(sin(seed * 127.1 + salt * 311.7) * 43758.5453);
}

#endif
