---
name: shader-theme
description: "Build a complete, cohesive PlasmaZones shader theme from a one-line description (for example 'a whole theme around jelly effects'): animation packs for every event class, a zone overlay pack, surface decoration packs (border, glass, ambience), motion curves, and the decoration set and motion set that apply it. Use when: theme, themed pack, pack set, shader theme, whole look, cohesive effects, 'a theme around X', 'make me a X pack for everything', or any request to create several shader packs that belong together."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Shader Theme

Build the shader theme the user requests. This skill supplies the shader contracts,
pack metadata, event coverage, profile integration and validation needed to make it work.
Visual design comes from the user's request. The skill prescribes no palette, motif,
composition, motion style or similarity target.

Complete the requested packs and integration, validate them, and judge actual rendered output
against the brief. Compilation and contract review do not establish visual quality.

## Usage

```
/shader-theme <description> [--into repo|user] [--scope minimal|full] [--name <slug>]
```

These flags are read from the request text; nothing parses them, so do not look for a
parser.

- `--into user` (default): prototype in ignored `scratchpad/<theme>/`, laid out as
  `references/validation.md` section 0 prescribes (a `data/<family>/` tree with the shared
  helpers symlinked in, so the gates run unchanged), then deliver packs to
  `~/.local/share/plasmazones/...`. Local skill tests stay out of bundled inventories.
- `--into repo`: explicitly requested bundled packs go under
  `data/animations|overlays|surface|curves`, licensed and test-gated like bundled packs.
  Prototype in scratchpad first. Respect an existing destination supplied by the user.
- `--scope full` (default) builds every row of the coverage matrix. `minimal` builds only
  the rows marked core (seven pack rows plus the curves and profiles).
- `--name` overrides the theme slug derived from the description.

## Reference files

| file | when |
|---|---|
| `references/animations.md` | before any `data/animations` pack |
| `references/overlays-surface.md` | before the overlay or any surface pack |
| `references/profiles.md` | before curves and sets |
| `references/validation.md` | before claiming anything works |
| `references/visual-development.md` | before design, prototyping and visual review |

Also read `CLAUDE.md` sections "License" and "User-Facing Text (Plain Prose)". They apply to
every `name` and `description` you write.

## Pipeline

### 1. Read the contracts

Read the relevant family references, shared headers, schemas, host scaffolds and contract
tests. Use these as the source of truth for uniforms, entry points, coordinate systems,
sampling, alpha, parameter limits and runtime support.

Do not use bundled effect implementations as templates or required reading. If a contract
detail remains unresolved, inspect only the relevant host plumbing in an existing shader.
Read a particular effect's visual implementation when the user explicitly requests it.

### 2. Map the requested coverage

Map each requested pack to its family, event class, id and destination. Check existing ids
for collisions and follow each family's schema naming requirements. Record the required
uniforms, entry points, optional capabilities and event assignments. Keep technical feasibility
separate from the visual choices below; do not compare against the bundled catalogue.

#### Coverage matrix

| row | family | class / kind | scope |
|---|---|---|---|
| open/close | animations | appearance | core |
| geometry morph (placeIn/placeOut/layoutSwitch) | animations | geometry | core |
| move (held drag) | animations | move | core |
| desktop switch | animations | desktop | core |
| zone overlay | overlays | | core |
| window border | surface | providesBorder | core |
| window glass/blur | surface | needsBackdrop when sampling backdrop | core |
| minimize | animations | appearance + iIconRect | full |
| desktop peek | animations | desktop | full |
| scrolling strip | animations | strip | full |
| tab switch | animations | tab | full |
| ambience / margin | surface | paddingParam | full |
| curves (settle, release) | curves | | core |
| decoration set, motion set (both halves per event), overlay set (global baseline) | profiles | | core |

### 3. Establish the visual direction before expanding coverage

Read `references/visual-development.md`. Dispatch `pz-shader-art-director` with the user's
brief, requested coverage and host capabilities. It proposes two distinct interpretations
and a provisional preference, then confirms the direction from rendered studies. The parent records the chosen direction
and observable acceptance criteria in `scratchpad/<theme>/design.md`; this is a working
artifact, not an approval checkpoint. Preserve any direction the user already chose.

For a material-led brief, render two small shader studies that use different material
mechanisms before completing a decoration chain. Judge each both on a diagnostic patch and
at its intended desktop footprint. Follow the study procedure in `visual-development.md`.
Then prototype the representative chain and motion together before expanding event coverage.
An attractive swatch that loses its identity on a window is not a successful prototype.

### 4. Author the packs

The parent owns the shared visual direction and keeps the art director available for
design decisions after renders. Do not turn the director into the implementation owner.
For full scope, delegate surface/overlay and
animation implementation to separate agents with disjoint file ownership. Give each the
same design artifact and its family contracts. The motion implementation owner also owns
curves so geometry and timing are developed together. Do not give an agent bundled effects
as visual references. Validate each pack within its owning agent before moving on.

For each requested pack:

1. Read the family contract reference and its relevant shared headers, schemas and scaffolds.
   Resolve any remaining integration question using the targeted fallback in step 1.
2. Write `metadata.json` with the required identity, category, event classes and parameters.
3. Write `effect.frag` and any vertex or buffer shaders required by the implementation.
   Follow the family's entry, include, parameter and licence contracts. Every declared
   parameter must be used; every `p_` read must be declared.
4. Run gate 1 and gate 2 from `references/validation.md` on that pack immediately, from the
   scratchpad layout section 0 of that file prescribes. Fix until exit 0 before starting the
   next pack. Never batch validation to the end.

Technical checks per pack:
- Verify both legs and the endpoint behaviour required by the event class.
- Follow the family's alpha and compositing contract.
- Overlay lengths go through `zoneSdf`/`zoneBorderWidth`/`zoneLen`; surface lengths use
  `* uSurfaceScale`; never mix logical and device pixels.
- Keep loops bounded and account for pass and sample costs.
- Handle focus state and unavailable backdrop where the surface contract requires them.
- Verify parameter wiring and rendering at declared defaults.

### 5. Curves and sets

Follow `references/profiles.md`:
- `<theme>-settle.json` and `<theme>-release.json` in the chosen destination's `curves/`.
- `<theme>.json` decoration set covering `window`, `shell.panel`, `shell.appletPopup`, `osd`
  and the four `popup.*` paths, chains built from the new surface packs (plus bundled
  `shadow` when requested).
- `<theme>.json` motion set, `"version": 2`, carrying BOTH halves of every event it covers:
  the timing (`curve` by name, `duration`) AND the pack, as a nested
  `"shader": { "effectId": "<pack id>" }`. A motion set that carries only timing applies
  cleanly and changes nothing visible. Follow "Motion set: rules that bite" in
  `references/profiles.md` and run its verification script before reporting.
- `<theme>.json` overlay set with a single `overrides` entry at `path: "overlay:global"`
  naming the new overlay pack by its registry UUID (`shaderId`), no per-layout overrides
  (layout UUIDs are per machine) and no `baseline` key. Without it the overlay pack you built
  is never assigned.

Set files are written to `$T/sets/<kind>/` in the scratchpad (`$T` as defined in
`references/validation.md` section 0). For `--into repo` they are
delivered there and in the report, since the repo ships no set files. For `--into user`, copy
them to the user dirs as gate 6 in `references/validation.md` shows.

### 6. Validate everything

Run every gate in `references/validation.md` in order. For `--into repo`, that includes the
full ctest run after a build with `--parallel 6`. Capture real output. Fix and re-run until
clean.

### 7. Render, critique and revise

Use the evidence and dispatch rules in `references/visual-development.md` at both the
prototype milestone and final coverage. Independent reviewers have distinct responsibilities:

- `pz-shader-material-reviewer`: material, composition, contrast and legibility in actual renders.
- `pz-shader-motion-reviewer`: action, pacing, continuity and material behavior in playback.
- `pz-glsl-shader-reviewer`: shader contracts, runtime correctness and cost.
- `pz-build-data-reviewer`: metadata, curves, set assignments and project rules.

Run independent reviews concurrently when slots allow; queue the rest. The parent reconciles
findings, applies fixes and captures fresh evidence. Revalidate changed packs immediately.
Technical findings must be resolved. Visual findings must cite evidence and the user's brief;
record aesthetic disagreements without treating every preference as a defect.

Allow up to three visual revision rounds per milestone unless the user specifies a budget.
Use the structural-revision rule in `visual-development.md`: a failed core material or
motion mechanism brings the art director back before the next edit. The budget must not
be spent entirely on variants of a rejected mechanism.
Stop earlier when the brief's acceptance criteria are demonstrated and no material visual
findings remain. If evidence is unavailable or the rounds do not converge, report the result
as a prototype with the specific unmet criteria; do not call it visually verified. Do not
silently reduce the requested coverage or substitute a compile pass for a visual verdict.

### 8. Report

Lead with what exists and whether it is verified. Then:
- whether the theme was APPLIED and seen running, or only written and validated. These are
  different claims and the second one is common: say which it is in the first two sentences.
- a table of every pack (id, family, class, event paths it is assigned to)
- the curves and their parameters
- where the three set files are and how to apply them (Settings → Appearance → Decorations →
  Library → Sets, Settings → Appearance → Animations → Library → Sets, and Settings →
  Appearance → Overlays → Library → Sets, each followed by the page's Apply; a format-2 motion
  set carries the packs too, so there is no separate step for them)
- links to rendered evidence, the visual verdict and any unresolved visual findings
- every gate with its real exit status, and anything left unverified (a live session smoke
  test you could not run, glslang missing, tests not built)
- what was deliberately left out of scope and why

Do not commit. The user commits.

## Failure modes to avoid

- Writing a shader from memory of the uniform contract. Read the shared header first.
- A pack that fails its event contract on the reverse leg.
- An em-dash or clause-splicing semicolon in any description. Two sentences instead.
- A colour param written CSS-style as `#RRGGBBAA`. Every family parses colour strings with
  `QColor`, which reads eight hex digits as `#AARRGGBB` (alpha FIRST); `#RRGGBB` (opaque) and
  `#AARRGGBB` are both fine in every family, but an alpha-last value is silently misread,
  not rejected. See `references/overlays-surface.md`.
- A motion set without its `shader` half, or a pack on a path its `appliesTo` does not cover.
  The first changes nothing visible, the second is accepted on write and never plays. See
  "Motion set: rules that bite" in `references/profiles.md`.
- Shipping `preview.png` inside a pack as a substitute for live previews. Rendered review
  images and clips belong in the scratchpad and are required evidence for visual claims.
- Adding a category outside the canonical list without extending the test deliberately.
- Claiming hot-reload worked without a journal line proving the pack loaded.
