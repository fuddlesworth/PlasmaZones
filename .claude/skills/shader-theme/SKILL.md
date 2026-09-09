---
name: "Shader Theme"
description: "Build a complete, cohesive PlasmaZones shader theme from a one-line description (for example 'a whole theme around jelly effects'): animation packs for every event class, a zone overlay pack, surface decoration packs (border, glass, ambience), motion curves, and the decoration set and motion set that apply it. Use when: theme, themed pack, pack set, shader theme, whole look, cohesive effects, 'a theme around X', 'make me a X pack for everything', or any request to create several shader packs that belong together."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Shader Theme

Turn a description into a full themed pack set, exactly the way the bundled Phosphor set was
built: one motif, one palette, one motion character, carried across all three shader families
and the artefacts that select them. The output is a set of directories under `data/` (or the
user dirs), two curves, a decoration set and a motion set, all validated with the project's
real gates.

**This skill runs to completion.** Invoking it authorises the whole pipeline: research, brief,
every pack, validation, review loop, report. Do not stop to ask whether to continue. Ask a
question only when two readings of the brief would produce materially different themes AND
the user is available; otherwise pick the reading a careful designer would, state it in the
brief, and go.

## Usage

```
/shader-theme <description> [--into repo|user] [--scope minimal|full] [--name <slug>]
```

- `--into repo` (default): packs go under `data/animations|overlays|surface|curves`, licensed
  and test-gated like bundled packs. `--into user`: packs go to `~/.local/share/plasmazones/...`
  and no bundled-tree tests apply.
- `--scope full` (default) builds every row of the coverage matrix. `minimal` builds only
  the rows marked core (seven pack rows plus the curves and profiles).
- `--name` overrides the theme slug derived from the description.

## Reference files (read them, they are the contracts)

| file | when |
|---|---|
| `references/prior-art-phosphor.md` | before writing the brief |
| `references/animations.md` | before any `data/animations` pack |
| `references/overlays-surface.md` | before the overlay or any surface pack |
| `references/profiles.md` | before curves and sets |
| `references/validation.md` | before claiming anything works |

Also read `CLAUDE.md` sections "License" and "User-Facing Text (Plain Prose)". They apply to
every `name` and `description` you write.

## Pipeline

### 1. Read the prior art

Read `references/prior-art-phosphor.md`, then open at least these sources fully:
`data/animations/phosphor-bloom/effect.frag`, `data/animations/phosphor-stream/effect.vert`,
`data/animations/desktop-phosphor/effect.frag`, `data/overlays/phosphor-flux/effect.frag`,
`data/surface/border-phosphor/effect.frag`, `data/surface/phosphor-glass/effect.frag`,
`data/surface/phosphor-motes/effect.frag`, and one non-phosphor pack per family that is
closest to the requested motif (for "jelly": `bounce`, `wobble`, `rippled-glass`, `liquid-canvas`).
Also read the family shared headers named in the reference files. Do not write from memory.

### 2. Write the theme brief

Write `<scratchpad>/theme-brief.md` with:

1. **Theme id** (kebab slug, e.g. `jelly`) and display word ("Jelly").
2. **Motif sentence**: the one physical idea every pack expresses (Phosphor: "light flowing
   through containment"; jelly: "a soft translucent solid that deforms, wobbles and settles").
3. **Palette**: four accent stops plus one ground, as hex, with the exact param ids and names
   every pack will declare (`colorA..D` naming is fine but pick words: `colorLime`, `colorMint`, ...).
   If the description names no colours, choose a palette that fits the motif and say why.
4. **Signature helper**: the GLSL helper every pack pastes verbatim (the four-stop gradient
   and, for a physical motif, the deformation or timing primitive, e.g. a damped-spring
   `jellyWobble(t, freq, damp)`).
5. **Motion character**: enter/leave asymmetry, overshoot count, durations. This becomes the
   two curves and the motion set.
6. **Motif vocabulary**: five to eight phrases reused across descriptions.
7. **Coverage matrix** (below) with one line per row: pack id, event class, what it does in
   two sentences, reference pack to copy structure from.

Naming: window packs `<theme>-<verb-noun>`; desktop pack `desktop-<theme>`; border pack
`border-<theme>`; overlay `<theme>-<noun>`; glass `<theme>-glass`; ambience `<theme>-<noun>`;
curves `<theme>-settle` and `<theme>-release`. Never reuse an existing id (check the trees).

#### Coverage matrix

| row | family | class / kind | scope |
|---|---|---|---|
| open/close (symmetric, universal) | animations | appearance | core |
| geometry morph (placeIn/placeOut/layoutSwitch) | animations | geometry, vert + grid | core |
| move (held drag) | animations | move | core |
| desktop switch | animations | desktop | core |
| zone overlay | overlays | | core |
| window border | surface | providesBorder | core |
| window glass/blur | surface | needsBackdrop multipass | core |
| minimize | animations | appearance + iIconRect | full |
| desktop peek | animations | desktop | full |
| scrolling strip | animations | strip | full |
| tab switch | animations | tab | full |
| ambience (particles/margin) | surface | paddingParam | full |
| curves (settle, release) | curves | | core |
| decoration set, motion set (both halves per event) | profiles | | core |

### 3. Author the packs, one family at a time

For each row, in this order: animations, overlay, surface, curves.

1. Read the family reference file and the reference pack for the row.
2. Write `metadata.json` first (id, name, description, author `PlasmaZones`, version `1.0`,
   category from the animation canonical list or the overlay/surface vocabulary in the
   reference file, appliesTo, the theme palette params last).
3. Write `effect.frag` (and `effect.vert` when the reference pack has one) starting with the
   SPDX header for the tree, a 6 to 15 line comment stating the motif, the layers, the reverse
   leg behaviour, and the alpha contract, then the pasted signature helper, then the entry
   function. Every declared param must be read; every `p_` read must be declared.
4. Run gate 1 and gate 2 from `references/validation.md` on that pack immediately. Fix until
   exit 0 before starting the next pack. Never batch validation to the end.

Quality bar per pack (what reviewers reject):
- The effect reads as the motif at a glance at 300 ms, not only in slow motion.
- **It reads that way at the pack's own DECLARED DEFAULTS**, not only at values you tuned
  while writing it. Walk each `default` in `metadata.json` and ask what the shader does with
  it. A pack whose motif only appears once the user moves a slider ships as an effect that
  does nothing: the two most visible defects in the first run of this skill were an ambience
  pack invisible at its defaults and an overlay whose zone body vanished at its default
  ground darkness. Neither was caught by any gate below, because both compiled and validated
  perfectly.
- Both legs work (open and close, in and out, from and to). Check the reverse leg explicitly.
- Endpoint early-outs: progress 0 draws nothing or identity, progress 1 is the clean surface.
- Premultiplied alpha, clamped, never more opaque than the surface at that pixel.
- Overlay lengths go through `zoneSdf`/`zoneBorderWidth`/`zoneLen`; surface lengths use
  `* uSurfaceScale`; never raw device px beside a scaled length.
- Bounded loops, no full-canvas extra passes beyond what the reference pack already pays.
- Focus cue on decorations (`focusDim`), no-backdrop fallback on glass.

### 4. Curves and sets

Follow `references/profiles.md`:
- `data/curves/<theme>-settle.json` and `<theme>-release.json`.
- `<theme>.json` decoration set covering `window`, `shell.panel`, `shell.appletPopup`, `osd`
  and the four `popup.*` paths, chains built from the new surface packs (plus bundled
  `shadow` where the brief wants one).
- `<theme>.json` motion set, `"version": 2`, carrying BOTH halves of every event it covers:
  the timing (`curve` by name, `duration`) AND the pack, as a nested
  `"shader": { "effectId": "<pack id>" }`.

A motion set that carries only timing is the single most likely thing to go wrong here, and
it fails silently: the set applies, the durations change, and every animation keeps whatever
pack it had. Format 1 (timing only) is still READ for old files, so nothing warns you. The
rules that actually bite:

- **`"version": 2` is mandatory** when any entry carries a `shader` key. A build older than
  format 2 refuses a v2 set outright, which is the correct clean failure — do not write
  version 1 with shader keys hoping for the best.
- **A `shader` key is only legal on a path the daemon consumes as a shader leg.** The SSOT is
  `shaderConsumedLeafEventPaths()` in `src/core/types/animationshadersupportedpaths.h`, plus
  every ancestor of those leaves. `eventPathSupportsShaderLeg()` refuses anything else, so the
  entry is dropped on apply. Read that header; do not infer the list from the event taxonomy.
- **The pack's `appliesTo` must cover the path's class.** Nothing validates this — an
  `appliesTo: ["desktop"]` pack on `window.appearance.open` is accepted and then never plays.
- **An entry may carry the pack half ALONE.** That is normal, not a mistake: a theme usually
  assigns a geometry pack to `window.movement.placeIn` / `placeOut` / `layoutSwitch` and a
  strip pack to `scrolling.view` without touching their timing. Applying such an entry leaves
  the path's timing alone, deliberately.
- **No `baseline` key**, at any version. The set validator refuses a set that carries one even
  when empty.
- Filename MUST be `slugify(name) + ".json"`, 4-space indent, alphabetically sorted keys.

Then VERIFY the file rather than reading it back: for every entry carrying a `shader`, confirm
the path appears in the SSOT header and that the named pack's `metadata.json` `appliesTo`
covers that path's class. A one-off script over the set file is the right amount of effort;
eyeballing a sixteen-entry file is not.

- For `--into repo`, set files are delivered in the scratchpad and the report, since the repo
  ships no set files. For `--into user`, write them to the user dirs directly.

### 5. Validate everything

Run every gate in `references/validation.md` in order. For `--into repo`, that includes the
full ctest run after a build with `--parallel 6`. Capture real output. Fix and re-run until
clean.

### 6. Review loop

Dispatch `pz-glsl-shader-reviewer` over the new pack directories and `pz-build-data-reviewer`
over metadata, curves and set files (run them in the background, in one message, and wait
for their reports). Apply every finding, re-run gates 1 to 5, and re-dispatch. The loop ends
when a full dispatch of both reviewers returns no findings.

### 7. Report

Lead with what exists and whether it is verified. Then:
- whether the theme was APPLIED and seen running, or only written and validated. These are
  different claims and the second one is common: say which it is in the first two sentences.
- a table of every pack (id, family, class, event paths it is assigned to)
- the curves and their parameters
- where the set files and profile snippet are and how to apply them (Settings > Appearance > Decorations >
  Library > Sets and Settings > Appearance > Animations > Library > Sets — a format-2 motion set carries the
  packs too, so there is no separate step for them)
- every gate with its real exit status, and anything left unverified (a live session smoke
  test you could not run, glslang missing, tests not built)
- what was deliberately left out of scope and why

Do not commit. The user commits.

## Failure modes to avoid

- Writing a shader from memory of the uniform contract. Read the shared header first.
- A pack that is beautiful in one leg and static in the other.
- Reusing the Phosphor palette or motifs. The theme is the user's, not Phosphor with new ids.
- An em-dash or clause-splicing semicolon in any description. Two sentences instead.
- A surface colour param written as `#RRGGBB`. Surface packs use Qt form `#AARRGGBB`
  (alpha first); overlays and animations use `#RRGGBB`. See `references/overlays-surface.md`.
- A motion set that carries only timing. It applies cleanly, changes the durations, and
  leaves every animation on the pack it already had, with nothing anywhere to say so. Every
  event the theme owns needs its `"shader": { "effectId": ... }` half and the file needs
  `"version": 2`.
- A pack assigned to a path whose class its `appliesTo` does not cover. Accepted on write,
  never plays.
- `preview.png` files. Previews are live; do not generate images.
- Adding a category outside the canonical list without extending the test deliberately.
- Claiming hot-reload worked without a journal line proving the pack loaded.
