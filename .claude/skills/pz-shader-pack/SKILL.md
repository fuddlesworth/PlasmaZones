---
name: pz-shader-pack
description: "Write, change or preview ONE PlasmaZones shader pack (window animation, zone overlay, surface decoration or pointer), or change the uniform contract that packs read. Covers the pack layout and metadata per family, the SPDX licence choice, the shared helpers and their C++ mirrors, plasmazones-shader-validate and the ctest gates, deterministic PNG previews with plasmazones-shader-render, the GPU cost check, and how a pack is found and installed. For several packs that belong together use shader-theme instead. Use when: shader pack, new effect, GLSL, effect.frag, metadata.json, animation pack, overlay pack, surface pack, decoration shader, pointer pack, cursor trail, click effect, preview a shader, render a PNG, shader-validate, uniform contract, pZone, pSurface, pPointer, pTransition."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PZ Shader Pack

Read the family's contract before any GLSL: the shader-theme reference, then the shared header itself. Never write a
uniform name from memory. Schemas: `phosphor-libs/data/schemas/<family>-metadata.schema.json` (`shader-` is overlays).

| family | dir (`plasmazones/data/`) | entry | runs on | read first |
|---|---|---|---|---|
| animation | `animations/<id>/` | `pTransition(uv, t)`, or `pIn` + `pOut` | compositor + settings preview | `.claude/skills/shader-theme/references/animations.md` |
| overlay | `overlays/<id>/` | `pZone(ZoneCtx z)` or `pImage(vec2 fragCoord)` | daemon (Qt-RHI) only | `…/references/overlays-surface.md` |
| surface | `surface/<id>/` | `pSurface(vec2 uv)` | compositor + daemon/preview | `…/references/overlays-surface.md` |
| pointer | `pointer/<id>/` | `pPointer(vec2 uv)` | compositor + settings preview | no reference: `pointer/shared/pointer_uniforms.glsl`, `pointer_lib.glsl` |

## A pack

- `metadata.json`, `effect.frag`, optional vertex stage, buffer passes and textures. `id` equals the directory name.
  Required: `id`, `name`, `fragmentShader`, plus `parameters` (may be `[]`) outside overlays. No `preview.png`.
- No `#version`, in/out declarations or `main()`: the registry prepends the family lib and generates `main()`. Return
  PREMULTIPLIED colour. Read parameters as `p_<id>`.
- Param keys differ and the overlay, surface and pointer param schemas are `additionalProperties: false`, so a param
  copied across families invalidates the pack (skipped at load). Overlay params take `unit`/`slot`/`use_zone_color`/
  `wrap`/`image` and NO `description`; surface and pointer params take `description`/`group`/`step`.
- Colours are `#RRGGBB` or `#AARRGGBB` (alpha FIRST, `QColor`). A CSS `#RRGGBBAA` is misread without an error.
- Pointer: `layer` (`below` default; `above` hides the cursor and the host draws the sprite after the chain), `reach`
  or `reachParam` (logical px, the damage rect, so paint beyond it is clipped), `trailSeconds` (repaint time after the
  last event), `trailWindowSeconds`/`trailWindowParam`, `samplesTrail` (`false` when click-only), `needsCursor`. Canvas
  is the output in device px, top-down; scale params by `pointerScale()`. A GLSL `kTrailSeconds` must equal `trailSeconds`.
- Includes: the compositor resolves `<x.glsl>` only against the family `shared/` roots, so a file inside the pack takes
  `"x.glsl"`. Absolute paths and files over 1 MiB are refused. A vertex output and the fragment input it feeds need the
  same name (the compositor strips locations and pairs by name) and the same `location` (Qt-RHI pairs by location).
- User-facing prose (CLAUDE.md "User-Facing Text"): pack `name`/`description`, param `name`/`description`, preset
  names. The prose rule checks those keys but not `group`, so review that by hand.

## Licence

- `metadata.json` and curve JSON carry NO header. Each `.frag`/`.vert`/`.glsl` opens with
  `// SPDX-FileCopyrightText: 2026 fuddlesworth` and `// SPDX-License-Identifier: …`. Keep an existing file's licence.
- New PlasmaZones-original shader: `LGPL-2.1-or-later` for animation, surface and pointer; `GPL-3.0-or-later` for overlay.
- A port of GPL upstream, or anything including `animations/shared/bmw_compat.glsl`, is `GPL-3.0-or-later` with a second
  `SPDX-FileCopyrightText` for the upstream author, AND a per-file stanza in `packaging/debian/copyright` after the LGPL
  `plasmazones/data/animations/*` one (DEP-5 is last-match-wins). A permissive port kept LGPL still needs a stanza
  crediting its holder. The `dep5` conventions rule fails without it.

## Changing the contract

Every RHI block opens with the 672-byte `BaseUniforms`
(`phosphor-libs/libs/phosphor-shaders/include/PhosphorShaders/BaseUniforms.h`). Each family's tail has a C++ mirror
pinned by `static_assert` on size and offsets:

| family | GLSL (`plasmazones/data/`) | C++ mirror (`phosphor-libs/libs/`) |
|---|---|---|
| overlay | `overlays/shared/common.glsl` | `phosphor-rendering/include/PhosphorRendering/ZoneShaderCommon.h`, `ZoneUniformExtension.h`; scaffold `plasmazones/src/daemon/rendering/zoneentryscaffold.cpp` |
| animation | `animations/shared/animation_uniforms.glsl` | `phosphor-animation/include/PhosphorAnimation/AnimationUniformExtension.h`, `AnimationShaderContract.h`, `src/contract_pins.cpp` |
| surface | `surface/shared/surface_uniforms.glsl` | `phosphor-surface/include/PhosphorSurface/SurfaceShaderUniforms.h`, `SurfaceShaderContract.h`, `src/surface_contract_pins.cpp`, `src/surfaceuniformprofile.cpp` |
| pointer | `pointer/shared/pointer_uniforms.glsl` | `phosphor-pointer/include/PhosphorPointer/PointerShaderUniforms.h`, `PointerShaderContract.h`, `src/pointer_contract_pins.cpp`, `src/pointeruniformextension.cpp` |

1. Change the GLSL block and the C++ struct together, std140 (a `vec3` takes 16 bytes, an array element at least 16).
   Update each `static_assert` value; never delete one to get a build through.
2. The KWin effect sets the `#ifdef PLASMAZONES_KWIN` branch as loose uniforms: `grep -rn <name> plasmazones/kwin-effect`
   (`plasmazoneseffect/paint_shader_window.cpp`, `surface_*`, `transitions/`, `pointer/pointerdecoration*`).
3. Feed it in `plasmazones/tools/shader-render/renderer.cpp` (and `pointerdriver.cpp`), document new helpers in the
   shader-theme reference, and run the ctest subset below. The effect `.so` never reloads in a running session, so
   check the compositor side with `pz-verify-live`.

## Validate

The `debug` preset builds `build/` with tools on. The validator is in the default build; the renderer needs `BUILD_TOOLS=ON`.

```bash
cmake --build build --parallel 6 --target plasmazones-shader-validate plasmazones-shader-render
python3 scripts/validate-json-schemas.py plasmazones/data/<family>/<id>/metadata.json   # OK (1 file(s) validated)
build/bin/plasmazones-shader-validate --<animation|overlay|surface|pointer> plasmazones/data/<family>/<id>
python3 scripts/check-conventions.py plasmazones/data/<family>/<id>/*
ctest --test-dir build -j6 --output-on-failure -R 'shader_validate|_shader_|_pack_|pack_model|entry_scaffold|zone_uniform|shell_chrome|pointer'
```

- The validator has no `--help` (usage, exit 2); exit 1 is errors. It compiles each stage for Qt-RHI and both KWin
  dialects (GLSL 140, GLES 300) and links fragment to vertex. Animation, surface and pointer need `glslang` or
  `glslangValidator` on PATH and fail without it. `--emit-preamble` writes a gitignored `p_generated.glsl`: delete it.
- Outside `plasmazones/data/` the pack needs a sibling `shared/`: lay a scratchpad out as `references/validation.md`
  section 0 says. `check-conventions.py` skips paths outside the repo, so run that file's gate 4 greps by hand there.
- `SHARED_PARAM_TEXT` in `scripts/conventions_shared_text.py` lists params resolved once per chain (`roundBottomCorners`
  on surface packs). Copy that description verbatim. A new chain-resolved param joins the table.
- The regex selects 40 tests. `test_strip_pack_contract` needs `BUILD_KWIN_EFFECT`, `test_shell_chrome` `BUILD_PHOSPHOR_SHELL`.

## Preview

`plasmazones-shader-render` renders OVERLAY and POINTER packs. Its default pack and layout dirs come from the cwd's
tree (repo root or `plasmazones/`) first, then the checkout the binary was built from, so a run from a worktree
renders that worktree. `--shader` takes an id or a `metadata.json` path.

```bash
build/bin/plasmazones-shader-render --shader <id|path> --resolution 1280x720 --fps 30 --frames 61 \
    --still-highlight 1 --out <scratchpad>/r/<id>.png
build/bin/plasmazones-shader-render --pointer --shader <id|path> --resolution 1280x720 --fps 30 --frames 30 \
    --out <scratchpad>/r/<id>.png
```

It writes `<stem>_000001.png` onward. File N is frame N-1 at `iTime = (N-1)/fps`, so reruns match. Read the frames
you need with the Read tool and look at them.

| Need | Options |
|---|---|
| Zones | `--layout <id\|path>` (default `master-stack`), `--still-highlight <zone number>` (0 cycles) |
| Audio-reactive | `--audio-mode silent\|sine\|noise\|sweep` (default `sine`) |
| Pointer motion | `--pointer-heading DEG`, `--pointer-speed PX_PER_SEC`, `--pointer-still` |
| Clicks | `--press-at S` (default 0.4, negative: none), `--release-at S` (negative: held), `--pointer-button 1\|2\|3` |
| Cursor sprite | `--no-cursor-sprite` (the fallback the settings preview shows), `--cursor-size PX` |
| Size, video | `--output-size WxH` (same aspect or it stretches); `--out x.webm` or `.mp4` encodes through ffmpeg |

- No parameter flag exists: change the `default` in a scratchpad copy. Scale is pinned at 1.0, and a larger
  `--resolution` does not emulate a scaled display.
- Check edges: frame 1, both sides of press and release, each button, `--pointer-still`, `silent`/`sweep`, 1 and many zones.
- Animation and surface packs have no scripted render. Use the live previews (Settings → Appearance → Animations →
  Library, Decorations → Library) or a nested session from `pz-verify-live`, captured from the host, covering both
  animation legs or focused, unfocused and no-backdrop. Without that, report the visual check as unverified.

## GPU cost

The tree has no GPU timer, and the renderer's wall clock is startup and readback (two identical 4K runs took 5 s and
106 s), so never quote it. Review the cost and state what you counted:

- A compositor pass shades every pixel of its canvas each frame while live. Count per-pixel loop iterations times
  texture fetches; every loop needs a constant bound.
- Surface: omit `animated` unless the pack reads `iTime` or `iMouse` (a static pack does not repaint). `needsBackdrop`
  adds a scene capture per frame, `paddingParam` grows the canvas, and buffer passes want `bufferScale` below 1.
- Pointer: `reach` and `trailSeconds` as small as the effect allows. Animation: `fboExtent: "surface"` and
  `geometryGrid` (cap 128) add cost. Overlay: `multipass`, `bufferFeedback` and `depthBuffer` add passes. Then hand
  the pack to the `pz-glsl-shader-reviewer` agent.

## Ship

- The directory under `plasmazones/data/<family>/` is the registration: `plasmazones/CMakeLists.txt` installs each
  family with `install(DIRECTORY)`, so there is no list to edit. A new bundled pack gets a `CHANGELOG.md` entry.
- Registries scan `plasmazones/<family>` in the XDG data dirs, user dir first, and rescan 50 ms after a change. To try
  a pack live, `cp -r` it into the nested session's user dir (`$XDG_DATA_HOME/plasmazones/<family>/` from a shell
  that sourced the nested `env.sh`, see `pz-verify-live`) and validate that copy with the family flag
  (`references/validation.md` gate 6 explains the installed include lookup). Never copy into the host
  `~/.local/share/plasmazones/`: the user's running daemon rescans it and picks the pack up. Never `cmake --install`
  or sudo.
- Selecting it is a profile change (decoration chain for surface and pointer, the latter at the `pointer` path):
  `references/profiles.md`.
