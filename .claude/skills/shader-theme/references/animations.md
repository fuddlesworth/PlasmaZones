<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Animation pack contract (`data/animations/<id>/`)

Source of truth: `libs/phosphor-animation/src/animationshadereffect.cpp` (`fromJson`),
`data/animations/shared/animation_uniforms.glsl`, `data/schemas/animation-metadata.schema.json`,
`tests/unit/ui/shaders/test_animation_shader_param_wiring.cpp`. Re-read those if anything here
looks stale; the tests are the bar bundled packs must clear.

## Files

```
<id>/metadata.json     required
<id>/effect.frag       required
<id>/effect.vert       only for fboExtent "surface" / geometryGrid packs
<id>/*.png             only when declared under "textures" (max 3)
```

No `preview.png`. Previews are rendered live by the settings app. `data/animations/shared/`
is skipped by every scanner; never put a pack there.

Runtime user dir: `~/.local/share/plasmazones/animations/<id>/`. Discovery is a directory
scan (any subdir with `metadata.json`); user dir beats the system dir; changes hot-reload via
a mtime signature. A pack failing schema validation is skipped with a warning, so validate
before claiming it works.

## metadata.json

Required: `id`, `name`, `fragmentShader`, `parameters` (may be `[]`). Bundled packs must
also have non-empty `description`, `author`, `version`, `category` (test-enforced).

| key | notes |
|---|---|
| `id` | kebab-case, equals the directory name |
| `category` | MUST be one of: `3D`, `Dissolve`, `Distortion`, `Fade`, `Geometry`, `Glitch`, `Particle`, `Physics`, `Pixelation`, `Reveal`, `Slide`, `Tile`, `Zoom` (canonical list in `test_animation_shader_param_wiring.cpp`) |
| `appliesTo` | string[] of event classes. Omitted = universal = every `appearance` surface. `desktop`, `move`, `strip`, `tab` are opt-in and never covered by universal |
| `vertexShader` | optional |
| `fboExtent` | `"anchor"` (default) or `"surface"` (padded canvas; needed for anything that draws outside the window rect) |
| `geometryGrid` | int, N×N tessellation, only with `fboExtent: "surface"` and a custom vert; cap 128, 40 to 48 is typical |
| `audio` | bool, opt-in for `<audio.glsl>` |
| `textures` | `[{ "path": "x.png", "wrap": "clamp"\|"repeat"\|"mirror" }]`, bound to `uTexture1..3` in order |
| `multipass`, `bufferShaders`, `bufferScale`, `bufferFeedback`, `wallpaper`, `depthBuffer` | daemon-only; avoid in a theme pack |
| `author` | attribution for ports goes here: `"PlasmaZones (ported from X, url)"` |

Parameter: `{ "id", "name", "type": float|int|bool|color, "default", "min", "max", "step", "description", "group" }`.
`color` default is `"#RRGGBB"` (animations) and reaches the shader as a `vec4`. Budget: 32 scalar lanes
(float/int/bool, 4 per `customParams` slot) and 16 colors. Ids must match `[A-Za-z0-9_]+`.

## Shader entry (no hand-written `main`, no `#version`, no `vTexCoord`/`fragColor` decls)

The registry prepends `#version 450`, `#include <animation_uniforms.glsl>`, `vTexCoord` in and
`fragColor` out, then the generated `p_<id>` defines. Write exactly one of:

```glsl
// symmetric: t is raw iTime, runtime flips it 1->0 on the close/reverse leg
vec4 pTransition(vec2 uv, float t) { ... }

// asymmetric: BOTH required, t is legProgress() (always 0->1)
vec4 pIn(vec2 uv, float t)  { ... }
vec4 pOut(vec2 uv, float t) { ... }
```

Output is PREMULTIPLIED alpha, clamped, never more opaque than `surfaceColor(uv).a` at that pixel.

Rules:
- Never sample `uTexture0` directly. Use `surfaceColor(uv)` (folds `iAnchorRectInTexture`, the
  KWin Y-flip, `uSurfaceLayer` redirect and `iWindowOpacity`).
- `iTime` is progress 0..1, NOT seconds (exception: `strip` class, where it is seconds). For a
  per-frame shimmer use `iFrame`; for wall-clock motion there is none, design around progress.
- Direction is `iIsReversed` / `p_reversed` / `legProgress()`. Never infer direction by
  inverting progress yourself.
- Params: read `p_<id>`. Colors: guard `length(p_c.rgb) > 0.01 ? p_c.rgb : fallback`.
- `resolutionSafe()` instead of raw `iResolution`. `iAnchorSize` = window logical px.
- No `#ifdef PLASMAZONES_KWIN` in a fragment shader. It is only allowed in `effect.vert`
  (gl_Position + Y-flip arms) and for declaring a kwin-only uniform not in a shared module.
- Every event class must also compile on the Qt-RHI preview ABI. Use
  `desktop_transition.glsl` / `old_content.glsl` / `strip_transition.glsl` for their shared
  samplers. Geometry `iFromRect` / `iToRect` and minimize `iIconRect` already exist in the
  preview UBO; guard their standalone declarations with `#ifdef PLASMAZONES_KWIN`.
- Drag deformation that must work in settings previews uses `iMoveMesh` or `iMoveTrail`.
  Both hosts supply them. The Qt-RHI branch defines `iMoveVelocity`, `iMoveVelocity2` and
  `iMoveOffset` as zero, so they cannot drive a preview.
- Keep loops bounded and cheap; the compositor path is GPU-bound.
- Preserve the full captured surface at visible endpoints, including asymmetric shadows.
  `surfacePadRel()` assumes symmetric padding and only accounts for the layer rect. For
  explicit clipping/reveal bounds, select the rect `surfaceColor()` actually samples
  (`iLayerRectInTexture` when layered, otherwise `iAnchorRectInTexture`) and derive card
  bounds `-rect.xy / rect.zw` through `(1.0 - rect.xy) / rect.zw`. Guard degenerate spans.
  Test asymmetric insets and both layered/unlayered sampling; a centered fixture hides this bug.

Useful helpers: `legProgress()`, `legTranslation(from,to)`, `legTravelShare`, `legDirection`,
`premultiply(c)`, `surfacePadRel()`, `PZ_FINALIZE_COLOR` (applied by the scaffold, do not call).
Uniforms: `iFrame`, `iResolution`, `iMouse`, `iAnchorSize`, `iAnchorPosInFbo`, `iSurfaceScreenPos`
(.xy origin, .zw screen size), `iFromRect`/`iToRect` (geometry), `iSwitchDelta` (desktop),
`iMoveMesh[16]`/`iMoveOffset`/`iMoveVelocity`/`iMoveTrail[16]` (move), `iIconRect` (minimize target).

## Shared includes and licence

| include | licence | provides |
|---|---|---|
| `animation_uniforms.glsl` | LGPL | contract (auto-included; explicit include is harmless) |
| `easing.glsl` | LGPL | `easeOutQuad`, `easeInQuad`, `easeOutCubic`, `easeInOutCubic` |
| `noise.glsl` | LGPL | `hash22`, `hash12`/`niriHash`, `simplex2D`, `simplex2DFractal`, `surfaceSeed()`, `boundaryMaskAA` |
| `anchor_remap.glsl` | LGPL | `anchorRemap(uv)` surface-UV to card-UV |
| `desktop_transition.glsl` | LGPL | `uFromDesktop`/`uToDesktop`, `switchDirection`, `getFromColor`, `getToColor`, `crossFade` |
| `old_content.glsl` | LGPL | `uOldWindow`, `oldColor`, `oldCrossFade` (tab class) |
| `strip_transition.glsl` | LGPL | `uStrip`, `iStripMotion`, `iStripRect`, `getStripColor` |
| `audio.glsl` | LGPL | spectrum sampler, needs `"audio": true` |
| `bmw_compat.glsl` | **GPL-3.0** | Burn-My-Windows shim. Including it makes the pack GPL. Do not use in a theme |

Theme packs are PlasmaZones-original: header `SPDX-License-Identifier: LGPL-2.1-or-later`.

## Event classes and paths

| class | binds | runtime | paths |
|---|---|---|---|
| `appearance` | `uTexture0` | daemon + compositor | `window.appearance.{open,close,minimize,focus}`, `osd.*`, `popup.*`, `shell.appletPopup.*` |
| `geometry` | `iFromRect`/`iToRect` | compositor only | `window.movement`, `.placeIn`, `.placeOut`, `.layoutSwitch` |
| `move` | `iMoveMesh` etc. | compositor only | `window.movement.move` |
| `desktop` | `uFromDesktop`/`uToDesktop` | compositor only | `desktop`, `desktop.switch`, `desktop.peek` |
| `strip` | `uStrip` | compositor only | `scrolling`, `scrolling.view` |
| `tab` | `uOldWindow` | compositor only | `scrolling.tabSwitch` |

`window.movement.resize` and `snapResize` no longer exist. Parents (`window.appearance`,
`window.movement`, `desktop`, `popup`, `osd`) are real cascade parents: an override on the parent
is inherited by every child that has none.

## effect.vert (geometry / surface-extent packs)

Choose pass-through or grid deformation as required by the implementation and host. Preserve
the `#ifdef PLASMAZONES_KWIN` split: `modelViewProjectionMatrix` + `1.0 - texCoord.y` on
kwin, `qt_Matrix` and px delta `* 2.0 / iResolution` on the daemon. Extra varyings use
`layout(location = 1) out ...` and the matching `in` at file scope in the frag.

Read the shared uniforms and vertex scaffold/bake tests for the exact declarations. If a
host detail remains unresolved, inspect only that plumbing in an existing vertex shader
to resolve that contract detail.
