// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glide transition — ported from Burn-My-Windows' glide.frag
// (https://github.com/Schneegans/Burn-My-Windows). Smooth slide-and-
// fade entrance/exit: the surface scales, squishes, tilts, and shifts
// over the leg with an `easeOutQuad` envelope on the geometry and a
// linear/quadratic envelope on the alpha (open vs close respectively).
//
// BMW uniform shims (`uForOpening`, `uProgress`, `uSize`,
// `getInputColor`, `setOutputColor`, easing curves, `alphaOver`,
// etc.) come from `<bmw_compat.glsl>`; see that header for the
// BMW-to-PlasmaZones uniform-name remap and the straight-alpha vs
// premultiplied I/O bridging.
//
// Parameter exposure: BMW's four custom floats (`uScale`, `uSquish`,
// `uTilt`, `uShift`) are remapped onto `customParams[0]` sub-slots in
// declaration order; defaults and ranges mirror the upstream
// `glide-*` keys in `schemas/.../burn-my-windows-profile.gschema.xml`
// and the corresponding adjustments in `resources/ui/adw/glide.ui`.
// No `uProgress * uDuration` substitution is needed — the body has no
// elapsed-seconds idiom.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <bmw_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define uScale  customParams[0].x   // window scale at endpoint, BMW default 0.95
#define uSquish customParams[0].y   // vertical squish amount, BMW default 0.15
#define uTilt   customParams[0].z   // x-axis tilt amount, BMW default -0.3
#define uShift  customParams[0].w   // vertical shift, BMW default -0.05

void main() {
  // We reverse the progress for window opening.
  float progress = easeOutQuad(uProgress);
  progress       = uForOpening ? 1.0 - progress : progress;

  // Put texture coordinate origin to center of window.
  vec2 coords = iTexCoord.st * 2.0 - 1.0;

  // Scale image texture with progress.
  coords /= mix(1.0, uScale, progress);

  // Squish image texture vertically.
  coords.y /= mix(1.0, (1.0 - 0.2 * uSquish), progress);

  // 'Tilt' image texture around x-axis.
  coords.x /= mix(1.0, 1.0 - 0.1 * uTilt * coords.y, progress);

  // Move image texture vertically.
  coords.y += uShift * progress;

  // Move texture coordinate center to corner again.
  coords = coords * 0.5 + 0.5;

  // Boundary mask: with default uScale=0.95 (and uSquish/uTilt non-zero),
  // the inverse-warped sample UV at corner fragments lands ~5% past the
  // [0,1] bounds. A clamp-to-edge sample would smear the edge column/row
  // outward — visible as a dim border + perceived 5% shrink of the
  // surface. Crop cleanly to transparent across the [0,1] edge with a
  // narrow smoothstep band (same pattern as morph/effect.frag), so the
  // warped silhouette stays the size BMW intended without altering the
  // scale/squish/tilt motion math.
  vec2 insideLo = smoothstep(vec2(-0.005), vec2(0.0), coords);
  vec2 insideHi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.005), coords);
  float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;

  vec4 oColor = getInputColor(coords) * mask;

  // Dissolve window.
  oColor.a = oColor.a * (uForOpening ? uProgress : pow(1.0 - uProgress, 2.0));

  setOutputColor(oColor);
}
