// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// TV Glitch transition — ported from Burn-My-Windows' tv-glitch.frag
// (https://github.com/Schneegans/Burn-My-Windows). Analog glitch
// artefacts — RGB shift, scanlines, and noise tear across the surface
// as it collapses through a CRT-style mask. Combines BMW's `tv` and
// `glitch` effects (original idea: Kurt Wilson).
//
// BMW uniform shims come from `<bmw_compat.glsl>`; helpers
// `hash22` / `simplex2D` come from `<noise.glsl>` (byte-equivalent
// to BMW's). See bmw_compat.glsl header for the BMW-to-PlasmaZones
// uniform-name remap.

#include <noise.glsl>

#include <bmw_compat.glsl>

// uSeed: BMW samples Math.random() per leg; PlasmaZones derives a
// stable per-surface scalar via `surfaceSeed()` (noise.glsl).
#define uSeed (surfaceSeed())

// uDuration: BMW gschema default `tv-glitch-animation-time` is 750 ms.
// Per the bmw_compat.glsl substitution rule, `uProgress * uDuration`
// would normally be replaced with `(float(iFrame) / 60.0)` (real
// elapsed seconds). Pinning `uDuration` to BMW's gschema default
// preserves upstream noise-wave pacing exactly when the PlasmaZones
// leg duration matches 750 ms; at other durations the visible
// pacing drifts linearly from BMW. Documented divergence from the
// bmw_compat substitution rule — kept because the pacing-from-the-
// BMW-default reads more like the upstream effect than a floating
// real-elapsed-seconds clock would, and PlasmaZones doesn't expose
// a per-leg duration uniform to derive the BMW-equivalent value.
#define uDuration 0.75

const float BLUR_WIDTH = 0.01;  // Width of the gradients.
const float TB_TIME    = 0.7;   // Relative time for the top/bottom animation.
const float LR_TIME    = 0.4;   // Relative time for the left/right animation.
const float LR_DELAY   = 0.6;   // Delay after which the left/right animation starts.
const float FF_TIME    = 0.1;   // Relative time for the final fade to transparency.
const float SCALING    = 0.5;   // Additional vertical scaling of the window.

// This is a combination of the effects from tv.frag and glitch.frag. Credits go to Kurt
// Wilson (https://github.com/Kurtoid) for this idea!
vec4 pTransition(vec2 uv, float t) {

  // Add the tv effect sooner/later in the animation, compared to the original TV effect.
  float tOffset    = uForOpening ? 0.0 : 1.0;
  float tvProgress = clamp(uProgress * 2.0 - tOffset, 0.0, 1.0);
  tvProgress = uForOpening ? 1.0 - easeOutQuad(tvProgress) : easeOutQuad(tvProgress);

  // Scale down the window vertically.
  float scale = 1.0 / mix(1.0, SCALING, tvProgress) - 1.0;
  vec2 coords = iTexCoord.st;
  coords.y    = coords.y * (scale + 1.0) - scale * 0.5;

  // This is from the original Glitch effect.
  float progress = easeInQuad(uForOpening ? 1.0 - uProgress : uProgress);
  float time     = progress * uDuration * p_uSpeed;
  float strength = p_uStrength * progress;
  float displace = 1000.0 * strength / uSize.x;
  float yPos     = p_uScale * uSize.y * (coords.y + uSeed * 10.0);

  // Create large noise waves and add some smaller noise waves.
  float noise = clamp(simplex2D(vec2(time, yPos * 0.002)) - 0.5, 0.0, 1.0);
  noise += (simplex2D(vec2(time * 10.0, yPos * 0.05)) - 0.5) * 0.15;

  // Apply the noise as x displacement for every line.
  float xPos  = clamp(coords.x - displace * noise * noise, 0.0, 1.0);
  vec4 oColor = getClippedInputColor(vec2(xPos, coords.y));

  // Mix in some random interference lines.
  vec3 interference          = p_uColor.rgb * hash12(vec2(yPos * time));
  float interferenceStrength = noise * min(strength, 1.0);
  oColor.rgb = mix(oColor.rgb, interference, p_uColor.a * interferenceStrength);

  // Mix in some grainy noise.
  vec3 grain          = p_uColor.rgb * simplex2D(uSize * coords + vec2(time * 100.0));
  float grainStrength = 0.2 * min(strength, 1.0);
  oColor.rgb          = mix(oColor.rgb, grain, p_uColor.a * grainStrength);

  // Add a subtle line pattern every 4 pixels.
  if (floor(mod(yPos * 0.25, 2.0)) == 0.0) {
    oColor.rgb = mix(oColor.rgb, p_uColor.rgb, p_uColor.a * (0.15 * noise));
  }

  // Shift green/blue channels.
  float offset = 0.1 * noise * displace;
  oColor.g     = mix(oColor.g, getClippedInputColor(vec2(xPos + offset, coords.y)).g, 0.25);
  oColor.b     = mix(oColor.b, getClippedInputColor(vec2(xPos - offset, coords.y)).b, 0.25);

  // Now hide the window according to the TV effect.

  // All of these are in [0..1] during the different stages of the animation.
  // tb refers to the top-bottom animation.
  // lr refers to the left-right animation.
  // ff refers to the final fade animation.
  float tbProg = smoothstep(0.0, 1.0, clamp(tvProgress / TB_TIME, 0.0, 1.0));
  float lrProg = smoothstep(0.0, 1.0, clamp((tvProgress - LR_DELAY) / LR_TIME, 0.0, 1.0));
  float ffProg =
    smoothstep(0.0, 1.0, clamp((tvProgress - 1.0 + FF_TIME) / FF_TIME, 0.0, 1.0));

  // This is a top-center-bottom gradient in [0..1..0]
  float tb = coords.y * 2.0;
  tb       = tb < 1.0 ? tb : 2.0 - tb;

  // This is a left-center-right gradient in [0..1..0]
  float lr = coords.x * 2.0;
  lr       = lr < 1.0 ? lr : 2.0 - lr;

  // Combine the progress values with the gradients to create the alpha masks.
  float tbMask = 1.0 - smoothstep(0.0, 1.0, clamp((tbProg - tb) / BLUR_WIDTH, 0.0, 1.0));
  float lrMask = 1.0 - smoothstep(0.0, 1.0, clamp((lrProg - lr) / BLUR_WIDTH, 0.0, 1.0));
  float ffMask = 1.0 - smoothstep(0.0, 1.0, ffProg);

  // Assemble the final color value.
  oColor.rgb =
    mix(oColor.rgb, p_uColor.rgb * oColor.a, p_uColor.a * smoothstep(0.0, 1.0, tvProgress));
  float mask = tbMask * lrMask * ffMask;

  oColor.a *= mask;

  return premultiply(oColor);
}
