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

#include <noise.glsl>

#include <bmw_compat.glsl>

vec4 pTransition(vec2 uv, float t) {
  // We reverse the progress for window opening.
  float progress = easeOutQuad(uProgress);
  progress       = uForOpening ? 1.0 - progress : progress;

  // Put texture coordinate origin to center of window.
  vec2 coords = iTexCoord.st * 2.0 - 1.0;

  // Scale image texture with progress.
  coords /= mix(1.0, p_uScale, progress);

  // Squish image texture vertically.
  coords.y /= mix(1.0, (1.0 - 0.2 * p_uSquish), progress);

  // 'Tilt' image texture around x-axis.
  coords.x /= mix(1.0, 1.0 - 0.1 * p_uTilt * coords.y, progress);

  // Move image texture vertically.
  coords.y += p_uShift * progress;

  // Move texture coordinate center to corner again.
  coords = coords * 0.5 + 0.5;

  // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
  vec4 oColor = getInputColor(coords) * boundaryMask(coords);

  // Dissolve window.
  oColor.a = oColor.a * (uForOpening ? uProgress : pow(1.0 - uProgress, 2.0));

  return premultiply(oColor);
}
