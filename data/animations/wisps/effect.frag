// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wisps transition — ported from Burn-My-Windows' wisps.frag
// (https://github.com/Schneegans/Burn-My-Windows). Smoke wisps —
// sparse animated noise trails dissipate the surface while several
// overlaid grids of glowing points drift across, carrying the window
// to the realm of dreams.
//
// BMW uniform shims come from `<bmw_compat.glsl>`; helpers
// `hash22`/`simplex2D`/`simplex2DFractal` come from `<noise.glsl>`
// (byte-equivalent to BMW's). See bmw_compat.glsl header for the
// BMW-to-PlasmaZones uniform-name remap.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <bmw_compat.glsl>

// metadata.json declaration order → customParams sub-slots.
// BMW's `uSeed` is vec2 — PlasmaZones param slots are scalar, so we
// expose the two components as separate `uSeedX` / `uSeedY` floats and
// reassemble at the (single) call site below.
#define uScale          customParams[0].x
#define uSeedX          customParams[0].y
#define uSeedY          customParams[0].z

// metadata.json color declaration order → customColors[0..2].
// BMW declares uColorN as vec3; PlasmaZones color slots are vec4 so
// the body samples .rgb. The .a channel is unused.
#define uColor1         customColors[0].rgb
#define uColor2         customColors[1].rgb
#define uColor3         customColors[2].rgb

const float WISPS_RADIUS    = 20.0;
const float WISPS_SPEED     = 10.0;
const float WISPS_SPACING   = 40.0 + WISPS_RADIUS;
const float WISPS_LAYERS    = 8.0;
const float WISPS_IN_TIME   = 0.5;
const float WINDOW_OUT_TIME = 1.0;
const float SCALING         = 0.9;

// Returns a grid of randomly moving points. Each grid cell contains one point which
// moves on an ellipse.
vec4 getWisps(vec2 texCoords, float gridSize, vec2 seed) {

  // Shift coordinates by a random offset and make sure the have a 1:1 aspect ratio.
  vec2 coords = (texCoords + hash22(seed)) * uSize;

  // Apply global scale.
  coords /= gridSize;

  // Get grid cell coordinates in [0..1].
  vec2 cellUV = mod(coords, vec2(1.0));

  // This is unique for each cell.
  vec2 cellID = coords - cellUV + vec2(362.456);

  // Add random rotation, scale and offset to each grid cell.
  float speed = mix(10.0, 15.0, hash12(cellID * seed * 134.451)) / gridSize * WISPS_SPEED;
  float rotation  = mix(0.0, 6.283, hash12(cellID * seed * 54.4129));
  float radius    = mix(0.5, 1.0, hash12(cellID * seed * 19.1249)) * WISPS_RADIUS;
  float roundness = mix(-1.0, 1.0, hash12(cellID * seed * 7.51949));

  vec2 offset = vec2(sin(speed * ((float(iFrame) / 60.0) + 1.0)) * roundness,
                     cos(speed * ((float(iFrame) / 60.0) + 1.0)));
  offset *= 0.5 - 0.5 * radius / gridSize;
  offset = vec2(offset.x * cos(rotation) - offset.y * sin(rotation),
                offset.x * sin(rotation) + offset.y * cos(rotation));

  cellUV += offset;

  vec3 color = tritone(hash12(cellID * seed * 1.256), uColor1, uColor2, uColor3);

  // Use distance to center of shifted / rotated UV coordinates to draw a glaring point.
  float dist = length(cellUV - 0.5) * gridSize / radius;
  if (dist < 1.0) {
    float alpha = min(5.0, 0.01 / pow(dist, 2.0));
    return vec4(color * alpha, alpha);
  }

  return vec4(0.0);
}

void main() {
  float progress = uForOpening ? 1.0 - easeOutQuad(uProgress) : easeOutQuad(uProgress);

  // Scale down the window slightly.
  float scale = 1.0 / mix(1.0, SCALING, progress) - 1.0;
  vec2 coords = iTexCoord.st * (scale + 1.0) - scale * 0.5;

  // Get the color of the window.
  // boundaryMask (see noise.glsl) crops sample texels that drift outside
  // [0, 1] — the `(scale + 1.0)` scale-down above pushes `coords` slightly
  // past the anchor on every edge at progress > 0, and uTexture0's
  // clamp-to-edge sampler would otherwise smear the edge texel into the
  // surrounding region, producing a 1-pixel-wide smear instead of
  // letting the dissolve mask take the surface cleanly to transparent.
  // The mask's bands sit OUTSIDE [0, 1] so identity sampling at
  // progress == 0 gets mask = 1 everywhere with no inner-edge clipping.
  // Same fix glide, fade, morph, popin, inkwell-drop, plasma-flow,
  // ripple, smoke, snap, and soft-warp-fade already apply.
  vec4 oColor = getInputColor(coords) * boundaryMask(coords);

  // Compute several layers of moving wisps.
  vec2 uv = (iTexCoord.st - 0.5) / mix(1.0, 0.5, progress) + 0.5;
  uv /= uScale;
  vec4 wisps = vec4(0.0);
  for (float i = 0.0; i < WISPS_LAYERS; ++i) {
    wisps = alphaOver(wisps, getWisps(uv * 0.3, WISPS_SPACING, vec2(uSeedX, uSeedY) * (i + 1.0)));
  }

  // Compute shrinking edge mask.
  float mask = getRelativeEdgeMask(mix(0.01, 0.5, progress));

  // Compute three different progress values.
  float wispsIn  = smoothstep(0.0, 1.0, clamp(progress / WISPS_IN_TIME, 0.0, 1.0));
  float wispsOut = smoothstep(
    0.0, 1.0, clamp((progress - WISPS_IN_TIME) / (1.0 - WISPS_IN_TIME), 0.0, 1.0));
  float windowOut = smoothstep(0.0, 1.0, clamp(progress / WINDOW_OUT_TIME, 0.0, 1.0));

  // Use a noise function to dissolve the window.
  float noise =
    smoothstep(1.0, 0.0, abs(2.0 * simplex2DFractal(uv * uSize / 250.0) - 1.0));
  float windowMask = 1.0 - (windowOut < 0.5 ? mix(0.0, noise, windowOut * 2.0)
                                            : mix(noise, 1.0, windowOut * 2.0 - 1.0));
  oColor.a *= windowMask * mask;

  // Add the wisps.
  wisps.a *= min(wispsIn, 1.0 - wispsOut) * mask;
  oColor = alphaOver(oColor, wisps);

  // These are pretty useful for understanding how this works.
  // oColor = vec4(vec3(windowMask), 1.0);
  // oColor = vec4(vec3(wisps), 1.0);
  // oColor = vec4(vec3(noise), 1.0);
  // oColor = vec4(vec3(mask*min(wispsIn, 1.0 - wispsOut)), 1.0);

  setOutputColor(oColor);
}
