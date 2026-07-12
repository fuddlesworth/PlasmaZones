<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Writing custom zone shaders

PlasmaZones renders the snapping overlay (the glow behind your zones) with GLSL
fragment shaders. A shader pack is a folder with a `metadata.json` and an
`effect.frag`; the 26 built-in packs (cosmic-flow, arch-drift, …) use the exact
same API documented here, so `data/overlays/*` are the best reference once you
know the shape.

The harness does the boilerplate for you. It supplies `#version`, the shared
`<common.glsl>` header (the zone UBO, the `ZoneCtx` struct, and helpers), the
`vTexCoord` / `vFragCoord` inputs, and generates `main()` — you write only a
per-zone body and declare your parameters by name.

> This is the quick-start. For the exhaustive reference — every uniform,
> multipass buffers, wallpaper sampling, audio reactivity, and the binding
> tables — see the **Shaders** page in the project wiki.

There is a second, related system for **transition/animation** shaders
(`data/animations/*`, played when a window snaps). It shares the same
named-parameter model (`p_<id>`) and a similar entry convention
(`pTransition` / `pIn` / `pOut`); this guide focuses on zone/overlay shaders.

---

## 1. Where shaders live

- **Your shaders:** `~/.local/share/plasmazones/overlays/<id>/` — one folder per
  pack. The settings app's shader browser scans this on startup.
- **Bundled shaders:** `data/overlays/<id>/` in the repo, installed to
  `/usr/share/plasmazones/overlays/<id>/`.
- **Shared headers:** `data/overlays/shared/` (installed to
  `/usr/share/plasmazones/overlays/shared/`) — `common.glsl`, `audio.glsl`,
  `multipass.glsl`, `wallpaper.glsl`, `depth.glsl`, `textures.glsl`. The harness
  resolves `#include <common.glsl>` (and friends) from here automatically.

A pack is two files (plus an optional `preview.png`):

```
~/.local/share/plasmazones/overlays/hello-world/
├── metadata.json
└── effect.frag
```

---

## 2. Quick start

The smallest working shader: fill each zone with a gently pulsing color.

`metadata.json`:

```jsonc
{
    "id": "hello-world",
    "name": "Hello World",
    "author": "you",
    "description": "A solid color that gently pulses — the smallest working shader.",
    "parameters": [
        { "id": "color", "name": "Color", "type": "color", "default": "#3daee9" },
        { "id": "speed", "name": "Pulse Speed", "type": "float", "default": 1.0, "min": 0.0, "max": 4.0 }
    ]
}
```

`effect.frag` — the whole authored file. `#version`, the includes, the `p_*`
defines, and the dispatch loop are all generated; you write only the per-zone
body, and read your params as `p_color` / `p_speed`:

```glsl
// Fill each zone with a gently pulsing color.
vec4 pZone(ZoneCtx z) {
    // Clip to this zone's rounded rectangle — return transparent outside it,
    // or zones would paint over each other. (params.x is the corner radius.)
    vec2 center = zoneRectPos(z.rect) + zoneRectSize(z.rect) * 0.5;
    if (sdRoundedBox(z.fragCoord - center, zoneRectSize(z.rect) * 0.5, z.params.x) >= 0.0)
        return vec4(0.0);

    float pulse = 0.5 + 0.5 * sin(iTime * p_speed);   // 0..1, animated
    return vec4(p_color.rgb * pulse, p_color.a);      // premultiply-friendly
}
```

That is the entire shader — five lines of logic. Drop the folder in, open the
**Snapping → Shaders** browser, and it appears with live preview and editable
`Color` / `Pulse Speed` controls.

---

## 3. `metadata.json`

| Field            | Required | Meaning                                                           |
| ---------------- | -------- | ----------------------------------------------------------------- |
| `id`             | yes      | Stable identifier; must match the folder name.                    |
| `name`           | yes      | Display name in the browser.                                      |
| `description`    | no       | One-line summary shown in the detail dialog.                      |
| `author`         | no       | Credited in the dialog.                                           |
| `version`        | no       | Free-form version string.                                         |
| `category`       | no       | Groups the pack in the browser (e.g. `Branded`, `Ambient`).       |
| `fragmentShader` | no       | Fragment filename; defaults to `effect.frag`.                     |
| `parameters`     | no       | Array of user-tunable parameters (below).                         |

Each parameter:

| Field     | Meaning                                                                 |
| --------- | ----------------------------------------------------------------------- |
| `id`      | Used in GLSL as `p_<id>`; must be a valid GLSL identifier.               |
| `name`    | Display label.                                                          |
| `group`   | Optional; groups sliders under a collapsible header in the editor.      |
| `type`    | `float`, `int`, `color`, or `image`.                                    |
| `default` | Default value (a `#rrggbb` / `#rrggbbaa` string for `color`).           |
| `min`/`max` | Slider bounds for `float` / `int`.                                     |

---

## 4. The `p_<id>` parameter model

Every parameter you declare becomes a `p_<id>` symbol in your shader — no manual
uniform plumbing. The harness generates a `#define p_<id> <accessor>` block
(spliced after `#version`) that maps each parameter to its packed UBO lane:

| Type            | Generated accessor       |
| --------------- | ------------------------ |
| `float` / `int` | `customParams[N/4].<xyzw>` (auto-packed four-per-vec4) |
| `color`         | `customColors[N]` (a `vec4` RGBA)                      |
| `image`         | `uTexture<N>` (a `sampler2D`)                          |

Slots are assigned automatically in declaration order per pool, so you never
write a slot number — just declare params and read `p_<id>`.

---

## 5. Entry points

Write **one** of these; the harness generates the matching `main()`:

- **`vec4 pZone(ZoneCtx z)`** — called once per visible zone; return its
  contribution (transparent outside the zone). This is what most packs use.
- **`vec4 pImage(vec2 fragCoord)`** — called once for the whole frame; you do
  your own zone loop. Use this when an effect spans zones (a full-frame label
  composite, global bloom, etc.).

`ZoneCtx` (from `common.glsl`):

```glsl
struct ZoneCtx {
    int   index;         // zone i
    vec2  fragCoord;     // screen-space pixel
    vec4  rect;          // normalized x,y,w,h — use zoneRectPos/zoneRectSize
    vec4  fillColor;     // the zone's fill color
    vec4  borderColor;   // the zone's border color
    vec4  params;        // x=borderRadius, y=borderWidth, z=highlight flag
    bool  isHighlighted; // params.z > 0.5
};
```

Useful helpers (also in `common.glsl`): `zoneRectPos(rect)` / `zoneRectSize(rect)`
(rect → pixels), `sdRoundedBox(p, b, r)` (signed distance to a rounded box).
Built-in uniforms include `iResolution`, `iTime`, `iMouse`. See the wiki for the
full list.

---

## 6. Editor support & validation

### Validate before you ship

`plasmazones-shader-validate` reproduces the exact runtime assembly (entry
scaffold + generated preamble + includes) and bakes every stage through
headless glslang, so it catches compile errors offline:

```console
$ plasmazones-shader-validate ~/.local/share/plasmazones/overlays/hello-world
hello-world  (2 params, single-pass)
  metadata      OK
  effect.frag   OK
  → OK
```

(For animation packs, pass `--animation`.)

### Autocomplete for `p_<id>` (glslls)

Because the `p_<id>` defines are generated at load, a language server doesn't see
them by default. Generate a sidecar with the defines:

```console
$ plasmazones-shader-validate --emit-preamble ~/.local/share/plasmazones/overlays/hello-world
wrote …/hello-world/p_generated.glsl
```

Then `#include` it near the top of your `effect.frag` while editing:

```glsl
#include "p_generated.glsl"   // editor-only; the loader skips this at runtime
```

The sidecar pulls in `<common.glsl>` and defines every `p_<id>`, so
[glslls](https://github.com/nolanderc/glsl_analyzer) /
`glsl-language-server` resolve them. **The loader skips this include at load** —
it neither ships nor affects the compiled shader — so you can leave it in and
re-run `--emit-preamble` whenever you change parameters. (`p_generated.glsl` is
git-ignored.) Point your language server's include path at
`/usr/share/plasmazones/overlays/shared` (or `data/overlays/shared` in a checkout)
so `<common.glsl>` resolves too.

---

## 7. Reference

- **Built-in packs:** `data/overlays/*` — the best worked examples.
- **Exhaustive API:** the **Shaders** page in the project wiki — every uniform,
  multipass buffers, wallpaper/depth sampling, audio reactivity, binding tables.
- **Design rationale:** [`shader-api-usability-design.md`](shader-api-usability-design.md).
- **Tiling layouts** (the Luau analogue): [`luau-algorithm-authoring.md`](luau-algorithm-authoring.md).
