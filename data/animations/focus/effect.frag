// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-FileCopyrightText: 2021-2024 Justin Garza
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Focus transition — ported from Burn-My-Windows' focus.frag
// (https://github.com/Schneegans/Burn-My-Windows). Camera-focus rack:
// the surface comes/goes via a depth-of-field-style radial blur ring
// driven by `easeInOutSine` on the blur radius and `easeInOutCubic`
// on the alpha. Originally authored by Justin Garza; see
// `// SPDX-FileCopyrightText` lines above.
//
// BMW uniform shims (`uForOpening`, `uProgress`, `uSize`,
// `getInputColor`, `setOutputColor`, `easeInOutCubic`, etc.) come
// from `<bmw_compat.glsl>`; see that header for the BMW-to-
// PlasmaZones uniform-name remap and the straight-alpha vs
// premultiplied I/O bridging.
//
// Local helpers: BMW's `easeInOutSine` and `getBlurredInputColor` are
// not exposed by `<bmw_compat.glsl>` (only the easings and color
// helpers actually shared across multiple BMW shaders live there). We
// inline both verbatim from BMW's `resources/shaders/common.glsl` so
// the body below stays 1:1 with upstream — they're trivially small
// and only this shader needs them. Both are GPL-3.0-or-later in BMW,
// matching this file's identifier.
//
// Parameter exposure: BMW's two custom uniforms (`uBlurAmount`,
// `uBlurQuality`) are remapped onto `customParams[0]` sub-slots in
// declaration order; defaults and ranges mirror the upstream
// `focus-blur-*` keys in `schemas/.../burn-my-windows-profile.gschema.xml`
// and the corresponding adjustments in `resources/ui/adw/focus.ui`.

#include <bmw_compat.glsl>

// BMW common.glsl verbatim — sine-based smooth easing for the blur
// radius envelope. Not in `<bmw_compat.glsl>` because no other BMW
// shader ported here needs it yet.
float easeInOutSine(float t) {
  // Smooth start and end, mimicking half a sine wave.
  return -0.5 * (cos(3.141592653589793 * t) - 1.0);
}

// BMW common.glsl — radial Poisson-style blur. Samples in
// `directions` evenly-spaced angles around a circle, each with
// `samples` taps along the radius (intensity-weighted toward the
// centre). `uSize` is supplied by `<bmw_compat.glsl>` and converts
// the pixel-space `radius` into texCoord space.
//
// PlasmaZones tweak vs upstream: BMW divides the accumulator by the
// fixed total tap count (`samples * directions`). On the daemon /
// kwin-effect path `uTexture0` is a finite, non-tiling surface; taps
// whose `uv + offset` falls outside [0, 1] return either clamp-to-edge
// or transparent. Either way, including those taps in the average
// dims the visible blur near the surface boundary (the reported
// "edges fade out" artefact at default uBlurAmount=50). We instead
// accumulate a `weight` that counts only the in-bounds taps and
// normalise by that — equivalent to BMW upstream when the entire
// blur kernel lies inside the window, and a clean reconstruction
// when it doesn't.
vec4 getBlurredInputColor(vec2 uv, float radius, float samples) {
  // Initialize the color accumulator to zero.
  vec4 color = vec4(0.0);
  float weight = 0.0;

  // Define a constant for 2 * PI (tau), which represents a full circle in radians.
  const float tau = 6.28318530718;

  // Number of directions for sampling around the circle.
  const float directions = 15.0;

  // Outer loop iterates over multiple directions evenly spaced around a circle.
  for (float d = 0.0; d < tau; d += tau / directions) {
    // Inner loop samples along each direction, with decreasing intensity.
    for (float s = 0.0; s < 1.0; s += 1.0 / samples) {
      // Calculate the offset for this sample based on direction, radius, and step.
      // The (1.0 - s) term ensures more sampling occurs closer to the center.
      vec2 offset = vec2(cos(d), sin(d)) * radius * (1.0 - s) / uSize;

      // Add the sampled color at the offset position to the accumulator,
      // but only count the tap toward the average if it lies inside the
      // surface's [0, 1] uv range — see header note above.
      vec2 sampleUv = uv + offset;
      if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
          sampleUv.y >= 0.0 && sampleUv.y <= 1.0) {
        color += getInputColor(sampleUv);
        weight += 1.0;
      }
    }
  }

  // Normalize the accumulated color by the in-bounds tap count so an
  // off-window kernel doesn't dim the result toward zero. `max(weight, 1.0)`
  // guards against the (geometrically impossible at radius >= 0) zero-tap
  // case so the divide can't produce a NaN.
  return color / max(weight, 1.0);
}

vec4 pTransition(vec2 uv, float t) {

    // Calculate the progression value based on the animation direction.
    // If opening, use uProgress as-is; if closing, invert the progression.
    float progl = uForOpening ? uProgress : 1.0 - uProgress;

    // Apply easing functions to the progression value:
    // - easedProgressBlur: Used for controlling the blur effect smoothly.
    // - easedProgressAlpha: Used for controlling the alpha (opacity) transition.
    float easedProgressBlur = easeInOutSine(progl);  // Sine-based smooth easing for blur.
    float easedProgressAlpha = easeInOutCubic(progl);  // Cubic-based smooth easing for alpha.

    // Calculate the blur amount by interpolating (mixing) between the maximum blur (uBlurAmount)
    // and zero blur based on the eased progression value.
    float blurAmount = mix(p_uBlurAmount, 0.0, easedProgressBlur);

    // Apply the calculated blur effect to the texture at the current texture coordinates.
    // The blur function uses the blur amount and quality (p_uBlurQuality) for sampling.
    vec4 texColor = getBlurredInputColor(iTexCoord.st, blurAmount, p_uBlurQuality);

    // Calculate the alpha value for the transition using eased progress.
    // This determines how transparent the final color will appear.
    float alpha = easedProgressAlpha;

    // Apply the alpha transition to the final texture color.
    // Multiply the texture's alpha channel by the computed alpha value.
    texColor.a *= alpha;

    // Output the final color with the applied blur and alpha transition.
    return premultiply(texColor);
}
