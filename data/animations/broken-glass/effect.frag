// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Broken Glass transition — ported from Burn-My-Windows' broken-glass.frag
// (https://github.com/Schneegans/Burn-My-Windows). Surface shatters into
// glass shards that fly apart. The shards.png atlas (`uTexture1`) encodes
// per-shard layer index in the green channel and per-shard distance-to-
// edge in the red channel; five layers each draw a subset of the shards
// with independent rotation, scale, and gravity for the cascade.
//
// BMW uniform shims come from `<bmw_compat.glsl>`; helpers
// `hash22` / `simplex2D` come from `<noise.glsl>` (byte-equivalent
// to BMW's). See bmw_compat.glsl header for the BMW-to-PlasmaZones
// uniform-name remap.
//
// Note on actor scale: BMW grows the actor by 2x so shards can fly past
// the original window bounds. PlasmaZones' runtime does not double the
// quad, so shards approaching `coords` outside [0, 1] will sample
// transparent black (handled by `getInputColor`'s un-premult guard) and
// land cropped at the original window rectangle. The cascade still reads
// as a shatter; only the off-window flight tail is clipped.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <bmw_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
#define uShardScale customParams[0].x
#define uBlowForce  customParams[0].y
#define uGravity    customParams[0].z

// uSeed: BMW samples Math.random() per leg; PlasmaZones derives a
// stable per-surface seed via `hash22(iSurfaceScreenPos.xy)`. The
// shard atlas is large relative to the per-window coordinate range,
// so even the same seed across legs produces a believable shatter
// — only repeated open/close at the exact same screen position
// replays the identical pattern (acceptable trade-off, see
// noise.glsl header for rationale).
#define uSeed (hash22(iSurfaceScreenPos.xy))

// uEpicenter: BMW defaults to (0.5, 0.5) and only overrides via the
// `broken-glass-use-pointer` setting. PlasmaZones doesn't carry a
// per-leg pointer position uniform, so always use the centre.
#define uEpicenter vec2(0.5)

// Shard atlas: BMW binds shards.png to layer 1 as `uShardTexture`.
// PlasmaZones declares it as the first user texture; `uTexture1` is
// auto-bound from metadata.json's `textures[0]` entry.
#define uShardTexture uTexture1

const float SHARD_LAYERS = 5.0;
const float ACTOR_SCALE  = 2.0;
const float PADDING      = ACTOR_SCALE / 2.0 - 0.5;

void main() {
  vec4 oColor = vec4(0.0);

  float progress = uForOpening ? 1.0 - uProgress : uProgress;

  // Draw the individual shard layers.
  for (float i = 0.0; i < SHARD_LAYERS; ++i) {

    // To enable drawing shards outside of the window bounds, the actor was scaled
    // by ACTOR_SCALE. Here we scale and move the texture coordinates so that the
    // window gets drawn at the correct position again.
    vec2 coords = iTexCoord.st * ACTOR_SCALE - PADDING;

    // Scale and rotate around our epicenter.
    coords -= uEpicenter;

    // Scale each layer a bit differently.
    coords /= mix(1.0, 1.0 + uBlowForce * (i + 2.0) / SHARD_LAYERS, progress);

    // Rotate each layer a bit differently.
    float rotation = (mod(i, 2.0) - 0.5) * 0.2 * progress;
    coords         = vec2(coords.x * cos(rotation) - coords.y * sin(rotation),
                  coords.x * sin(rotation) + coords.y * cos(rotation));

    // Move down each layer a bit.
    float gravity =
      (uForOpening ? -1.0 : 1.0) * uGravity * 0.1 * (i + 1.0) * progress * progress;
    coords += vec2(0.0, gravity);

    // Restore correct position.
    coords += uEpicenter;

    // Retrieve information from the shard texture for our layer.
    vec2 shardCoords = (coords + uSeed) * uSize / uShardScale / 500.0;
    vec2 shardMap    = texture(uShardTexture, shardCoords).rg;

    // The green channel contains a random value in [0..1] for each shard. We
    // discretize this into SHARD_LAYERS bins and check if our layer falls into
    // the bin of the current shard.
    float shardGroup = floor(shardMap.g * SHARD_LAYERS * 0.999);

    if (shardGroup == i && (shardMap.x - pow(progress + 0.1, 2.0)) > 0.0) {
      oColor = getInputColor(coords);
    }
  }

  setOutputColor(oColor);
}
