// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Broken Glass transition — ported from Burn-My-Windows' broken-glass.frag
// (https://github.com/Schneegans/Burn-My-Windows). The surface shatters
// into glass shards that fly apart. The shards.png atlas (`uTexture1`)
// encodes per-shard layer index in the green channel and per-shard
// distance-to-edge in the red channel; five layers each draw a subset of
// the shards with independent rotation, scale, and gravity for the
// cascade.
//
// BMW uniform shims come from `<bmw_compat.glsl>`; the `hash22` helper
// comes from `<noise.glsl>` (byte-equivalent to BMW's). See
// `bmw_compat.glsl` for the BMW→PlasmaZones uniform-name remap.
//
// Note on actor scale: BMW grows the actor by 2x (ACTOR_SCALE=2.0,
// PADDING=0.5) so shards can fly past the original window bounds.
// PlasmaZones achieves the same outcome via `fboExtent: "surface"`
// (metadata.json) — SurfaceAnimator sizes the shader item to the
// QQuickWindow's contentItem, so the cascade has the entire surface
// (= host screen / VS rect post-fullscreen-OSD migration) to fly into,
// not just a 2x bounding box. The `anchorRemap` helper below converts
// the surface-UV `vTexCoord` (Qt-interpolated [0, 1] over the shader
// item) into anchor-space coords; for fragments outside the captured
// anchor's region those coords land outside `[0, 1]` and the
// `getClippedInputColor` helper crops the sample to transparent.
//
// Kwin-effect path: the rasterised quad is the natural one-over-
// frameGeometry mesh. `iAnchorPosInFbo` is `(0, 0)` and
// `iAnchorSize == iResolution`, so `anchorRemap` collapses to identity
// (`coords == vTexCoord`, range `[0, 1]`). Shards stay within the
// captured frame on kwin; the cascade still produces the shatter look
// since the close-leg's progress sweeps the per-shard alpha threshold
// past every visible shard.
//
// `bmw_compat.glsl`'s default `getInputColor` only un-premultiplies the
// sample — it does NOT clip, so an off-anchor lookup on a clamp-to-edge
// `uTexture0` would smear edge pixels (visible artefact for opaque-
// edged windows like terminals or most app chrome). The cascade below
// calls `getClippedInputColor` instead, which crops samples outside
// `[0, 1]` to `vec4(0.0)` before delegating to `getInputColor`.

#include <noise.glsl>

#include <bmw_compat.glsl>

#include <anchor_remap.glsl>

// uEpicenter: BMW defaults to (0.5, 0.5) and only overrides via the
// `broken-glass-use-pointer` setting. PlasmaZones doesn't carry a
// per-leg pointer position uniform, so always use the anchor centre.
#define uEpicenter vec2(0.5)

// Shard atlas: BMW binds shards.png to layer 1 as `uShardTexture`.
// PlasmaZones declares it as the first user texture; `uTexture1` is
// auto-bound from metadata.json's `textures[0]` entry.
#define uShardTexture uTexture1

const float SHARD_LAYERS = 5.0;

// Shard atlas density per anchor-UV unit. BMW computed this from
// `uSize / 500.0` (where `uSize` is the 2x-actor pixel size), so for a
// 1000-pixel window the BMW density was `2000/500 = 4` atlas tiles
// visible across the 2x actor, i.e. ~2 tiles visible across the window
// itself.
//
// PlasmaZones can't reuse that formula safely: under the surface-extent
// model `iAnchorSize` is the captured-anchor pixel size and is normally
// pushed at leg start by `SurfaceAnimator::syncShaderGeometryNow`, but
// transient pre-attach paints, mid-relayout frames, or kwin paths that
// arrive before the first `iAnchorSize` uniform write briefly carry
// `iAnchorSize == (0, 0)`. With `uSize = max(iAnchorSize, vec2(1.0))`
// from bmw_compat that collapses the divisor to 1, making `shardCoords`
// vary by `1/500` per anchor-UV unit — so the atlas barely advances
// across the rendered surface and the discretised `shardGroup` ends up
// piecewise-constant in horizontal bands. Combined with each layer's
// per-row Y-gravity displacement that produced the "horizontal lines
// breaking out" symptom seen on the previous port. Hardcoding the
// equivalent density removes the dependency on a uniform that can
// reach zero, mirroring BMW's effective 2-tiles-across-window default.
const float kShardTilesAcrossAnchor = 2.0;

vec4 pTransition(vec2 uv, float t) {
  // Per-window pseudo-random seed offset for the shard atlas sampling.
  // BMW pulls a fresh `Math.random()` pair per leg; PlasmaZones has no
  // per-leg random uniform, so we derive a deterministic offset from
  // the surface's screen position instead. Add irrational constants to
  // `iSurfaceScreenPos.xy` before hashing — for integer screen
  // positions (common: titlebar-aligned windows on integer DPR
  // displays) the raw `hash22` chain collapses to `(0, 0)` because
  // `fract(int * const)` is zero, and the atlas sampling then loses
  // its per-instance offset (all windows at integer positions get
  // identical shard patterns).
  vec2 surfaceHash = hash22(iSurfaceScreenPos.xy + vec2(11.31, 17.71));

  // Defence-in-depth on the divisor: metadata declares min 0.2, but a
  // host that bypasses validation could push p_uShardScale to zero and
  // turn the shard-atlas UV math below into NaN.
  float shardScaleSafe = max(p_uShardScale, 1e-3);

  vec4 oColor = vec4(0.0);

  float progress = uForOpening ? 1.0 - uProgress : uProgress;

  // Draw the individual shard layers.
  for (float i = 0.0; i < SHARD_LAYERS; ++i) {

    // Convert this fragment's surface-UV into anchor-space coords.
    // Replaces BMW's `iTexCoord*ACTOR_SCALE - PADDING`. For anchors
    // smaller than the surface (typical case) the range naturally
    // exceeds BMW's `[-0.5, 1.5]` — shards fly all the way to the
    // surface boundary instead of being clipped at the 2x bounding
    // box.
    vec2 coords = anchorRemap(vTexCoord);

    // Scale and rotate around our epicenter.
    coords -= uEpicenter;

    // Scale each layer a bit differently.
    coords /= mix(1.0, 1.0 + p_uBlowForce * (i + 2.0) / SHARD_LAYERS, progress);

    // Rotate each layer a bit differently.
    float rotation = (mod(i, 2.0) - 0.5) * 0.2 * progress;
    coords         = vec2(coords.x * cos(rotation) - coords.y * sin(rotation),
                          coords.x * sin(rotation) + coords.y * cos(rotation));

    // Move down each layer a bit.
    float gravity =
      (uForOpening ? -1.0 : 1.0) * p_uGravity * 0.1 * (i + 1.0) * progress * progress;
    coords += vec2(0.0, gravity);

    // Restore correct position.
    coords += uEpicenter;

    // Retrieve information from the shard texture for our layer.
    // `fract` enforces atlas tiling at the shader level so a sampler-
    // wrap-mode misconfiguration (or a runtime path that bypasses the
    // metadata `"wrap": "repeat"` value) can't collapse the cascade
    // back to clamp-to-edge behaviour.
    vec2 shardCoords = fract((coords + surfaceHash) * kShardTilesAcrossAnchor / shardScaleSafe);
    vec2 shardMap    = texture(uShardTexture, shardCoords).rg;

    // The green channel contains a random value in [0..1] for each shard. We
    // discretize this into SHARD_LAYERS bins and check if our layer falls into
    // the bin of the current shard.
    float shardGroup = floor(shardMap.g * SHARD_LAYERS * 0.999);

    // BMW's threshold was pow(progress + 0.1, 2.0), which reaches 1.0 at
    // progress = 0.9 — every shard (shardMap.x <= 1) had popped out by 90%
    // of the leg, leaving a 10% empty band at the destroy tail (and the
    // same dead band at the head of an open leg, the phosphor-peek
    // dead-domain bug). Dropping the +0.1 bias lands the clear at exactly
    // progress = 1; the only head-side cost is BMW's faint pre-cracked
    // shard borders (threshold 0.01 at rest), which vanish.
    if (shardGroup == i && (shardMap.x - progress * progress) > 0.0) {
      oColor = getClippedInputColor(coords);
    }
  }

  return premultiply(oColor);
}
