<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Overlay (zone) and surface (decoration) pack contracts

Source of truth: `data/overlays/shared/common.glsl`, `src/daemon/rendering/zoneentryscaffold.cpp`,
`data/schemas/shader-metadata.schema.json`; `data/surface/shared/surface_uniforms.glsl`,
`data/surface/shared/surface_lib.glsl`, `libs/phosphor-surface/include/PhosphorSurface/SurfaceShaderEffect.h`,
`data/schemas/surface-metadata.schema.json`. Read the two shared headers before writing.

| | overlay | surface |
|---|---|---|
| dir | `data/overlays/<id>/` | `data/surface/<id>/` |
| files | `metadata.json`, `effect.frag`, optional `passN.frag` | `metadata.json`, `effect.frag` |
| licence header | `GPL-3.0-or-later` | `LGPL-2.1-or-later` |
| entry | `vec4 pZone(ZoneCtx z)` or `vec4 pImage(vec2 fragCoord)` | `vec4 pSurface(vec2 uv)` |
| runtime | daemon only (Qt-RHI) | daemon AND KWin compositor, same source |
| colour default | `#RRGGBB` | `#AARRGGBB` (Qt form, alpha FIRST) |
| user dir | `~/.local/share/plasmazones/overlays/<id>` | `~/.local/share/plasmazones/surface/<id>` |
| selection | `Overlays.OverlayShaderTree` global/per-layout assignment, or rule override | `Decorations.DecorationProfileTree` chain per surface path |

No `preview.png`. No `#version`, no in/out declarations, no `main()`: the scaffold prepends
`#version 450`, the family lib include, `vTexCoord`/`vFragCoord`/`fragColor`, and appends the
generated `main()`. A file that defines `main()` itself is passed through unchanged (legacy).

## Overlay packs

### metadata.json

Required `id`, `name`, `fragmentShader`. Add `description`, `author`, `version`, `category`.
`category` is free-form; existing vocabulary: `Branded`, `Energy`, `Cyberpunk`, `Audio Visualizer`,
`Organic`, `3D`. Use one of those.

Parameter, OVERLAY ONLY: `{ id, name, group, type: float|int|bool|color|image, default, min, max, slot?, unit?: "px", use_zone_color?, wrap? }`.
Omit `slot` (auto-slot by declaration order per pool: scalar lanes 0..31, colors 0..15, images 0..3).
Put `"unit": "px"` on every param that is a logical-px length so the settings preview scales it.

The surface param shape is NARROWER and its schema is `additionalProperties: false`, so
copying this line into a surface pack makes the file invalid. `unit`, `slot`,
`use_zone_color` and `wrap` are all overlay-only. See the surface section below before
writing one.
Multipass: `"multipass": true, "bufferShaders": ["pass0.frag"], "bufferFeedback": true, "bufferWrap": "clamp"`;
each `passN.frag` is a `pImage` body that samples only through `channelUv()`. Avoid multipass
unless the chosen visual mechanism requires persistent state or intermediate sampling;
account for the pass count and buffer scale.

### Contract (common.glsl is auto-included)

UBO: `iTime` (seconds, wrapped, safe), `iTimeDelta`, `iFrame`, `iResolution` (device px),
`zoneCount`, `highlightedCount`, `iMouse` (xy px, zw normalised, Y-down), `iDate`,
`customParams[8]`, `customColors[16]`, `iAudioSpectrumSize`, `zoneRects[64]`,
`zoneFillColors[64]`, `zoneBorderColors[64]`, `zoneParams[64]`, `uZoneScale`.
Sampler `uZoneLabels` at binding 1 (`labelsUv(fragCoord)`).

`ZoneCtx`: `index`, `fragCoord`, `rect`, `fillColor` (rgb PREMULTIPLIED by activeOpacity),
`borderColor` (straight), `params` (x = corner radius, y = border width, both LOGICAL px,
z = highlight flag), `isHighlighted`.

Generated `pZone` main loops every zone, `blendOver`s the results, then `clampFragColor`.
So `pZone` returns a straight colour for that zone only; do not clamp or premultiply yourself.
`pImage(fragCoord)` is for full-screen effects that loop zones themselves.

Scale doctrine (PR #841, reviewers enforce it):
- corner radius: `ZoneSDF z = zoneSdf(fragCoord, rect, z.params.x)` (never `max(radius, N)` floors)
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
`providesOpacityTint`, `needsBackdrop`, `interiorOpaque`, `multipass`, `bufferShaders` (max 4),
`bufferScale` (0.125..1.0), `bufferFeedback`, `bufferWrap(s)`, `bufferFilter(s)`, `depthBuffer`,
`halfFloatBuffers`, `paddingParam`, `textures` (max 3). Max 48 params. No `slot` field
(auto-slot by declaration order). Param keys, and ONLY these, because the schema is
`additionalProperties: false`: `id, name, description, group, type, default, min, max, step`.
No `unit` here, unlike an overlay param. A logical-px length in a surface shader is scaled
with `* uSurfaceScale` in the shader instead.

`category` free-form; vocabulary: `Borders`, `Blur`, `Ambience`, `Focus`.

Contract flags (declare honestly, the hosts key behaviour on them):
- `animated`: pack reads `iTime` or `iMouse`. Static packs omit it and cost nothing.
- `needsBackdrop`: samples the scene behind the window via `<surface_backdrop.glsl>` /
  `iChannel1` after the builtin blur. MUST branch on `uHasBackdrop >= 0.5` and render a
  fallback slab when 0 (daemon and preview hosts have no scene).
- `paddingParam: "<paramId>"`: names the int/float logical-px param that is the transparent
  outer margin the pack draws into (glow, shadow, particles). Host inflates the canvas by the
  chain's largest request.
- `interiorOpaque`: only if the pack NEVER lowers alpha inside the frame rect (shadow, glow).
  Every border, glass, tint pack leaves it false.
- `providesBorder`: declare params `borderWidth` and `cornerRadius` (ints, px). Settings seeds
  them from the plain border setting.
- `providesOpacityTint`: declare `opacity`, `tintStrength`, `tintColor`.
- `audio`: includes `<surface_audio.glsl>`.
- Blur: `"multipass": true, "bufferScale": 0.25, "bufferShaders": ["builtin:gaussian-h", "builtin:gaussian-v"]`,
  param `blurRadius` (int), `#include <surface_multipass.glsl>`, read `texture(iChannel1, uv)`.
  The compositor degrades multipass to single-pass, so the pane must still look sane without the blur.

### Contract (surface_lib.glsl is auto-included; it includes surface_uniforms.glsl)

Uniforms (same names on both runtimes): `uTexture0` (previous chain stage's output, sample via
`surfaceTexel(uv)`), `uSurfaceSize` (padded canvas, device px), `uSurfaceFrameTopLeft`,
`uSurfaceFrameSize` (the window frame rect inside the canvas), `uSurfaceScale` (logical to
device), `uSurfaceFocused` (0/1), `iTime` (seconds, only for `animated`), `iAudioSpectrumSize`,
`customParams[8]`, `customColors[16]`, `uHasBackdrop`, `iMouse` (xy device px top-down,
`x < 0` = off surface), `uTexture1..3`, `iTextureResolution[4]`. `uSurfaceOpacity` is legacy 1.0.

Every logical-px param is multiplied by `uSurfaceScale` at the call site. Output is the final
PREMULTIPLIED colour; there is no clamp pass after `pSurface`.

Helpers: `surfacePixel(uv)` (device px, runtime Y handled), `surfaceTexel(uv)`,
`surfaceFrameDegenerate()`, `frameSdf(p, radiusDevicePx)`, `frameMask(d)`, `focusDim(lo)`,
`standardBorderBand(p, borderWidthLogical, cornerRadiusLogical)` -> `{fs, insideMask, edge}`,
`borderComposite(tex, bandColor, edge, insideMask)`, `surfaceSlabOpen(uv, cornerRadiusDevicePx)`
-> `{window, px, fs, mask}`, `slabComposite(window, pane)`, `marginComposite(base, rgb, a)`,
`faintTintSlab`, `haloFalloff(d, reach, edgePx, baseAlpha, strength, focusFloor)`, `frameUv(px)`,
`pxToUv(v)`, `framePerimeter(p, center, halfSize)` (-0.5..0.5 around the frame, for travelling
gleams).

Compositing contracts:
- border: sample the texel, guard degenerate frames, use `standardBorderBand`, apply
  `focusDim` and finish with `borderComposite`.
- glass: use `surfaceSlabOpen`, preserve the intended content opacity, branch on
  `uHasBackdrop`, provide a fallback slab and finish with `slabComposite`.
- margin effect: use `frameSdf` to locate the outer region and `marginComposite` to preserve
  the interior.

Focus: `focusDim(0.30 .. 0.65)` on the effect's alpha or brightness. Every decoration pack dims
when unfocused. Chains are serial filters: order in `chain` is bottom to top, so glass first,
border second, shadow/ambience after (they draw in the margin and pass the interior through).

## Zone overlay selection reminder

Since schema v8, overlay assignments live in `Overlays.OverlayShaderTree`, with a global
default and optional per-layout overrides. Deliver an overlay set as described in
`profiles.md`; use the registry UUID rather than the metadata slug for `shaderId`.
Do not write a shader assignment into a layout JSON file.
