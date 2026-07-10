// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Incinerate transition — ported from Burn-My-Windows' incinerate.frag
// (https://github.com/Schneegans/Burn-My-Windows). Heat-driven dissolve
// with embers and chromatic distortion: a fiery edge sweeps across the
// surface starting from a configurable origin, leaving smoke and
// glowing ember particles in its trail.
//
// BMW uniform shims come from `<bmw_compat.glsl>`; helpers
// `hash22`/`simplex2D`/`simplex2DFractal` come from `<noise.glsl>`
// (byte-equivalent to BMW's). See bmw_compat.glsl header for the
// BMW-to-PlasmaZones uniform-name remap.

#include <noise.glsl>

#include <bmw_compat.glsl>

// metadata.json declaration order → customParams sub-slots.
// BMW's `uSeed` and `uStartPos` are vec2 — PlasmaZones param slots are
// scalar, so we expose the components as separate `*X` / `*Y` floats
// and reassemble at the call sites below.
// `p_*` param macros are generated from metadata.json by the harness.

// This maps a given value in [0..1] to a color from the rgba color ramp
// [transparent black ... semi-transparent uColor ... opaque white].
vec4 getFireColor(float val) {
  return vec4(tritone(val, vec3(0.0), p_uColor.rgb, vec3(1.0)), val);
}

vec4 pTransition(vec2 uv, float t) {

  // The burning fire edge is composed of multiple regions. The width of all regions is
  // defined below. If a window is closed and the burning direction happens to be from top
  // to bottom, the different zones are arragned like this:
  //
  //   .      .    .      .   ^
  //      .  :     .:   .     |  smokeRange: In this zone, smoke and little ember
  //   . :  : .. :   :  .     |              particles are drawn.
  //     : .  .:.:  .:  :..   |
  //   ...:: ..: :: : .  ..   |
  //    :: )  .. : : ..: : .  |  ^
  //    ( /(   (  (  . .::.   |  |  flameRange: Flames are drawn in this zone.
  //    )\()) ))\ )( . (  .   |  |
  //   ((_)\ /((_|()\  )\ )   |  |                                     ^
  //   | |(_|_))( ((_)_(_/(   v  v                                     | burnRange: In
  //   /////////////////////  <- hideThreshold: From here on,          | this zone, a
  //   | / / / / / / / / / |  ^                 the window is hidden.  | firery edge is
  //   |  /  /  /  /  /  / |  |                                        v drawn.
  //   | /   /   /   /   / |  v  scorchRange: The window becomes
  //   |                   |                  brownish and a subtle
  //   |                   |                  heat distortion effect
  //   |                   |                  kicks in.
  //   |                   |
  //   \___________________/
  //
  // If a window is opened, the window texture is visible on the other side of the
  // hideThreshold and the scorchRange is also flipped to the other side (so it is drawn
  // below the flames and the smoke).

  // All widths depend on the configured scale of the effect.
  float SCORCH_WIDTH = 0.2 * p_uScale;
  float BURN_WIDTH   = 0.03 * p_uScale;
  float SMOKE_WIDTH  = 0.9 * p_uScale;
  float FLAME_WIDTH  = 0.2 * p_uScale;

  // During the animation, the hideThreshold transitions from a small value to a larger
  // value, so that all the above ranges are covered.
  float hideThreshold =
    mix(uForOpening ? 0.0 : -SCORCH_WIDTH, 1.0 + SMOKE_WIDTH, uProgress);

  // The individual ranges are now given by the current hideThreshold and the widths of
  // the zones.
  vec2 scorchRange = uForOpening ? vec2(hideThreshold - SCORCH_WIDTH, hideThreshold)
                                 : vec2(hideThreshold, hideThreshold + SCORCH_WIDTH);
  vec2 burnRange   = vec2(hideThreshold - BURN_WIDTH, hideThreshold + BURN_WIDTH);
  vec2 flameRange  = vec2(hideThreshold - FLAME_WIDTH, hideThreshold);
  vec2 smokeRange  = vec2(hideThreshold - SMOKE_WIDTH, hideThreshold);

  // Now we compute a 2D gradient in [0..1] which covers the entire window. The dark
  // regions will be burned first, the bright regions in the end. We mix a radial gradient
  // with some noise. The center of the radial gradient is positioned at uStartPos.
  float circle = length((iTexCoord - vec2(p_uStartPosX, p_uStartPosY)) * (uSize.xy / max(uSize.x, uSize.y)));

  vec2 noiseUv = iTexCoord / p_uScale * uSize / 1.5;
  // BMW: `uSeed + uProgress * vec2(0.0, 0.3 * uDuration)`. The
  // `uProgress * uDuration` term is BMW's elapsed-leg-seconds idiom,
  // substituted with `(float(iFrame) / 60.0)` per bmw_compat.glsl.
  float smokeNoise =
    simplex2DFractal(noiseUv * 0.01 + vec2(p_uSeedX, p_uSeedY) + (float(iFrame) / 60.0) * vec2(0.0, 0.3));

  float gradient =
    mix(circle, smokeNoise, 200.0 * p_uTurbulence * p_uScale / max(uSize.x, uSize.y));

  // Now, based on the gradient and the ranges, we can compute masks for the individual
  // zones.
  float smokeMask = smoothstep(0.0, 1.0, (gradient - smokeRange.x) / SMOKE_WIDTH) *
                    getAbsoluteEdgeMask(100.0, 0.3);
  float flameMask = smoothstep(0.0, 1.0, (gradient - flameRange.x) / FLAME_WIDTH) *
                    getAbsoluteEdgeMask(20.0, 0.0);
  float fireMask   = smoothstep(1.0, 0.0, abs(gradient - hideThreshold) / BURN_WIDTH);
  float scorchMask = smoothstep(1.0, 0.0, (gradient - scorchRange.x) / SCORCH_WIDTH);

  if (uForOpening) {
    scorchMask = 1.0 - scorchMask;
  }

  // Now we retrieve the window color. We only show the window when the burning edge has
  // passed.
  vec4 oColor = vec4(0.0);

  if ((!uForOpening && gradient > hideThreshold) ||
      (uForOpening && gradient < hideThreshold)) {

    //  We add some distortion in the scorch zone. This is only possible if using GLES.
    vec2 distort = vec2(0.0);

#ifndef GL_ES
    if (scorchRange.x < gradient && gradient < scorchRange.y) {
      distort = vec2(dFdx(gradient), dFdy(gradient)) * scorchMask * 5.0;
    }
#endif

    // boundaryMask (see noise.glsl) crops the distorted sample to
    // transparent outside [0, 1] — the dFdx/dFdy distort pushes the
    // sample UV past the anchor edge along the scorch front, and
    // uTexture0's clamp-to-edge sampler would otherwise smear the
    // rim texel into the heat-distorted region.
    vec2 sampleUv = iTexCoord + distort;
    oColor = getInputColor(sampleUv) * boundaryMask(sampleUv);
  }

  // Add smoke and embers.
  if (smokeRange.x < gradient && gradient < smokeRange.y) {
    float smoke = smokeMask * smokeNoise;
    oColor      = alphaOver(oColor, vec4(0.5 * vec3(smoke), smoke));

    // BMW: `uSeed - smokeNoise * vec2(0.0, 0.3 * smokeMask * uDuration)`.
    // `uDuration` here is a free scalar (not paired with `uProgress`);
    // we substitute it with 1.0 — a typical BMW leg duration in seconds —
    // so the ember sampling offset stays at its BMW magnitude.
    float emberNoise = simplex2DFractal(
      noiseUv * 0.05 + vec2(p_uSeedX, p_uSeedY) - smokeNoise * vec2(0.0, 0.3 * smokeMask * 1.0));
    float embers = clamp(pow(emberNoise + 0.3, 100.0), 0.0, 2.0) * smoke;
    oColor += getFireColor(embers);
  }

  // Add scorch effect.
  if (scorchRange.x < gradient && gradient < scorchRange.y) {
    oColor.rgb = mix(oColor.rgb, mix(oColor.rgb, vec3(0.1, 0.05, 0.02), 0.4), scorchMask);
  }

  // Add trailing flames.
  if (min(burnRange.x, flameRange.x) < gradient &&
      gradient < max(burnRange.y, flameRange.y)) {
    // BMW: `uSeed + smokeNoise * vec2(0.0, 1.0 * uDuration) +
    //                vec2(0.0, uProgress * uDuration)`.
    // First `uDuration` is a free scalar → 1.0; the `uProgress *
    // uDuration` term → `(float(iFrame) / 60.0)`.
    float flameNoise =
      simplex2DFractal(noiseUv * 0.02 + vec2(p_uSeedX, p_uSeedY) + smokeNoise * vec2(0.0, 1.0) +
                       vec2(0.0, (float(iFrame) / 60.0)));

    if (flameRange.x < gradient && gradient < flameRange.y) {
      float flame = clamp(pow(flameNoise + 0.3, 20.0), 0.0, 2.0) * flameMask;
      flame += clamp(pow(flameNoise + 0.4, 10.0), 0.0, 2.0) * flameMask * flameMask * 0.1;
      oColor += getFireColor(flame);
    }

    // Add burning edge.
    if (burnRange.x < gradient && gradient < burnRange.y) {
      float fire = fireMask * pow(flameNoise + 0.4, 4.0) * oColor.a;
      oColor += getFireColor(fire);
    }
  }

  return premultiply(oColor);
}
