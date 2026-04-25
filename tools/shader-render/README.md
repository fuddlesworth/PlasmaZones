# plasmazones-shader-render

Headless offscreen renderer for PlasmaZones shaders. Reproducible
shader previews without a compositor, display, or screen-capture
tool. Used by the documentation site at
[phosphor-works.github.io/plasmazones/shaders/](https://phosphor-works.github.io/plasmazones/shaders/)
and anyone else who needs a stable image of a built-in shader.

## What it does

1. Loads `data/shaders/<id>/metadata.json` plus the shader's GLSL
   files exactly the way the daemon does.
2. Loads `data/layouts/<id>.json` to get a real zone arrangement,
   so the shader has actual zones to draw rather than a single
   mock rect.
3. Boots a Qt Quick scene under `QQuickRenderControl` (offscreen,
   no window manager required) with a `ShaderEffect` filling the
   surface and the daemon's `ZoneUniformExtension` attached so the
   `zoneRects[64]` / `zoneFillColors[64]` / etc. UBO arrays match
   the runtime byte-for-byte.
4. Renders N frames, advancing `iTime` and feeding a synthetic
   audio spectrum on every frame for audio-reactive shaders.
5. Pipes raw RGBA into `ffmpeg` to encode VP9 / H.264, or saves a
   PNG sequence directly.

## Build

The tool is opt-in. Configure with `-DBUILD_TOOLS=ON`:

```bash
cmake -B build -DBUILD_TOOLS=ON
cmake --build build --target plasmazones-shader-render
```

It links `Qt6::Quick`, `PhosphorRendering`, and `PhosphorShell`.
For headless CI it works under software Vulkan
(`VK_ICD_FILENAMES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json)`),
or fall back to OpenGL with `QSG_RHI_BACKEND=opengl`.

## Run

```bash
plasmazones-shader-render \
    --shader neon-city \
    --layout master-stack \
    --resolution 1920x1080 \
    --frames 150 \
    --fps 30 \
    --out neon-city.webm
```

Common flags:

| Flag | Default | Purpose |
|------|---------|---------|
| `--shader` (required) | — | id (e.g. `neon-city`) or path to `metadata.json` |
| `--layout` | `master-stack` | id (e.g. `master-stack`) or path to a layout JSON |
| `--resolution` | `1920x1080` | render target — what the shader sees as `iResolution` |
| `--output-size` | same as `--resolution` | downscale the encoded output |
| `--frames` | `150` | total frames |
| `--fps` | `30` | frame rate (drives `iTime` advancement) |
| `--out` | `<id>.webm` | output path; extension picks the format |
| `--audio-mode` | `sine` | one of `silent`, `sine`, `noise`, `sweep` |
| `--shader-dir` | `data/shaders/` then `/usr/share/...` | where to find shader bundles |
| `--layout-dir` | `data/layouts/` then `/usr/share/...` | where to find layout JSONs |

Output formats are picked by extension:

- `.webm` — VP9 via `ffmpeg` (`-c:v libvpx-vp9 -b:v 600k -crf 34`)
- `.mp4` — H.264 via `ffmpeg` (`-c:v libx264 -crf 23`)
- `.png` — numbered sequence (`out_0001.png`, `out_0002.png`, ...)

`ffmpeg` must be on `PATH` for the video formats.

## Audio modes

Audio-reactive shaders (everything tagged `audio-reactive` in the
shader gallery) sample `uAudioSpectrum`. To get representative
output, the renderer feeds a synthetic spectrum:

- `silent` — all zeros. Audio shaders render in their idle state.
  Use only when you want to see the dormant geometry.
- `sine` — every bar follows a slow phased sine. Default — looks
  like steady mid-tempo music.
- `noise` — deterministic per-frame noise. Looks like a flat-
  spectrum noise bed.
- `sweep` — a peak walks bass → treble → bass on a 4-second loop.
  Useful to verify that low / mid / high buckets light up the
  expected visual elements.

The bar count matches the runtime CAVA default (256).

## Batch wrapper

A shell loop over the shaders.json list is the typical site-update
flow:

```bash
for id in $(jq -r '.[].id' src/data/plasmazones/shaders.json); do
    plasmazones-shader-render \
        --shader "$id" \
        --resolution 960x540 \
        --output-size 480x270 \
        --frames 150 \
        --out public/plasmazones/shaders/$id.webm
done
```

Run on a developer machine once per release; commit the resulting
`.webm` files to the docs repo. Or run it as a CI step against
PRs that touch `data/shaders/`.

## Status / known limitations

Working today:

- CLI argument parsing and input resolution
- Metadata + layout loaders (covers the full schema the daemon
  reads)
- Audio mock generators (silent / sine / noise / sweep)
- Frame-grab and PNG / VP9 / H.264 output
- ZoneUniformExtension attached so zone arrays in the UBO match
  the runtime exactly

Known gaps to verify on first run:

- **Multipass + buffer-feedback shaders** (neon-city, voxel-
  terrain, paper, ...) — the renderer uses
  `QQuickRenderControl::polishAndSync() + render()` which works
  for simple `ShaderEffect`s but may need explicit
  `beginFrame()`/`endFrame()` (Qt 6.6+) for clean ping-pong
  between the buffer textures. See `renderer.cpp` TODO.
- **Depth-buffered shaders** — `useDepthBuffer = true` flag is
  forwarded; the corresponding offscreen depth attachment is
  set up by `ShaderNodeRhi`, but verify visually against a real
  daemon render before trusting CI output.
- **Layout zone defaults** — the loader pulls per-zone fill /
  border colors from the layout JSON if present, otherwise
  cycles through the Phosphor brand palette. Some layout JSONs
  ship without color overrides; if a shader's preview reads
  unexpectedly flat, check the layout file.

## Tech debt

`tools/shader-render/CMakeLists.txt` includes
`src/daemon/rendering/` directly to reuse the daemon's
`ZoneUniformExtension` header. The right long-term fix is to
extract `ZoneUniformExtension` (and the `ZoneShaderUniforms` UBO
struct) into a small reusable library — probably
`libs/phosphor-shell/` since the extension is the canonical
example of `IUniformExtension`. Until then, this header coupling
is the trade-off for not duplicating the UBO layout, which would
be a worse failure mode (silent visual drift between docs and
runtime).
