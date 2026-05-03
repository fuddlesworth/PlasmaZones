// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Morph transition — sine-based UV displacement field warps the
// rendered surface (sampled through iChannel0). The previous stub
// emitted `vec4(warpedUv.x, warpedUv.y, warpMask, alpha)` — a
// rainbow-like gradient of the UV coords — instead of sampling
// anything, which is the "weird gradient that shows then QML
// renders" report. Now: sample iChannel0 at the warped UV so the
// surface ITSELF deforms during the transition, then settles back
// to its un-warped self as `iTime` reaches the leg endpoints
// (sin envelope peaks at iTime==0.5).

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength  customParams[0].x
#define warpFrequency customParams[0].y

// Bounds-padding fraction supplied by SurfaceAnimator from
// AnimationShaderEffect::boundsPadding (metadata.json). Single source of
// truth — this slot is filled by surfaceanimator.cpp's attachShaderToAnchor
// at leg start. No hardcoded constant means the GLSL value can never drift
// from metadata.json. customParams[7] is reserved for SurfaceAnimator's
// structural data (see ShaderEffect.h customParams8 Q_PROPERTY); no
// user-declared parameter in any shipping shader reaches this slot
// (translateAnimationParams fills from customParams[0] up).
#define boundsPadding customParams[7].x

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Remap padded vTexCoord → anchor-space UV. Anchor occupies the
    // central region [pad/(1+2pad), (1+pad)/(1+2pad)] of the padded
    // shaderItem; anchorUv = uv*(1+2pad) - pad ∈ [-pad, 1+pad].
    float k = 1.0 + 2.0 * boundsPadding;
    vec2 anchorUv = vTexCoord * k - boundsPadding;

    // Envelope peaks at iTime == 0.5 (mid-transition) and returns to
    // 0 at the endpoints — same shape as glitch, gives both show and
    // hide a "warp peak then settle" feel.
    float visibility = clamp(iTime, 0.0, 1.0);
    float envelope = sin(visibility * 3.14159);
    float strength = warpStrength * envelope;
    float freq = max(warpFrequency, 1.0);

    vec2 warp = vec2(
        sin(anchorUv.y * freq * 6.28318 + iTime * 6.28318) * strength,
        cos(anchorUv.x * freq * 6.28318 + iTime * 6.28318) * strength
    );

    // Geometry-warp: at output anchorUv, sample BACK from where the
    // warp would have displaced it (first-order inverse). Points whose
    // pre-image falls outside [0,1] represent areas of the warped
    // silhouette outside the anchor's content — emit transparent.
    // Padding on shaderItem + matching padding on the surface give
    // the rippled silhouette room to extend OUTSIDE the original
    // anchor rectangle without clipping.
    vec2 sampleUv = anchorUv - warp;
    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
        fragColor = vec4(0.0);
        return;
    }
    fragColor = texture(iChannel0, sampleUv);
}
