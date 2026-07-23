<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Design: Shader authoring API usability (v3.1)

## Status

**Tier 1 implemented** (T1.1–T1.5; all bundled animation + zone packs migrated,
and the offline validator now covers overlay, animation and surface packs with
a CI gate each). From Tiers 2–3, T2.2 (`--emit-preamble` sidecar), T2.3
(authoring guide) and T3.1 (live preview in the shader browser) have also
landed; T2.1 (hello-world template) and T3.2 (in-app compile banner) remain
proposed. This document covers the full scope across all tiers; the sections
for the two unlanded items are the forward design.

Pack counts throughout this document reflect the v3.1 tree at design time (53
animation + 26 zone packs); both trees have since grown (81 animation + 27 zone
packs as of v3.3) and every pack added after the T1.5 migration follows the
migrated conventions from day one.

## Motivation

v3.1 focuses on the usability of *user-created* things. The autotile algorithm
authoring experience was reworked into something genuinely pleasant: named typed
parameters, a real standard library, pre-load type checking, editor autocomplete,
and a graduated onboarding path (see
[`luau-algorithm-authoring.md`](luau-algorithm-authoring.md)).

The shader system did **not** get the same treatment. Its *capabilities* are
excellent — audio reactivity, multipass buffer chains, wallpaper sampling,
zone-aware SDF helpers, label compositing — but the *authoring ergonomics* are
roughly where autotile was before its rework. A motivated GPU programmer can write
a shader; nearly everyone else bounces off.

There are in fact **three** shader systems with the same problems: overlay/zone
shaders (`data/overlays/*`, 26 packs at design time), animation/transition
shaders (`data/animations/*`, 53 packs at design time) and surface/decoration
shaders (`data/surface/*`, added since). Each has its own authoring model,
validator mode and CI gate — `src/shadervalidate/main.cpp` is the canonical
list. The body of this document develops the design
against the zone system, then the
[Animation / transition shaders](#the-second-system--animation--transition-shaders)
section folds in the second — which adds a cross-runtime constraint and a direction
(in/out) dimension that reshape parts of Tier 1.

This is not a capability gap. It is an ergonomics, tooling, validation, and
onboarding gap. The autotile system is the north star, and the goal of this work
is **parity with it along five axes**:

| Axis | Autotile (good) | Shaders (today) |
|---|---|---|
| Parameter access | `ctx.custom.focusBoost` (named, typed) | `customParams[2].x` (hand-computed slot arithmetic) |
| Entry point | `tile(ctx)` — receive context, return zones | hand-written `main()` + dispatch loop, re-implemented per pack |
| Pre-load validation | `luau-analyze` catches errors before load | errors surface only at GPU runtime, unmapped |
| Editor tooling | `.luaurc` + `pluau.d.luau` → autocomplete | nothing |
| Onboarding | 10-line working quick-start | ~89-line frag + vert + JSON minimum, no "hello world" |

## Non-goals

- **Visual / node-graph shader editor.** A node graph that lets non-programmers
  author effects without touching GLSL is a separate product serving a different
  audience, and a multiple-times-larger effort. It is explicitly out of scope for
  this round; the work below targets people willing to write GLSL but who are
  currently fighting the toolchain instead of the shading.
- **Replacing GLSL with a DSL.** Unlike tiling (which maps cleanly onto a small
  Lua surface), shading is intrinsically GLSL. We make GLSL authoring ergonomic;
  we do not abstract it away.
- **Changing the runtime uniform layout / UBO.** The `customParams[8]` /
  `customColors[16]` UBO, the `BaseUniforms` block, and the animation system's
  `AnimationUniforms` + `AnimationUniformExtension` std140 layout (pinned by the
  `BaseUniforms.h` offset asserts) all stay exactly as they are. Every change below
  is additive and backward compatible with all bundled packs (26 zone + 53
  animation).

---

## The current authoring surface (grounded)

A user shader pack is a directory under `~/.local/share/plasmazones/overlays/<id>/`
containing `metadata.json`, `effect.frag`, optional buffer passes
(`pass0.frag`…), an optional `zone.vert`, and an optional `preview.png`. Bundled
packs live in `data/overlays/` (installed to `…/share/plasmazones/overlays/`).

> **Three shader systems.** This is one of *three* shader registries (the
> third, surface/decoration, arrived after this document was written and is
> covered by `--surface` and its own CI gate). The other —
> **animation / transition shaders** (`data/animations/*`, 53 bundled packs,
> driven by `AnimationShaderRegistry`) — has the same authoring problems *plus* a
> direction (in/out) dimension *and* runs on two runtimes. Everything from here to
> the end of Tier 3 is written for the overlay/zone system; the
> [Animation / transition shaders](#the-second-system--animation--transition-shaders)
> section folds that second system in and is required reading before
> implementation. Several Tier-1 mechanisms have to be built cross-runtime from
> the start, not retrofitted.

Key code paths this design touches:

- **Metadata parse + slot mapping:** `libs/phosphor-shaders/src/shaderregistry.cpp`
  - `parseShaderMetadata()` reads every metadata field, including each
    parameter's explicit `slot`.
  - `ParameterInfo::uniformName()` is the single point that maps
    `(type, slot)` → uniform key (`customParams3_x`, `customColor1`, `uTexture0`).
  - `translateParamsToUniforms()` turns stored param values into the
    uniform-key map handed to the renderer.
- **Include expansion:** `libs/phosphor-shaders/src/shaderincluderesolver.cpp`
  - `expandIncludes()` recursively inlines `#include <common.glsl>` etc.
    from the search paths. *(Originally concatenated without `#line` directives;
    T1.3 added the `#line` emission that maps diagnostics to the author's file.)*
- **Compilation (headless, CPU):** `libs/phosphor-rendering/src/shadercompiler.cpp`
  - `compileFromFile(path, includePaths)` → `loadAndExpand()` →
    `compile()` → `QShaderBaker` (glslang). Returns `{ QShader, success, error }`.
    No GPU context required — glslang runs on CPU.
- **Compile-result reporting:** `ShaderRegistry::reportShaderBakeFinished()`
  (`shaderregistry.cpp`) already emits
  `shaderCompilationFinished(id, success, error)`.
- **Install:** `src/settings/services/shaderpackinstaller.{h,cpp}` — validated, size-capped,
  symlink-rejecting, atomic-rename install of a dropped pack.

The pieces needed for every improvement below **already exist**; the work is
wiring and generation, not new subsystems.

---

## The diagnosis, in priority order

### D1 — Slot arithmetic is the worst friction

*(Pre-work state — addressed by Tier 1 / T2.2 / T3.1; kept for the record.)*

An author declares a parameter with an explicit integer `slot`, must avoid
colliding with other params, and then reads it in GLSL by hand-decoding the slot
into a vec4 lane:

> slot 8 → `customParams[2].x`, slot 5 → `customParams[1].y`, …

Because this is miserable and error-prone, real shaders hand-write accessor
boilerplate. From `data/overlays/nexus-cascade/effect.frag`:

```glsl
float getChromaStrength() { return customParams[3].x >= 0.0 ? customParams[3].x : 4.0; }
float getFillOpacity()    { return customParams[3].y >= 0.0 ? customParams[3].y : 0.92; }
```

That is the author manually reproducing what `ctx.custom.chromaStrength` gives an
autotile author for free. Worse, the uniform-key name (`customParams3_x`) is
off-by-one from the GLSL index the author must write (`customParams[2].x`), which
trips people repeatedly.

### D2 — No pre-load validation, and runtime errors are unmapped

*(Pre-work state — addressed by T1.2/T1.3.)* A malformed shader was only
discovered when `QShaderBaker` ran at render time. There was no `luau-analyze`
equivalent. And because `expandIncludes()` concatenated includes without `#line`
directives, glslang's reported line numbers pointed into the *expanded* blob, not
the author's `effect.frag` — so even when an error did surface it was hard to
locate.

### D3 — No editor tooling

*(Pre-work state — addressed by Tier 1 / T2.2 / T3.1; kept for the record.)*

There is no `.glslrc` analogue, no documented include path for an LSP, no shipped
headers location an editor can resolve `#include <common.glsl>` against. Authors
write GLSL blind.

### D4 — Onboarding cliff

The smallest working example is ~89 lines of fragment shader plus a vertex shader
plus metadata JSON. There is no bundled "hello world", and no "Create shader"
action in the settings app (the algorithms page already has create/duplicate).
The wiki (`PlasmaZones.wiki/Shaders.md`, 597 lines) is a dense reference that
assumes GPU expertise, with no separate quick-start.

### D5 — In-app experience is split

*(Pre-work state — addressed by Tier 1 / T2.2 / T3.1; kept for the record.)*

Live preview + audio-reactive testing exists **only** in the zone editor's
`ShaderSettingsDialog.qml`. The settings app's shader browser
(`ShaderBrowserDetailDialog.qml`) is read-only — you cannot test parameters
without first assigning a shader to an event in a different app. The renderer
(`ZoneShaderRenderer.qml`) is already shared, so this is a wiring gap, not a
missing capability.

### D6 — Every pack re-implements the dispatch loop

*(Pre-work state — addressed by Tier 1 / T2.2 / T3.1; kept for the record.)*

There is no entry-point convention. Each shader writes its own `main()`, which
across all 26 packs is near-identical boilerplate: the `#version`/in/out preamble,
a `zoneCount == 0` guard, audio setup, a `for (i < zoneCount && i < 64)` loop with
a degenerate-rect `continue`, a `blendOver` accumulate, an optional label
composite, and a final `clampFragColor`. The packs *already* factor the real work
into a per-zone function (e.g. `cosmic-flow`'s `renderCosmicZone(fragCoord, rect,
fillColor, borderColor, params, isHighlighted, …)`) — but the function signature
is ad hoc per pack and the dispatch loop is copy-pasted every time. This is the
direct analogue of an autotile author having to hand-write the window-placement
loop instead of just implementing `tile(ctx)`.

---

## Proposed work, tiered

### Tier 1 — Named parameters, entry point & offline validation (the parity core)

These items remove the worst friction (D1, slot arithmetic), the worst boilerplate
(D6, the hand-written dispatch loop), and the worst feedback loop (D2, no pre-load
validation). They are the foundation everything else builds on, and they compose:
T1.1 and T1.4 are both "the harness wraps your code", and T1.2/T1.3 give that code
a real error path.

#### T1.1 — Generated named-param preamble + automatic slot assignment

**Goal:** the author writes the parameter's `id` directly in GLSL; `slot` becomes
an optional implementation detail.

**Mechanism.** `ShaderRegistry` gains a method that, given a `ShaderInfo`,
synthesizes a `#define` preamble from the parameter list:

```glsl
// ---- generated from metadata.json (do not edit) ----
#define p_speed          customParams[0].x
#define p_chromaStrength customParams[2].x
#define p_fillColor      customColors[0]
#define p_logoTex        uTexture0
// ----------------------------------------------------
```

- Naming: a `p_<id>` prefix avoids collisions with user locals and makes
  generated identifiers obvious. (Bikeshed in review — alternative is bare `<id>`,
  but a prefix is safer.)
- The mapping is derived from the **same** `(type, slot)` logic that
  `ParameterInfo::uniformName()` already owns — extended to emit the GLSL
  *array-index* form (`customParams[vec].lane`, `customColors[slot]`,
  `uTexture<slot>`) rather than the uniform-key form. Factor the shared
  index math out of `uniformName()` so the two stay in lockstep.

**Injection point.** The preamble must appear *after* the UBO struct is declared
(so the `customParams[]`/`customColors[]` symbols exist) and *before* the author's
body. The clean seam is the compile path: after `ShaderCompiler::loadAndExpand()`
inlines `common.glsl` (which declares the UBO), the caller prepends the preamble
to the author's portion before calling `compile()`. Concretely, the renderer-side
call sites that invoke `compileFromFile` (`zoneshadernoderhi`, `ShaderEffect`)
ask the registry for `generatedParamPreamble(shaderId)` and splice it in. Because
`#define` is pure text substitution, exact placement only needs to precede *use*;
inserting immediately after the `common.glsl` include block is simplest and
robust.

> Implementation note: `loadAndExpand()` takes a path, not a shaderId, so the
> preamble is composed at the call site that *has* the shaderId, not inside the
> compiler. Keep `ShaderCompiler` shader-agnostic; the registry owns generation.

**Automatic slot assignment.** Once the author no longer writes the slot into
GLSL, `parseShaderMetadata()` can assign slots itself when `slot` is omitted:
pack `float`/`int`/`bool` params into lanes 0..31 in declaration order, `color`
params into 0..15, `image` params into 0..3. Explicit `slot` values are still
honored (so the 26 existing packs are untouched), and a slot collision becomes a
load-time warning surfaced by the validator (T1.2).

**Backward compatibility.** Fully additive. Existing shaders that read
`customParams[2].x` directly keep working; the new `p_*` defines are extra
symbols. Existing explicit `slot` declarations keep working.

#### What this actually looks like — `cosmic-flow` before / after

This is the real bundled `data/overlays/cosmic-flow/` pack (25 parameters),
trimmed to the parts that change.

**Before — metadata.** Every parameter carries a hand-assigned `slot`, and the
author has to keep them collision-free across both the float pool *and* the
color pool:

```jsonc
{ "id": "speed",        "type": "float", "slot": 0,  "default": 0.1, "min": 0.01, "max": 0.5 },
{ "id": "flowSpeed",    "type": "float", "slot": 1,  "default": 0.3, "min": 0.05, "max": 1.0 },
// … slots 2 … 19 …
{ "id": "showLabels",   "type": "bool",  "slot": 21, "default": true },          // ← slot 20 skipped by hand
{ "id": "paletteColorA","type": "color", "slot": 0,  "default": "#808080" }       // ← separate color pool, also hand-numbered
```

**Before — fragment shader.** The author hand-decodes every slot into a vec4
lane *and* re-declares every default a second time with a `>= 0.0` guard (the
defaults now live in two places that can silently drift):

```glsl
float speed      = customParams[0].x >= 0.0 ? customParams[0].x : 0.1;
float flowSpeed  = customParams[0].y >= 0.0 ? customParams[0].y : 0.3;
float noiseScale = customParams[0].z >= 0.0 ? customParams[0].z : 3.0;
int   octaves    = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);
float colorShift = customParams[1].x >= 0.0 ? customParams[1].x : 0.0;
// … 16 more lines of this …
float fbmRot     = customParams[4].w >= 0.0 ? customParams[4].w : 0.5;
vec3  palA = customColors[0].rgb;   // which metadata color was [0] again?
// …
if (customParams[5].y > 0.5)        // "is showLabels on?" — decoded by hand from slot 21
    color = compositeCosmicLabels(...);
```

**After — metadata.** Slots are gone; the registry packs them by declaration
order. Defaults stay here, as the single source of truth:

```jsonc
{ "id": "speed",        "type": "float", "default": 0.1, "min": 0.01, "max": 0.5 },
{ "id": "flowSpeed",    "type": "float", "default": 0.3, "min": 0.05, "max": 1.0 },
// …
{ "id": "showLabels",   "type": "bool",  "default": true },
{ "id": "paletteColorA","type": "color", "default": "#808080" }
```

**After — fragment shader.** The generated preamble (injected automatically;
the author never writes or sees it) is what makes the body read by name. The
runtime already fills defaults via `translateParamsToUniforms()`, so the `>= 0.0`
guards disappear too:

```glsl
// ---- generated from metadata.json, injected after <common.glsl> (do not edit) ----
#define p_speed        customParams[0].x
#define p_flowSpeed    customParams[0].y
#define p_noiseScale   customParams[0].z
#define p_octaves      customParams[0].w
// … one #define per parameter …
#define p_showLabels   customParams[5].y
#define p_paletteColorA customColors[0]
// -----------------------------------------------------------------------------------

// what the author actually writes now:
float speed     = p_speed;
float flowSpeed = p_flowSpeed;
int   octaves   = int(p_octaves);
vec3  palA      = p_paletteColorA.rgb;
// …
if (p_showLabels > 0.5)
    color = compositeCosmicLabels(...);
```

The slot-decode block (≈20 lines), the duplicated defaults, and the
manual `slot 20 skipped` / `slot 5.y == showLabels` bookkeeping all evaporate —
and the off-by-one trap (metadata `slot: 8` ↔ GLSL `customParams[2].x`) is gone
because the author never touches the index.

#### T1.2 — Offline validation CLI (`plasmazones-shader-validate`)

**Goal:** a `luau-analyze` equivalent — catch compile errors before the daemon
loads the shader, and let CI gate the bundled set.

**Mechanism.** A small CLI target (under `src/`, GPL-3.0-or-later) linking
`PhosphorShaders` + `PhosphorRendering`. Given a pack directory it:

1. Parses `metadata.json` via the registry's parser (reusing
   `parseShaderMetadata` so the tool and daemon agree on what a pack *is*).
2. Composes the generated preamble (T1.1) for each stage.
3. Calls `ShaderCompiler::compileFromFile()` for `effect.frag`, every declared
   buffer pass, and the vertex shader, with the shared include paths.
4. Prints per-stage `OK` / `error` with the glslang diagnostic, plus metadata
   lints (unknown param `type`, slot collision, multipass declared but buffers
   missing, `bufferScale` out of range, etc.).

Because `QShaderBaker` is headless CPU glslang, this needs **no compositor, no GPU,
no Wayland session** — it runs in CI exactly like `luau-analyze` does for
algorithms. Add a CTest entry that runs it over `data/overlays/*` so a broken
bundled pack fails the build.

Exit non-zero on any failure so it composes in scripts and pre-commit.

Sample output:

```
$ plasmazones-shader-validate ~/.local/share/plasmazones/overlays/my-shader
my-shader  (3 params, single-pass)
  metadata     OK
  effect.frag  ERROR
    effect.frag:42: 'p_spede' : undeclared identifier
    (did you mean 'p_speed'? — declared in metadata.json)
  → 1 error
```

(The `effect.frag:42` line number is correct because of T1.3; the "declared in
metadata" hint comes from cross-referencing the generated preamble's symbols.)

#### T1.3 — `#line` directives in the include resolver

**Goal:** glslang errors point at the author's file and line, not the expanded
blob — turning D2's "unmapped runtime error" into an actionable message.

**Mechanism.** In `expandIncludesRecursive()` (`shaderincluderesolver.cpp`),
emit `#line <n> <source-index>` (or filename via the GL_GOOGLE_cpp_style_line
extension if the bake targets accept it; otherwise integer source indices plus a
printed index→path legend) at each include boundary and on return to the parent.
This is a self-contained change to one function and immediately improves both the
runtime toast (via the existing `shaderCompilationFinished` error string) and the
T1.2 CLI output.

#### T1.4 — Entry-point convention (harness-generated `main()`)

**Goal:** the author implements a named function that receives a context and
returns a color — the shading analogue of `tile(ctx)` — and the harness generates
the `main()` plumbing around it. This formalizes the per-zone-function convention
the 26 packs already follow by hand (D6).

**Two entry shapes** (a single per-zone signature is too narrow — see the cost
below), plus the existing `main()` as an escape hatch:

1. **`vec4 pZone(ZoneCtx z)`** — the harness generates the full dispatch:
   the `zoneCount == 0` guard, the `for (i < zoneCount && i < 64)` loop with the
   degenerate-rect skip, the `blendOver` accumulate, the optional label composite,
   and `clampFragColor`. `ZoneCtx` is a generated/shared struct carrying the
   per-zone data the packs pass by hand today:

   ```glsl
   struct ZoneCtx {
       int   index;          // zone i
       vec2  fragCoord;      // screen-space pixel (so continuous-field shaders still work)
       vec4  rect;           // zoneRects[i]
       vec4  fillColor;      // zoneFillColors[i]
       vec4  borderColor;    // zoneBorderColors[i]
       vec4  params;         // zoneParams[i] (x=borderRadius, y=borderWidth, …)
       bool  isHighlighted;
   };
   ```

   Globals (`iTime`, `iResolution`, the audio helpers, the full `zoneRects[]`
   arrays) stay readable inside `pZone`, so continuous-field shaders (`cosmic-flow`)
   and cross-zone effects (`plasma-sigil`'s energy bridges, `endeavouros-drift`'s
   proximity network) are still expressible.

2. **`vec4 pImage(vec2 fragCoord)`** — full-frame entry; the harness generates a
   trivial `main()` (version, in/out, `clampFragColor`). This is what multipass
   **buffer passes already are**, so a multipass author writes one `pImage` per
   pass with no ceremony, and full-screen / wallpaper effects use it directly.

3. **`void main()`** — retained verbatim. If the author defines `main()`, the
   harness uses it untouched. The 26 existing packs are unaffected, and power users
   who need full control of the loop keep it.

**Dispatch rule.** Exactly the additive pattern T1.1 uses: the registry inspects
the (include-expanded) source for a `main()` definition; if present, compile as-is;
otherwise wrap whichever of `pZone` / `pImage` is defined. The generated wrapper,
the `#version`/in/out preamble, the `common.glsl` include, and the T1.1 param
preamble are all emitted by the same generation step, so the author's file can be
just the function bodies.

**Why it's worth it.** Same win-class as named params: it deletes plumbing that is
identical across all 26 packs, lets compositing rules (premultiplied alpha, clamp,
a future per-zone field) be fixed *centrally* instead of in every shader, and
removes a category of bugs (forgetting `i < 64`, the degenerate-rect skip, or the
final clamp). The hello-world collapses to a single function body — true
`tile(ctx)` parity.

**The cost (and mitigation).** When the author writes only the entry function,
their file is no longer standalone-valid GLSL — the harness supplies `#version`,
in/out, and `main()`. This is the same trade Shadertoy makes (`mainImage`), and it
is what makes it approachable, but it is in tension with T2.2 editor tooling. The
mitigation is the same `--emit-preamble`/`--emit-scaffold` mechanism: the validator
writes the generated scaffold to a sidecar so an LSP still sees a complete
translation unit. Because this is more opinionated than the purely-additive T1.1,
it warrants explicit sign-off before implementation.

**`cosmic-flow` carried through.** The pack already has
`renderCosmicZone(fragCoord, rect, fillColor, borderColor, params, isHighlighted, …)`
and a `main()` that loops and calls it. Under T1.4 that becomes:

```glsl
// before: ~25-line main() — guard, audio setup, for-loop, blendOver, label composite, clamp
void main() {
    if (zoneCount == 0) { fragColor = vec4(0.0); return; }
    bool hasAudio = iAudioSpectrumSize > 0;
    float bass = getBassSoft(); /* …mids, treble, overall… */
    vec4 color = vec4(0.0);
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        color = blendOver(color, renderCosmicZone(vFragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5, bass, mids, treble, overall, hasAudio));
    }
    if (p_showLabels > 0.5) color = compositeCosmicLabels(color, vFragCoord, bass, treble, hasAudio);
    fragColor = clampFragColor(color);
}

// after: the author writes only the per-zone body; the loop/guard/blend/clamp are generated
vec4 pZone(ZoneCtx z) {
    // audio helpers are still global; z carries the per-zone data
    return renderCosmicZone(z.fragCoord, z.rect, z.fillColor, z.borderColor,
                            z.params, z.isHighlighted, getBassSoft(), getMidsSoft(),
                            getTrebleSoft(), getOverallSoft(), iAudioSpectrumSize > 0);
}
```

(The label composite is the one piece that's genuinely whole-frame, not per-zone —
so a pack that needs it either keeps `main()` or the harness exposes an optional
`vec4 pComposite(vec4 frame)` post-hook. Worth deciding in review; the per-zone +
full-frame + `main()` triad covers everything else cleanly.)

---

### Tier 2 — Onboarding & editor tooling (D3, D4)

#### T2.1 — Bundled `hello-world` pack + "Create shader" action

- Add a minimal, heavily-commented `data/overlays/hello-world/` pack: one
  fragment shader, two or three named params, no multipass — the shader analogue
  of the 10-line `my-columns.luau` quick-start. With T1.1 its body reads
  `p_speed`, `p_color` directly. The entire pack is two short files:

  `metadata.json`:

  ```jsonc
  {
      "name": "Hello World",
      "author": "you",
      "description": "A solid color that gently pulses — the smallest working shader.",
      "parameters": [
          { "id": "color", "name": "Color", "type": "color", "default": "#3daee9" },
          { "id": "speed", "name": "Pulse Speed", "type": "float", "default": 1.0, "min": 0.0, "max": 4.0 }
      ]
  }
  ```

  `effect.frag` — the whole authored file. With T1.4 the author writes only the
  per-zone body; the `p_*` defines, the includes, and `main()` are generated:

  ```glsl
  // Fill each zone with a gently pulsing color. (#version, includes, and the
  // dispatch loop are generated; p_color / p_speed come from metadata.json.)
  vec4 pZone(ZoneCtx z) {
      float halfRadius = z.params.x;
      vec2  center = zoneRectPos(z.rect) + zoneRectSize(z.rect) * 0.5;
      if (sdRoundedBox(z.fragCoord - center, zoneRectSize(z.rect) * 0.5, halfRadius) >= 0.0)
          return vec4(0.0);                              // outside this zone
      float pulse = 0.5 + 0.5 * sin(iTime * p_speed);   // 0..1, animated
      return vec4(p_color.rgb * pulse, p_color.a);
  }
  ```

  That is the entire shader — five lines of actual logic. It is the moral
  equivalent of the `my-columns.luau` quick-start, and the thing a "Create shader"
  action stamps out. (A power user who wants the `main()` loop, or a full-frame
  `pImage`, can write those instead — see T1.4.)
- Add `createFromTemplate(name)` to the shader page controller
  (`src/settings/pages/animationspagecontroller_shaders.cpp`), copying the template into
  the user dir via the existing `ShaderPackInstaller` path — mirroring the
  algorithms page's create/duplicate affordance. Wire a "Create shader…" /
  "Duplicate" button into `ShaderBrowserPage.qml`.

#### T2.2 — Editor support (the `.luaurc` analogue) — LANDED

- Ship the shared GLSL headers at a documented, stable path (they already install
  under `…/share/plasmazones/overlays/shared/`).
- Document the include search path and a recommended editor config so
  `glsl-language-server` / `glslls` resolves `#include <common.glsl>` and friends.
- For the generated `p_*` defines: the validator (T1.2) gains a `--emit-preamble`
  mode that writes the generated `#define` block to a sidecar
  (e.g. `p_generated.glsl`) the author can `#include "…"` while editing for
  autocomplete, stripped/ignored at load. (This is the pragmatic GLSL-side stand-in
  for `pluau.d.luau`; GLSL LSP tooling is weaker than luau-lsp, so we lean on a
  generated include rather than a type stub.)

#### T2.3 — Documentation split — LANDED (`docs/architecture/shader-authoring.md`)

Restructure shader docs to mirror `luau-algorithm-authoring.md`:

- A new `docs/architecture/shader-authoring.md` quick-start: "where shaders live",
  a complete ~15-line hello-world (named params), the metadata table, and the
  `p_*` access model — the things a first-time author needs, in order.
- Keep `PlasmaZones.wiki/Shaders.md` as the exhaustive reference (uniforms,
  multipass, wallpaper, audio, binding tables) and link to it from the quick-start.

---

### Tier 3 — Unify the in-app experience (D5)

#### T3.1 — Live preview + editable params in the settings app — LANDED

Promote the editor's `ZoneShaderRenderer`-based live preview into
`ShaderBrowserDetailDialog.qml` so a user can scrub parameters and watch the
effect without leaving the settings app or assigning it to an event first. The
renderer and parameter editor (`ParameterEditor.qml` /
`ParameterRow.qml`) are already shared components — this is wiring plus a
transient (non-persisted) param session in the detail dialog.

#### T3.2 — In-app compile feedback (and, later, a source pane)

- Surface `ShaderRegistry::shaderCompilationFinished(id, success, error)` as an
  inline error banner / toast in `ShaderSettingsDialog.qml` and the browser, so a
  failed hot-reload is visible in-app rather than only in the journal. (Small;
  the signal already carries the mapped error once T1.3 lands.)
- *Stretch:* an editable GLSL source pane in the editor dialog that recompiles on
  save and shows the error inline — a "Shadertoy-in-app" loop. The 60fps live
  preview infrastructure already exists; this adds an editor surface on top. Larger
  lift; sequence last.

---

## The second system — animation / transition shaders

PlasmaZones has two shader registries (per `AnimationShaderContract.h`):

| | Overlay / zone shaders | Animation / transition shaders |
|---|---|---|
| Registry | `PhosphorShaders::ShaderRegistry` | `AnimationShaderRegistry` |
| Source | `data/overlays/*` | `data/animations/*` (53 packs) |
| Lifetime | long-lived ambient effect | short-lived 0→1 transition |
| `iTime` | wrapped wall-clock seconds | per-leg progress in `[0,1]` |
| Runtimes | daemon RHI only | **daemon RHI *and* kwin-effect (classic GL)** |
| Slots today | explicit `slot:` in metadata | **already auto-assigned by declaration order** |

The authoring pain (D1–D4, D6) is identical: T1.1–T1.3 and T2.x apply directly, and
T1.4's *mechanism* (harness-generated `main()` around a named entry function)
applies — but with animation-specific entry shapes, defined in A3/T1.5 below rather
than `pZone`/`pImage`. Three things make the animation system different, and two of
them change the *shape* of the Tier-1 work — they must be designed in from the start,
not bolted on.

### A1 — Animation shaders are the ideal first adopter of named params

They already do what T1.1 proposes. `bounce/effect.frag` declares one parameter
with no `slot:` and hand-writes the mapping the preamble would generate:

```glsl
// metadata.json declaration order → customParams[0] sub-slots
#define bounces customParams[0].x
```

`AnimationShaderRegistry::translateAnimationParams()` already packs params into
`customParams[N]`/`customColors[N]` by declaration order. So for animations T1.1 is
*purely* "generate the `#define` block the author writes by hand today" — there is
no explicit-slot legacy to preserve, and auto-slotting is already the runtime
reality. **Start T1.1 here**; it's lower-risk than the zone-shader side.

### A2 — The generation must be cross-runtime (the real wrench)

The same `effect.frag` runs on **both** runtimes:

- **Daemon** — Qt RHI via `ShaderEffect` → `ShaderCompiler` (the path T1.1–T1.3
  already target).
- **Compositor** — `kwin-effect` via `KWin::ShaderManager::generateCustomShader`,
  which prepends `#define PLASMAZONES_KWIN` after `#version` and uses classic
  default-block uniforms (no UBO).

Consequences for Tier 1:

- **T1.1 / T1.4 generation cannot live only in the daemon `ShaderCompiler` call
  sites.** The generated param preamble and any generated `main()`/entry wrapper
  must be produced by a shared, runtime-agnostic component (the registry is the
  natural owner) and injected on **both** the RHI path *and* the kwin-effect
  source-assembly path (`shader_transitions.cpp`), after that path's `#version` /
  `PLASMAZONES_KWIN` prepend. Treat "emit the generated GLSL" and "splice it into a
  compile" as two steps so both runtimes share step one.
- **T1.2 validation must bake both branches.** A valid animation shader is one that
  compiles **with and without** `PLASMAZONES_KWIN`. The validator should compile
  both; there is already `tests/unit/ui/shaders/test_animation_shader_bake.cpp` baking every
  daemon-eligible animation shader's vertex stage through `qsb` (fragment / UBO
  coverage lives in `test_animation_shader_preamble_bake`) — extend those
  harnesses rather than duplicating them, and have the CLI share their code.
- **Multipass / wallpaper are daemon-only** on this path (the kwin
  `OffscreenEffect` is single-pass). The validator should *warn*, not error, when an
  animation shader uses daemon-only features — they degrade to single-pass on the
  compositor by design. Audio is NOT in that set: animation packs opt in via
  `data/animations/shared/audio.glsl` plus the `audio` metadata flag, and the
  kwin-effect feeds the spectrum from its own CAVA provider, so audio-reactive
  transitions run on both runtimes.

### A3 — Direction (the in/out toggle) — `T1.5`

This is the dimension zone shaders don't have. A transition plays **forward (in)**
or **reverse (out)** — `window.open`/`snapIn`/`show` vs `window.close`/`snapOut`/
`hide`. Today (`AnimationShaderContract::kIIsReversed`) the runtime exposes two
mechanisms and the author wires them by hand:

1. The runtime **flips `iTime`** on reverse legs (0→1 in, 1→0 out), so **symmetric**
   shaders auto-mirror with no code. `bounce` relies on exactly this.
2. For **asymmetric** shaders, an `int iIsReversed` is `1` on reverse legs, `0`
   otherwise — with a documented footgun: branch on `iIsReversed == 1`, never `!= 0`.

The pain is real and widespread: **24 of the 53 shaders bundled at design time
branch on `iIsReversed`**, and most hand-write the *same* line to recover direction-
independent progress. From `matrix/effect.frag`:

```glsl
float legProgress = (iIsReversed == 1) ? (1.0 - iTime) : iTime;   // un-flip to 0→1 forward
// …later…
float windowAlpha = (iIsReversed == 1) ? windowSweep : (1.0 - windowSweep);  // the real asymmetry
```

So there are three layers of boilerplate: the `== 1` footgun, the manual
`iTime` un-flip, and the asymmetric branch itself.

**T1.5 — direction ergonomics.** Three additive pieces, mirroring how T1.4 turns
`main()` into an entry function:

1. **Named `p_reversed`** (bool, generated alongside the param preamble) replaces
   `iIsReversed == 1` — kills the exact-equality footgun.
2. **A `legProgress()` helper** in `animation_uniforms.glsl` returns the un-flipped
   0→1 forward progress, so the 24 shaders that hand-roll it
   (`(iIsReversed == 1) ? 1.0 - iTime : iTime`) just call it. Pure-additive shared
   helper; cheap; high coverage.
3. **Direction-dispatched entry points** (the T1.4 entry convention, specialized for
   animations):
   - **`vec4 pTransition(vec2 uv, float t)`** — symmetric. One function; the
     harness keeps the existing `iTime`-flip so it auto-mirrors. `t` is `iTime`.
   - **`vec4 pIn(vec2 uv, float t)` + `vec4 pOut(vec2 uv, float t)`** —
     asymmetric. `t` is **always** 0→1 forward leg progress (the harness applies
     `legProgress()` for you), and the harness calls the right function by leg
     direction. The author **never** touches `iIsReversed`, never un-flips `iTime`,
     and the `== 1` footgun is gone.

   The "in/out toggle" thus becomes *which entry functions the author provides*: one
   `pTransition` (symmetric) or a `pIn`/`pOut` pair (asymmetric). `main()` stays
   the escape hatch for shaders that want raw `iTime`/`iIsReversed`.

#### `matrix` before / after (asymmetric)

```glsl
// before — the author manages direction by hand
#define letterSize customParams[0].x          // hand-written slot mapping
#define trailColor customColors[0]
void main() {
    float legProgress = (iIsReversed == 1) ? (1.0 - iTime) : iTime;   // un-flip
    // … rain falls over legProgress …
    float windowAlpha = (iIsReversed == 1) ? windowSweep : (1.0 - windowSweep);
    fragColor = composite(...);
}

// after — generated #defines + harness-dispatched direction
//   p_letterSize / p_trailColor / p_reversed are generated; t is forward 0→1
vec4 pIn(vec2 uv, float t)  { return matrixRain(uv, t, /*windowFadingIn=*/true);  }
vec4 pOut(vec2 uv, float t) { return matrixRain(uv, t, /*windowFadingIn=*/false); }
```

The `iTime` un-flip and both `iIsReversed == 1` branches collapse into the choice of
which function the harness calls. A symmetric shader like `bounce` instead writes a
single `pTransition` and keeps its current auto-mirror behaviour.

### A4 — Definition of done (animation system)

- Animation shaders get the same named-param preamble (T1.1) and validation
  (T1.2/T1.3), generated by a shared component and applied on **both** the daemon
  and kwin-effect paths.
- Animation packs are gated by `test_animation_shader_preamble_bake.cpp`, which
  bakes every pack through the full runtime assembly (entry scaffold + `p_<id>`
  preamble) on the daemon path. *(The `plasmazones-shader-validate` CLI now has
  `--overlay` / `--animation` / `--surface` modes, each with its own CI gate,
  and the kwin-branch `PLASMAZONES_KWIN` bake landed as
  `test_animation_shader_kwin_bake`.)*
- An author can write `pTransition` (symmetric) or `pIn`/`pOut` (asymmetric) with
  no `iIsReversed` handling; `p_reversed` and `legProgress()` exist for authors who
  keep `main()`.
- All 53 design-time animation packs compile on every runtime that loads them.
  Packs whose `appliesTo` omits `appearance` are compositor-only (kwin-effect
  only, gated by `shaderEffectIsCompositorOnly`) and never run on the daemon,
  so cross-runtime render parity applies only to daemon-eligible packs.

---

## Suggested sequencing

1. **T1.3** (`#line` directives) — small, self-contained, improves every error
   message; do it first so T1.1/T1.2/T1.4 land on good diagnostics.
2. **T1.1** (named-param preamble + auto-slot) — the headline ergonomic win;
   everything else reads better with it in place. **Build the generator as a shared,
   runtime-agnostic component from the start** (see A2) and adopt it on the animation
   side first (A1, no explicit-slot legacy), then the zone side.
3. **T1.4** (entry-point convention) — builds on T1.1's generation step (same
   harness-wraps-your-code seam); gate on explicit sign-off since it's the most
   opinionated change.
4. **T1.5** (direction ergonomics: `p_reversed`, `legProgress()`, `pIn`/`pOut`) —
   animation-only; pairs with T1.4's entry convention. Land with or right after T1.4.
5. **T1.2** (validation CLI + CI gate) — locks in correctness for both bundled sets,
   baking animation packs with **and** without `PLASMAZONES_KWIN` (A2); gives authors
   the pre-load check; validates that packs defining only `pZone`/`pImage`/
   `pTransition`/`pIn`+`pOut` wrap and compile.
6. **T2.1 / T2.3** (hello-world + create action + quick-start docs) — onboarding,
   now that authoring is pleasant. Cover both systems (a zone hello-world and an
   animation hello-world).
7. **T2.2** (editor tooling).
8. **T3.1 / T3.2** (in-app preview + compile feedback).

Tiers 1–2 deliver the bulk of the parity-with-autotile value. Tier 3 is polish
that closes the cross-app gap.

## Risks & open questions

- **Preamble identifier naming** (`p_<id>` vs bare `<id>`): prefix is safer
  against collisions with author locals; confirm in review.
- **`#line` filename support** depends on what the bake targets / glslang accept;
  fall back to integer source-index + legend if filenames aren't honored.
- **Auto-slot ordering must be deterministic and stable** across reloads, or a
  saved param value could rebind to a different lane. Declaration order in
  `metadata.json` is the natural key; document that reordering params is a
  breaking change for saved values (same caveat autotile has).
- **Where the preamble is spliced** must stay downstream of the `common.glsl` UBO
  declaration; covered by injecting after include expansion, but worth a test that
  asserts a pack with zero explicit slots still compiles and binds.
- **Entry-point detection** (T1.4): the "does this source define `main()`?" check
  must run on the *include-expanded* source (a `main()` could in principle come
  from an include) and must not false-match a `main` substring in a comment or
  string. A tolerant-but-correct parse, or simply requiring the entry function at
  top level, needs deciding in review.
- **The whole-frame label composite** in per-zone shaders (e.g. `cosmic-flow`)
  doesn't fit `pZone` cleanly — resolve whether to expose an optional
  `pComposite(frame)` post-hook or have such packs keep `main()`.
- **Standalone-validity trade** (T1.4): authoring only the entry function means the
  file isn't compilable in isolation; confirm the `--emit-scaffold` sidecar is an
  acceptable answer for editor tooling, or keep `main()` as the documented default.
- **Cross-runtime generation parity** (A2): the generated preamble/wrapper must
  produce identical author-visible symbols on the daemon (UBO) and kwin-effect
  (`PLASMAZONES_KWIN` default-block) paths. A test should bake a generated-entry
  animation shader on both branches and assert both compile — the std140 offset
  contract is already pinned by `BaseUniforms.h` asserts +
  `test_animation_shader_preamble_bake` (the fragment-UBO bake;
  `test_animation_shader_bake` covers the vertex stage only), so the new
  surface is the *generated* GLSL, not the UBO layout.
- **`legProgress()` and the `iTime` flip** (T1.5): `pIn`/`pOut` receive forward
  0→1 `t`, but `pTransition` receives raw (flipped) `iTime`. Document this
  difference loudly — a symmetric author who assumes `t` is always forward will get
  a reversed out-leg. The asymmetric pair is the "I want forward progress" path.
- **Per-event direction availability**: whether the settings UI should disable the
  "out" assignment for a shader that only defines `pIn`, or fall back to mirroring,
  is a UX decision for the AnimationEventCard work — out of scope here but worth a
  flag so it isn't silently dropped.
- **What `uv` is in the animation entry points** (T1.5): `bounce` reads
  `anchorRemap(vTexCoord)` (card space) while anchor-extent shaders get card-space
  `vTexCoord` directly, so the meaning depends on `fboExtent`. The harness must pass
  a *defined, fboExtent-normalized* `uv` (most likely card/anchor space, what
  `surfaceColor()`/`boundaryMaskAA()` consume) and still expose raw `vTexCoord` as a
  global for surface-extent shaders that need it. Pin this convention before
  implementing the wrapper, or the entry signature is ambiguous.

## Definition of done (Tier 1)

- A shader author can declare a param with no `slot` and read it in GLSL by name.
- A shader author can implement `pZone` (or `pImage`) with no `main()` and have
  it compile and render; packs that define `main()` are wrapped/used unchanged.
- `plasmazones-shader-validate <pack>` reports per-stage OK/errors over
  `data/overlays/*`, runs in CI (`shader_validate_bundled`), and exits non-zero on
  failure. *(Animation packs are gated by both
  `test_animation_shader_preamble_bake` and the CLI's `--animation` mode.)*
- glslang errors reference the author's file/line.
- All 26 design-time zone packs migrated to the named-param + entry-point API and render
  identically (25 via pImage/pZone; magnetic-field keeps its custom-vertex main()).
- The animation system meets its own [definition of done](#a4--definition-of-done-animation-system)
  (cross-runtime generation, both-branch validation, `pTransition`/`pIn`/`pOut`,
  all 53 design-time animation packs migrated).
