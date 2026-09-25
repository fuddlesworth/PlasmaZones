// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paint Brush transition — ported from Burn-My-Windows' paint-brush.frag
// (https://github.com/Schneegans/Burn-My-Windows). Reveals/conceals the
// surface through a thick paint-stroke wipe driven by a 4-channel brush
// atlas (one channel per stroke-density level), with a soft drop-shadow
// trailing the brush front.
//
// BMW uniform shims (`uForOpening`, `uProgress`, `uSize`,
// `getInputColor`, `setOutputColor`, easing curves, `alphaOver`,
// etc.) come from `<bmw_compat.glsl>`; see that header for the
// BMW-to-PlasmaZones uniform-name remap and the straight-alpha vs
// premultiplied I/O bridging.
//
// Parameter exposure: BMW's two custom uniforms are mapped here as
// follows:
//   • `uBrushTexture` (sampler2D) → `uTexture1`. The runtime auto-binds
//     the first declared texture in metadata.json (`brush.png`) to slot
//     1, matching matrix's `matrix-font.png` convention.
//   • `uBrushSize` (int 1..4 in BMW, indexes the brush atlas's RGBA
//     channels) → hard-coded to BMW's default `3.0` per the porting
//     spec. The `[int(...)]` runtime-indexed swizzle on a `vec4` sample
//     is core GLSL 450 and stays verbatim from BMW.
//
// Body substitutions: `texture2D` → `texture` (GLSL 4.50 core); BMW's
// `getMask` and the shadow loop are byte-for-byte from upstream apart
// from that one-token rename.

#include <noise.glsl>

#include <bmw_compat.glsl>

// BMW's `uBrushTexture` → first user-bound texture slot.
#define uBrushTexture uTexture1
// BMW default — see resources/schemas/.../burn-my-windows-profile.gschema.xml
// (`paint-brush-size`). Held constant per the porting spec
// (metadata.json declares no parameters).
#define uBrushSize 3.0

const float SHADOW_WIDTH = 0.03;
const float SHADOW_STEPS = 10.0;

// Returns 1.0 if the window texture at the given texCoord should be painted.
float getMask(vec2 texCoord, float progress) {
  float brush = texture(uBrushTexture, texCoord)[int(uBrushSize - 1.0)];
  if (uForOpening) {
    return step(brush, progress);
  }

  return step(progress, brush);
}

vec4 pTransition(vec2 uv, float t) {

  vec4 oColor = getInputColor(iTexCoord.st);

  // This will be 0.0 in the masked-out areas.
  float mask = getMask(iTexCoord.st, uProgress);

  // Add a simple drop shadow below the remaining parts.
  if (mask == 0.0) {

    for (float i = 0.0; i < SHADOW_STEPS; ++i) {
      vec2 shadowTexCoord = iTexCoord.st - vec2(0.0, i * SHADOW_WIDTH / SHADOW_STEPS);
      float shadowMask = getMask(shadowTexCoord, uProgress);
      // boundaryMask (see noise.glsl) crops the alpha read to zero
      // outside [0, 1] — shadowTexCoord.y goes negative for fragments
      // near the top of the window (the shadow loop walks UP from
      // each fragment), and uTexture0's clamp-to-edge sampler would
      // otherwise read the top row's alpha (typically opaque content)
      // as the shadow source, painting a phantom shadow above the
      // window's top edge.
      float shadowAlpha = (1.0 - i / SHADOW_STEPS) * shadowMask
                        * getInputColor(shadowTexCoord).a
                        * boundaryMask(shadowTexCoord);

      if (shadowAlpha > 0.0) {
        oColor = vec4(vec3(0.0), shadowAlpha * 0.2);
        break;
      }

      // If we arrived at the end of the shadow, make sure to set the alpha to 0.
      if (i == SHADOW_STEPS - 1.0) {
        oColor.a = 0.0;
      }
    }
  }

  return premultiply(oColor);
}
