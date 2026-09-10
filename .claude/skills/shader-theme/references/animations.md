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
<id>/effect.vert       optional; required with geometryGrid; ignored for desktop/strip packs
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
| `textures` | `[{ "path": "x.png", "wrap": "clamp"\|"repeat"\|"mirror" }]`, bound to `uTexture1..3` in order. Desktop and strip packs must not declare textures, `vertexShader` or `geometryGrid` (the validator lints all three: the pass binds only its scene captures, which alias `uTexture1`/`uTexture2` on the preview branch, and draws its own quad). A tab pack may declare at most two textures, because `old_content.glsl` maps `uOldWindow` onto `uTexture3` on the preview branch; the validator does not lint this, so check by hand |
| `multipass`, `bufferShaders`, `bufferScale`, `bufferFeedback`, `wallpaper`, `depthBuffer` | daemon-only; avoid in a theme pack |
| `author` | attribution for ports goes here: `"PlasmaZones (ported from X, url)"`. A port of copyleft upstream code also keeps its licence (see the licence section below) |

Parameter: `{ "id", "name", "type": float|int|bool|color, "default", "min", "max", "step", "description", "group" }`.
`color` default is `"#RRGGBB"` or Qt-form `"#AARRGGBB"` (alpha FIRST) and reaches the shader as a
`vec4`; every family parses colours with `QColor`, so a CSS-style `#RRGGBBAA` is silently misread
rather than rejected. Budget: 32 scalar lanes (float/int/bool, 4 per `customParams` slot) and
16 colors; the validator lints an overflow, the registry drops the surplus at load. Ids must
match `[A-Za-z0-9_]+`.

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
  per-frame shimmer use `iFrame`. There is no wall clock: `iTimeDelta` exists on both hosts but
  cannot be accumulated without state, so design around progress or `iFrame`.
- Direction is `iIsReversed` / `p_reversed` / `legProgress()`. Never infer direction by
  inverting progress yourself.
- Params: read `p_<id>`. Colors: guard `length(p_c.rgb) > 0.01 ? p_c.rgb : fallback`.
- `resolutionSafe()` instead of raw `iResolution`. `iAnchorSize` = window logical px.
- No `#ifdef PLASMAZONES_KWIN` in a fragment shader. It is only allowed in `effect.vert`
  (gl_Position + Y-flip arms) and for declaring a kwin-only uniform not in a shared module.
- Every pack compiles twice: the Qt-RHI preview ABI (std140 UBO, no `PLASMAZONES_KWIN`) and
  the compositor ABI (default-block uniforms, `PLASMAZONES_KWIN` defined). The validator bakes
  both. The shared header exposes the same names on both branches, so ordinary packs need no
  `#ifdef`. The exceptions run in two directions:
  - Names that exist ONLY in the preview UBO: `qt_Matrix`, `qt_Opacity`, `_appField0/1`,
    `iFlipBufferY`, `iChannelResolution[]`. Never read them in a pack. `iAudioSpectrumSize` and
    `uAudioSpectrum` reach the compositor only through `<audio.glsl>` with `"audio": true`.
  - Names the preview UBO already contains as block members but the compositor does not
    declare in the header: `iFromRect`/`iToRect` (geometry), `iIconRect` (minimize), and the
    transition scalars `iSwitchDelta`, `iStripMotion`, `iStripRect`, `iStripAxis`,
    `iOldWindowOpacity`. The transition ones are declared for you by `desktop_transition.glsl`
    / `strip_transition.glsl` / `old_content.glsl`, so include the helper instead of declaring
    them. For the geometry and minimize rects, declare them yourself inside
    `#ifdef PLASMAZONES_KWIN ... #endif` only (`window-morph/effect.frag` is the pattern):
    declaring them unguarded redeclares a UBO member and fails the preview bake, omitting them
    fails the compositor bake.
  - Names that are real uniforms on the compositor and `#define` stand-ins on the preview:
    `iWindowOpacity` (1.0), `iHasSurfaceLayer` (0), `iLayerRectInTexture` (identity),
    `iMoveVelocity`, `iMoveVelocity2`, `iMoveOffset` (all zero). They compile on both, but they
    are reserved identifiers: you cannot declare, assign, or name a local after them, and they
    cannot drive a preview. `uSurfaceLayer` is compositor-only with no stand-in; reach it only
    through `surfaceColor()`.
  - `iMoveMesh[16]` and `iMoveTrail[16]` are real on both branches and are what the settings
    preview drives for a move pack. The daemon never pushes any of the transition tail
    (`iFromRect`, `iSwitchDelta`, `iIconRect`, the mesh), so on a daemon appearance leg they
    are zero.
- Which leg the settings preview stages: an appearance-capable pack (universal, or `appliesTo`
  containing `appearance`) previews its appearance leg; otherwise the first declared class in
  the order desktop, strip, tab, geometry, move. A pack declaring `["geometry", "desktop"]`
  previews as desktop.
- Keep loops bounded and cheap; the compositor path is GPU-bound.
- Preserve the full captured surface at visible endpoints, including asymmetric shadows.
  `surfacePadRel()` assumes symmetric padding and only accounts for the layer rect. For
  explicit clipping/reveal bounds, select the rect `surfaceColor()` actually samples
  (`iLayerRectInTexture` when layered, otherwise `iAnchorRectInTexture`) and derive card
  bounds `-rect.xy / rect.zw` through `(1.0 - rect.xy) / rect.zw`. Guard degenerate spans.
  Test asymmetric insets and both layered/unlayered sampling; a centered fixture hides this bug.

Useful helpers: `legProgress()`, `legTranslation(from,to)`, `legTravelShare`, `legDirection`,
`premultiply(c)`, `surfacePadRel()`, `PZ_FINALIZE_COLOR` (applied by the scaffold, do not call).
Uniforms: `iFrame`, `iTimeDelta`, `iDate`, `iIsReversed`, `iResolution`, `iMouse`, `iAnchorSize`,
`iAnchorPosInFbo`, `iAnchorRectInTexture`, `iSurfaceScreenPos` (.xy origin, .zw screen size),
`iTextureResolution[4]`, `iFromRect`/`iToRect` (geometry), `iSwitchDelta` (desktop),
`iStripAxis`, `iStripMotion`, `iStripRect` (strip; displace through `stripAxisOffset()`, a
hardcoded `vec2(amount, 0)` smears sideways on a vertical strip), `iHasOldWindow`/`iOldWindowOpacity`
(tab), `iMoveMesh[16]`/`iMoveOffset`/`iMoveVelocity`/`iMoveTrail[16]` (move), `iIconRect`
(minimize target).

## Shared includes and licence

| include | licence | provides (selected; read the header for the full list) |
|---|---|---|
| `animation_uniforms.glsl` | LGPL | contract (auto-included; explicit include is harmless) |
| `easing.glsl` | LGPL | `easeOutQuad`, `easeInQuad`, `easeOutCubic`, `easeInOutCubic` |
| `noise.glsl` | LGPL | `hash22`, `hash12`/`niriHash`, `classicHash`, `niriNoise`, `simplex2D`, `simplex2DFractal`, `fbm`, `surfaceSeed()`, `boundaryMask`, `boundaryMaskAA`, `hexDist`, `hexLocal` |
| `anchor_remap.glsl` | LGPL | `anchorRemap(uv)` surface-UV to card-UV |
| `desktop_transition.glsl` | LGPL | `uFromDesktop`/`uToDesktop`, `switchDirection`, `getFromColor`, `getToColor`, `crossFade` |
| `old_content.glsl` | LGPL | `uOldWindow`, `oldColor`, `oldCrossFade` (tab class) |
| `strip_transition.glsl` | LGPL | `uStrip`, `uBelow`, `iStripMotion`, `iStripRect`, `iStripAxis`, `getStripColor`, `stripAxisOffset`, `stripMask`, `stripEdgeFade`, `stripUv`, `stripSampleUv`, `stripComposite`. It REDEFINES `PZ_FINALIZE_COLOR` as the re-composite over the below-strip content, which is why a strip pack must be entry-only and must sample through `getStripColor()` (the validator lints both) |
| `audio.glsl` | LGPL | spectrum sampler, needs `"audio": true` |
| `bmw_compat.glsl` | **GPL-3.0** | Burn-My-Windows shim. Including it makes the pack GPL. Do not use in a theme |

Theme packs are PlasmaZones-original: header `SPDX-License-Identifier: LGPL-2.1-or-later`. A
pack that ports copyleft upstream code (a GPL shader, whether or not through `bmw_compat`)
stays `GPL-3.0-or-later` and carries a second `SPDX-FileCopyrightText` line crediting the
upstream author; a port of permissively licensed upstream (MIT gl-transitions) may be LGPL.
The `author` field is attribution for readers, not the licence record.

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
