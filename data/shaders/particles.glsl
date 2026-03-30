// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fragment shader helpers for reading particle texture.
// Include in effect.frag to composite particles over zones.

#ifndef PLASMAZONES_PARTICLES_GLSL
#define PLASMAZONES_PARTICLES_GLSL

layout(binding = 13) uniform sampler2D uParticleTexture;

// Sample and blend particles over base color
vec4 compositeParticles(vec4 base, vec2 uv) {
    vec4 p = texture(uParticleTexture, uv);
    return vec4(mix(base.rgb, p.rgb, p.a), max(base.a, p.a));
}

#endif
