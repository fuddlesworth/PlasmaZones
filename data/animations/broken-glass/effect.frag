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
// Note on actor scale: BMW grows the actor by 2x (ACTOR_SCALE=2.0,
// PADDING=0.5) so shards can fly past the original window bounds. We
// match that on PlasmaZones via the `fboExtent` metadata grammar:
// metadata declares `"fboExtent": "anchor+0.5"`, SurfaceAnimator
// expands the shaderItem QUAD accordingly, and the runtime pushes
// the anchor's region inside the padded FBO via `iAnchorPosInFbo`
// (= (padW, padH)) alongside `iAnchorSize` and Qt-auto-derived
// `iResolution`. The `anchorRemap` helper below converts the padded
// vTexCoord back into anchor-space coords (-0.5..1.5 for ring=0.5),
// exactly what BMW's `iTexCoord*2 - 0.5` produced upstream.
//
// Sampling uses BMW's plain `getInputColor` (un-premultiply via the
// bmw_compat shim, no out-of-[0,1] clip). The shardMap.x alpha gate
// only fires where the atlas says a shard exists, so clamp-to-edge
// on the captured anchor texture is naturally bounded by the shard
// distribution itself — exactly how BMW's reference works.

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

// Anchor's region inside the shader FBO. SurfaceAnimator expands the
// shaderItem QUAD by metadata's `fboExtent: "anchor+N"` ring fraction.
// The runtime pushes `iAnchorPosInFbo` = (padW, padH) and `iAnchorSize`
// = the captured-anchor pixel size; Qt auto-derives `iResolution` from
// the padded shaderItem bounds. The remap below converts the padded
// `iTexCoord` (0..1 over the expanded FBO) back into anchor-space coords
// (-ring..1+ring over the original anchor), reproducing BMW's
// `iTexCoord*ACTOR_SCALE - PADDING` without hardcoding the constants.
// With ring=0.5 (ACTOR_SCALE=2 equivalent) the coords land in [-0.5, 1.5]
// exactly as BMW's broken-glass.frag expects.
//
// Kwin-effect path: actor expansion is implemented at quad-construction
// time. PlasmaZonesEffect::apply() (paint_pipeline.cpp) rebuilds the
// window's quads at `(1+2·ring) × frame` with texCoord remapped to
// `[-ring, 1+ring]`, so `iTexCoord` already arrives in anchor-space.
// `iAnchorPosInFbo` is pushed as (0, 0) and `iAnchorSize == iResolution`,
// so the unified remap math collapses to coords == iTexCoord, giving
// the same `[-ring, 1+ring]` range the daemon path produces.
vec2 anchorRemap(vec2 uv) {
    // Defence in depth: see morph/effect.frag's resSafe comment. The
    // runtime normally pushes positive iResolution / iAnchorSize at
    // leg start, but transient pre-attach or mid-relayout frames can
    // land at zero. Clamp the divisors so a stale frame samples a
    // degenerate but finite UV rather than propagating Inf / NaN
    // through the shard cascade.
    vec2 resSafe = max(iResolution, vec2(1.0));
    vec2 anchorSizePx = max(iAnchorSize, vec2(1.0));
    vec2 anchorTopLeftUv = iAnchorPosInFbo / resSafe;
    vec2 anchorSizeUv = anchorSizePx / resSafe;
    return (uv - anchorTopLeftUv) / anchorSizeUv;
}

// uSeed (BMW: per-leg `Math.random()`) is computed once at the top of
// `main()` from `hash22(iSurfaceScreenPos.xy)` and bound to a local
// `surfaceHash` so the loop below doesn't recompute the hash 5× per
// fragment. See noise.glsl header for the per-instance vs per-leg
// trade-off rationale.

// uEpicenter: BMW defaults to (0.5, 0.5) and only overrides via the
// `broken-glass-use-pointer` setting. PlasmaZones doesn't carry a
// per-leg pointer position uniform, so always use the centre.
#define uEpicenter vec2(0.5)

// Shard atlas: BMW binds shards.png to layer 1 as `uShardTexture`.
// PlasmaZones declares it as the first user texture; `uTexture1` is
// auto-bound from metadata.json's `textures[0]` entry.
#define uShardTexture uTexture1

const float SHARD_LAYERS = 5.0;

void main() {
  // Hoist the per-instance hash so we don't recompute it 5×SHARD_LAYERS
  // times per fragment in the loop below.
  vec2 surfaceHash = hash22(iSurfaceScreenPos.xy);
  // Defence-in-depth on the divisor: metadata declares min 0.2, but a
  // host that bypasses validation could push uShardScale to zero and
  // turn the shard-atlas UV math below into NaN.
  float shardScaleSafe = max(uShardScale, 1e-3);

  vec4 oColor = vec4(0.0);

  float progress = uForOpening ? 1.0 - uProgress : uProgress;

  // Draw the individual shard layers.
  for (float i = 0.0; i < SHARD_LAYERS; ++i) {

    // To enable drawing shards outside of the window bounds, SurfaceAnimator
    // expands the shaderItem QUAD by metadata's `fboExtent: "anchor+0.5"`
    // ring fraction. The helper above turns the padded `iTexCoord` back into
    // anchor-space coords (-0.5..1.5 for ring=0.5), reproducing BMW's
    // `iTexCoord*ACTOR_SCALE - PADDING` from `iAnchorPosInFbo` /
    // `iAnchorSize` / `iResolution` instead of the previous structural
    // `customParams[7].x` slot.
    vec2 coords = anchorRemap(iTexCoord.st);

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
    vec2 shardCoords = (coords + surfaceHash) * uSize / shardScaleSafe / 500.0;
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
