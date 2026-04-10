// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlasmaZones shared animation shader helpers (GLSL #version 450).
// Include from animation effect.frag with:
//   #include <animation_common.glsl>
//
// UBO layout matches AnimationShaderUniforms in animationshadercommon.h.
// Content texture at binding 1 (overlay content captured via layer.enabled).

#ifndef PLASMAZONES_ANIMATION_COMMON_GLSL
#define PLASMAZONES_ANIMATION_COMMON_GLSL

layout(std140, binding = 0) uniform AnimationUniforms {
    mat4 qt_Matrix;           // Scene graph transform
    float qt_Opacity;         // Qt opacity factor

    // Animation timing
    float pz_progress;        // [0.0-1.0] animation progress
    float pz_duration;        // Total duration in milliseconds
    float pz_style_param;     // Style-specific parameter

    // Content resolution
    vec2 iResolution;         // Content size in pixels

    // Animation metadata
    int pz_mode;              // Reserved (unused by file-based shaders)
    int pz_direction;         // 0 = show (in), 1 = hide (out)

    // Custom shader parameters (32 float slots in 8 vec4s)
    // Mapped from metadata.json parameter slots.
    // Sentinel: -1.0 = unset (shader should use its own default).
    vec4 customParams[8];
};

// Content texture — the overlay's rendered content captured via QQuickItem::layer
layout(binding = 1) uniform sampler2D contentTexture;

// ── Helper: premultiplied alpha output ──
// Qt Quick compositing expects premultiplied alpha. Always use this
// for the final fragColor output to avoid dark-fringe artifacts.
vec4 pzPremultiply(vec4 color) {
    return vec4(color.rgb * color.a, color.a);
}

#endif // PLASMAZONES_ANIMATION_COMMON_GLSL
