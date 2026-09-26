<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Overlay (zone) and surface (decoration) pack contracts

Source of truth: `plasmazones/data/overlays/shared/common.glsl`, `plasmazones/src/daemon/rendering/zoneentryscaffold.cpp`,
`phosphor-libs/data/schemas/shader-metadata.schema.json`; `plasmazones/data/surface/shared/surface_uniforms.glsl`,
`plasmazones/data/surface/shared/surface_lib.glsl`, `phosphor-libs/libs/phosphor-surface/include/PhosphorSurface/SurfaceShaderEffect.h`,
`phosphor-libs/data/schemas/surface-metadata.schema.json`. Read the two shared headers before writing.

| | overlay | surface |
|---|---|---|
| dir | `plasmazones/data/overlays/<id>/` | `plasmazones/data/surface/<id>/` |
| files | `metadata.json`, `effect.frag`, optional `passN.frag` | `metadata.json`, `effect.frag`, optional `passN.frag`, optional vertex shader, up to three `textures` |
| licence header | `GPL-3.0-or-later` | mixed tree: LGPL for a new PlasmaZones-original pack, but the bundled backdrop packs (blur, glass, frosted-glass, rippled-glass, rain-glass, duotone, mosaic) are `GPL-3.0-or-later`; follow the closest sibling and CLAUDE.md "License" |
| entry | `vec4 pZone(ZoneCtx z)` or `vec4 pImage(vec2 fragCoord)` | `vec4 pSurface(vec2 uv)` |
| runtime | daemon only (Qt-RHI) | daemon AND KWin compositor, same source; the compositor compiles it with `PLASMAZONES_KWIN` defined and the shared modules carry both arms, so never add a branch on it yourself |
| colour default | `#RRGGBB` or `#AARRGGBB` (Qt form, alpha FIRST); all families parse with `QColor`, and a CSS-style `#RRGGBBAA` is silently misread, not rejected | same |
| user dir | `~/.local/share/plasmazones/overlays/<id>` | `~/.local/share/plasmazones/surface/<id>` |
| selection | `Overlays.OverlayShaderTree` global/per-layout assignment, or rule override | `Decorations.DecorationProfileTree` chain per surface path |

No `preview.png`. No `#version`, no in/out declarations, no `main()`: the scaffold prepends
`#version 450`, the family lib include, `vTexCoord`/`vFragCoord`/`fragColor`, and appends the
generated `main()`. A file that defines `main()` itself is passed through unchanged (legacy).

## Overlay packs

### metadata.json

Required `id`, `name`, `fragmentShader`. Add `description`, `author`, `version`, `category`.
`category` is free-form; existing vocabulary: `Branded`, `Energy`, `Cyberpunk`, `Audio Visualizer`,
`Organic`, `3D`. Use one of those. An optional top-level `presets` object (named maps of
param id to value) is accepted; a theme does not need it.

Parameter, OVERLAY ONLY: `{ id, name, group, type: float|int|bool|color|image, default, min, max, slot?, unit?: "px", use_zone_color?, wrap? }`.
Those are the ONLY keys: the overlay schema is `additionalProperties: false` too, and it is
enforced at scan, so a param carrying `description` or `step` (legal on surface and animation
params) makes the whole pack fail validation and be skipped. Omit `slot` (auto-slot by
declaration order per pool: scalar lanes 0..31, colors 0..15, images 0..3). Put
`"unit": "px"` on every param that is a logical-px length so the settings preview scales it.

The surface param shape is different and its schema is `additionalProperties: false` as
well, so copying this line into a surface pack makes the file invalid. `unit`, `slot`,
`use_zone_color`, `wrap` and `type: image` are all overlay-only. See the surface section
below before writing one.
Multipass: `"multipass": true, "bufferShaders": ["pass0.frag"], "bufferFeedback": true, "bufferWrap": "clamp"`;
each `passN.frag` is a `pImage` body that samples only through `channelUv()`. Buffer passes
compile WITHOUT the generated `p_<id>` defines on every host and in the validator, so a
pass reads parameters by their raw `customParams`/`customColors` slot (declaration order).
Avoid multipass unless the chosen visual mechanism requires persistent state or
intermediate sampling; account for the pass count and buffer scale.

### Contract (common.glsl is auto-included)

UBO: `iTime` (seconds, wrapped, safe), `iTimeHi` (the wrap counter `timeSin`/`timeCos` rely
on), `iTimeDelta`, `iFrame`, `iResolution` (device px), `zoneCount`, `highlightedCount`,
`iMouse` (xy px, zw normalised, Y-down), `iDate`, `customParams[8]`, `customColors[16]`,
`iAudioSpectrumSize`, `iChannelResolution[4]` (vec2 here, unlike the vec4 of the other
families), `iTextureResolution[4]`, `iFlipBufferY`, `zoneRects[64]`, `zoneFillColors[64]`,
`zoneBorderColors[64]`, `zoneParams[64]`, `uZoneScale`.
Sampler `uZoneLabels` at binding 1 (`labelsUv(fragCoord)`).

`ZoneCtx`: `index`, `fragCoord`, `rect`, `fillColor` (rgb PREMULTIPLIED by activeOpacity),
`borderColor` (straight), `params` (x = corner radius, y = border width, both LOGICAL px,
z = highlight flag), `isHighlighted`.

Generated `pZone` main loops every zone, `blendOver`s the results, then `clampFragColor`.
So `pZone` returns a straight colour for that zone only; do not clamp or premultiply yourself.
`pImage(fragCoord)` is for full-screen effects that loop zones themselves.

Scale doctrine (PR #841, reviewers enforce it):
- corner radius: `ZoneSDF s = zoneSdf(z.fragCoord, z.rect, z.params.x)` (never `max(radius, N)` floors; the result must not reuse the `z` parameter's name, GLSL shares one scope between a function's parameters and its body)
- border width: `zoneBorderWidth(z.params.y)`
- any other length measured from a zone edge (glow reach, ring travel, inset): `zoneLen(logicalPx)`
- a stroke derived from the border: `zoneStrokeWidth(deviceWidth)`
- an effect that sits near the edge and must survive border width 0: `zoneEdgeBand(deviceWidth, minLogical)`
- `pxScale()` is 1080p-relative and only for full-screen background patterns
- tint from the zone colour: `zoneTint(base, z.fillColor, weight)` or `zoneFillHue(z.fillColor)`,
  never `fillColor.rgb` raw
- `borderColor.a` scales the whole border contribution
- fill alpha comes from the pack's own `fillOpacity` param (catalogue convention)

Helpers: `timeSin/timeCos`, `sdRoundedBox`, `sdSegment`, `rot`, `hash11/21/22`, `noise1D/2D`,
`angularNoise`, `fbm`, `triStopPalette`, `iqPalette`, `softBorder`, `expGlow`, `expGlowBounded`,
`colorWithFallback`, `luminance`, `zoneVitality`, `vitalityDesaturate`, `vitalityScale`,
`blendOver`, `labelsUv`. Opt-in includes: `<audio.glsl>`, `<multipass.glsl>`, `<flow-noise.glsl>`
(`curlNoise`), `<textures.glsl>`, `<wallpaper.glsl>`, `<depth.glsl>`, `<logo-drift.glsl>`.

Declare only parameters the implementation uses. Use `colorWithFallback` for color defaults
and `uZoneLabels` for labels when provided. Highlighted zones must be clearly distinguishable
from idle ones; the user's brief determines how that state is expressed.

## Surface packs

### metadata.json

Required `id`, `name`, `fragmentShader`, `parameters` (may be `[]`). Top level is
`additionalProperties: false`, so only these keys: `description`, `author`, `version`,
`category`, `preview`, `vertexShader`, `animated`, `audio`, `providesBorder`,
`providesOpacityTint`, `needsBackdrop`, `interiorOpaque`, `multipass`, `bufferShaders` (max 8),
`bufferScale` (0.0078125..1.0), `bufferScales` (per-pass, same bounds, one entry per
`bufferShaders` entry, surface-only), `bufferFeedback`, `bufferWrap(s)`, `bufferFilter(s)`, `depthBuffer`,
`halfFloatBuffers`, `paddingParam`, `textures` (max 3), `presets`, and the legacy `handlesOpacity`
(accepted, ignored; do not write it). Max 48 params. No `slot` field (auto-slot by
declaration order). Param keys, and ONLY these, because the schema is
`additionalProperties: false`: `id, name, description, group, type, default, min, max, step`.
`type` is `float`, `int`, `bool` or `color`: a surface pack has no image params, the runtime
would bind one as a scalar lane, and images are declared under `textures` and read from
`uTexture1..3`. No `unit` here, unlike an overlay param. A logical-px length in a surface
shader is scaled with `* uSurfaceScale` in the shader instead.

`category` free-form; vocabulary: `Borders`, `Blur`, `Ambience`, `Focus`.

Contract flags (declare honestly, the hosts key behaviour on them):
- `animated`: pack reads `iTime` or `iMouse`. Static packs omit it and cost nothing.
- `needsBackdrop`: the pack samples the scene behind the window. Include
  `<surface_backdrop.glsl>` and read `backdropTexel(uv)`. On the compositor that is a
  per-frame capture of the scene under the padded canvas, clamped into `uBackdropRect`. On
  the daemon and in previews it is the desktop wallpaper when the host supplies one, and
  transparent when it does not. Always branch on `uHasBackdrop >= 0.5` and draw a fallback
  slab in the other arm; the gate scalar lives in `surface_uniforms.glsl`, so you can test it
  without including the sampler module.
- `paddingParam: "<paramId>"`: names the int/float logical-px param that is the transparent
  outer margin the pack draws into (glow, shadow, particles). Host inflates the canvas by the
  chain's largest request.
- `interiorOpaque`: only if the pack NEVER lowers alpha inside the frame rect. Two routes
  qualify: a halo confined to the transparent margin (glow, shadow) and a pane composited
  behind the PREVIOUS STAGE's texel with `slabComposite`, which can only raise alpha
  (fireflies, phosphor-motes). Note the second route needs no backdrop: neither pack declares
  `needsBackdrop`, and `window` there is `surfaceTexel`, not the capture. Every border, glass
  and tint pack leaves it false.
- `roundBottomCorners` (bool, default true): ANY pack that resolves the frame through a corner
  radius MUST declare this, whatever else it declares. It is not a `providesBorder` concern —
  ten of the twenty bundled declarers are blur, glass, glow and shadow packs. The host resolves
  ONE answer per chain and injects it into every pack that declares it, so a pack that omits it
  keeps rounded bottoms inside a chain the user squared, which is the exact disagreement the
  chain-wide answer exists to remove. A pack that places content around the frame RECT with no
  corner radius at all has no silhouette to reconcile and correctly omits it: phosphor-motes
  does that through `framePerimeter`, fireflies from its own half-size and centre.
- `edgeSoftness` (float): the anti-alias feather of the edge, and like the switch above it is
  not a `providesBorder` concern — the eight Blur declarers set no such flag. The Borders
  family declares default 0.7 over 0.1–2.0, the Blur family default 1.0 over 0.1–3.0.
- `providesBorder`: declare params `borderWidth` and `cornerRadius` (ints, px). Settings seeds
  them from the plain border setting, by exact id, and skips silently when an id is absent. A
  multi-band pack may name its widths otherwise (border-double has `outerWidth`, `gapWidth` and
  `innerWidth` and no `borderWidth`), in which case only `cornerRadius` is seeded and the widths
  keep their pack defaults. That is supported, not a mistake.
- `providesOpacityTint`: declare `opacity`, `tintStrength`, `tintColor`.
- `audio`: includes `<surface_audio.glsl>`.
- Blur, and multipass in general: declare THREE keys, not one. `"multipass": true` is what
  makes the chain run at all (`bufferShaders` alone is cleared at load and the pack renders
  single-pass, silently), `"needsBackdrop": true` is what gives the first pass a source, and
  `"bufferShaders"` is the chain.
- The chain every bundled blur-family pack runs is the DUAL KAWASE PYRAMID, seven passes with
  a per-pass scale list:

  ```json
  "bufferShaders": ["builtin:kawase-down-0", "builtin:kawase-down-1",
                    "builtin:kawase-down-2", "builtin:kawase-down-3",
                    "builtin:kawase-up-0",   "builtin:kawase-up-1",
                    "builtin:kawase-up-2"],
  "bufferScales":  [0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25]
  ```

  The main pass includes `<surface_multipass.glsl>` and reads the finished blur as
  `surfaceBlurTexel(uv)`, which is `iChannel6`, the last UP pass. The older
  `builtin:gaussian-h` / `builtin:gaussian-v` pair still resolves and still reads back as
  `iChannel1`, for a third-party pack that declares it. An unknown token, or any missing
  pack-local pass, disables multipass for the whole pack and it renders single-pass.
  `iChannelResolution[N].xy` gives the pixel size of the FIRST FOUR channels only; a pass
  reading iChannel4..7 sizes it with `textureSize()`, which is what the Kawase passes do. Both
  runtimes run the buffer passes: the daemon through the shared ShaderEffect node and the
  compositor through its own composite fold (`surfacelayers.cpp`). The compositor only falls
  back to single-pass when a buffer pass fails to load or compile, in which case the iChannel samplers
  are unbound, so the main pass must still look sane when `uHasBackdrop` is 0 or the blurred
  sample is empty. `bufferFeedback` (sampling your own previous frame) is honoured on the
  daemon and ignored on the compositor, so do not build a surface pack on it.

  Buffer passes compile WITHOUT the generated `p_<id>` defines on every host and in the
  validator, so they read parameters by raw slot. The builtin gaussian passes read
  `customParams[0].x` as the radius, which means `blurRadius` MUST be the first scalar
  (float/int/bool) parameter declared in `metadata.json`; every bundled blur-family pack does
  this, and nothing lints it. Colours do not affect the slot. A pack-local `passN.frag` follows
  the same rule: `#include <surface_uniforms.glsl>` (plus the backdrop/multipass modules it
  needs) and index `customParams` directly. On the compositor a buffer pass receives only
  `uTexture0`, `iTime`, `uSurfaceSize`, `uSurfaceScale`, `uBackdrop`, `uBackdropRect`,
  `uHasBackdrop`, the audio pair, `iChannel0..7`, `iChannelResolution[]` (first four only), `customParams[]` and
  `customColors[]`; `uSurfaceFrameTopLeft`, `uSurfaceFrameSize`, `uSurfaceFocused` and `iMouse`
  are not pushed to buffer passes there and read zero, so keep frame-relative and pointer
  logic in the main pass.

### Contract (surface_lib.glsl is auto-included; it includes surface_uniforms.glsl)

Uniforms (same names on both runtimes): `uTexture0` (previous chain stage's output, sample via
`surfaceTexel(uv)`), `uSurfaceSize` (padded canvas, device px), `uSurfaceFrameTopLeft`,
`uSurfaceFrameSize` (the window frame rect inside the canvas), `uSurfaceScale` (logical to
device), `uSurfaceFocused` (0/1), `iTime` (seconds, only for `animated`), `iAudioSpectrumSize`,
`customParams[8]`, `customColors[16]`, `uHasBackdrop`, `uBackdropRect` (the capture's rect,
used by `backdropTexel()`), `iChannelResolution[4]`, `iMouse` (xy device px top-down,
`x < 0` = off surface), `uTexture1..3`, `iTextureResolution[4]`. `uSurfaceOpacity` is legacy 1.0.

Every logical-px param is multiplied by `uSurfaceScale` at the call site, EXCEPT for the
helpers documented as taking logical px: `standardBorderBandSplit` scales `borderWidth`,
`cornerRadius` and `bottomRadius` internally, so pre-scaling them double-scales on a 2x
display. `frameSdfSplit`, `surfaceSlabOpen` and `haloFalloff` take device px. Output is the
final PREMULTIPLIED colour; there is no clamp pass after `pSurface`.

Helpers: `surfacePixel(uv)` (device px, runtime Y handled), `surfaceTexel(uv)`,
`surfaceFrameDegenerate()`, `frameSdfSplit(p, topRadiusDevicePx, bottomRadiusDevicePx)`,
`frameMask(d[, aaDevicePx])`, `focusDim(lo)`,
`standardBorderBandSplit(p, borderWidthLogical, cornerRadiusLogical, bottomRadiusLogical, aaDevicePx)` -> `{fs, insideMask, edge}`,
`borderComposite(tex, bandColor, edge, insideMask)`,
`surfaceSlabOpen(uv, topRadiusDevicePx, bottomRadiusDevicePx, aaDevicePx)`
-> `{window, px, fs, mask}`, `slabComposite(window, pane)`, `marginComposite(base, rgb, a)`,
`faintTintSlab`,
`surfaceBottomRadius(roundedRadius, roundBottomFlag)`,
`haloFalloff(d, reach, edgePx /* vec2 */, baseAlpha, strength, focusFloor, gateCornerTopDevicePx,
gateCornerBottomDevicePx)`,
`frameUv(px)`, `pxToUv(v)`, `framePerimeter(p, center, halfSize)` (-0.5..0.5 around the frame,
for travelling gleams).

The uniform-radius `standardBorderBand`, `frameSdf` and two-arg `surfaceSlabOpen`, and the
six- and seven-arg `haloFalloff`, still exist as third-party compatibility overloads. Do NOT
write a new pack against them: the seven-arg `haloFalloff` takes one radius for both ends and
the six-arg form a square gate at both, and the rest take one radius for all four corners, so
a pack that uses any of them cannot follow the chain's bottom-corner answer.

Derive the bottom radius with `surfaceBottomRadius`, which every bundled declarer calls:

```glsl
// feeding standardBorderBandSplit — logical px, because it scales internally
float bottomRadius = surfaceBottomRadius(p_cornerRadius, p_roundBottomCorners);
// feeding surfaceSlabOpen / frameSdfSplit / haloFalloff — device px
float cornerPx = p_cornerRadius * uSurfaceScale;
float bottomPx = surfaceBottomRadius(cornerPx, p_roundBottomCorners);
```

Label by CONSUMER, not by family: border-double is a Borders pack that builds its bands from
`frameSdfSplit`, so it takes the second form and passes a radius it has already dilated by its
own stack width. Do not match on the variable NAME either — it spells that device-px value
`bottomRadius`, which is the name the first form uses for a logical-px one. The helper is a pure select, so it neither scales nor clamps and the unit you
hand it is the unit you get back. Pass whichever one your consumer wants, and do not pre-scale for
`standardBorderBandSplit` or the bottom end gets scaled twice while the top stays right.
A ZERO radius means square and is deliberately not dilated by the band width, so the outline
coincides with a squared backdrop slab underneath it.

Compositing contracts:
- border: sample the texel, guard degenerate frames, use `standardBorderBandSplit` with the
  derived bottom radius, apply `focusDim` and finish with `borderComposite`.
- glass: use `surfaceSlabOpen` with the derived bottom radius, preserve the intended content
  opacity, branch on `uHasBackdrop`, provide a fallback slab and finish with `slabComposite`.
- margin effect: use `frameSdfSplit` to locate the outer region and `marginComposite` to
  preserve the interior.

Focus: `focusDim(0.30 .. 0.65)` on the effect's alpha or brightness. Every decoration pack dims
when unfocused. Chains are serial filters: order in `chain` is bottom to top, so glass first,
border second, shadow/ambience after (they draw in the margin and pass the interior through).

## Zone overlay selection reminder

Since schema v8, overlay assignments live in `Overlays.OverlayShaderTree`, with a global
default and optional per-layout overrides. Deliver an overlay set as described in
`profiles.md`; use the registry UUID rather than the metadata slug for `shaderId`.
Do not write a shader assignment into a layout JSON file.
