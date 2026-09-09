<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Validation gates for a generated theme

Run every gate. A gate you skipped is a claim you cannot make. Report each one's real output.

## 0. Where the packs live while you work

Both the schema script and the shader validator locate their inputs by directory shape: the
script only validates files matching `data/<family>/*/metadata.json` under its `--root`, and
the validator detects a pack's family from the sibling `shared/` directory and resolves
includes from it. A pack dropped loose in `scratchpad/<theme>/` passes gate 1 vacuously
("OK (0 file(s) validated)") and fails gate 2's detection. So the scratchpad mirrors the
repo's `data/` layout, with the shared pieces symlinked in:

```bash
T=scratchpad/<theme>
mkdir -p $T/data/{animations,overlays,surface,curves} $T/renders $T/sets/{motionsets,decorationsets,overlaysets}
ln -sfn ../../../data/schemas $T/data/schemas
for f in animations overlays surface; do ln -sfn ../../../../data/$f/shared $T/data/$f/shared; done
```

Packs go in `$T/data/<family>/<id>/`, curves in `$T/data/curves/`, set files in
`$T/sets/<kind>/`. Every gate below is written against `$P`, which is `$T/data` while the
packs are in the scratchpad (the default, and the whole run for `--into user`) and `data`
after they have been copied into the repo for `--into repo`:

```bash
P=$T/data      # scratchpad, or --into user
P=data         # after the --into repo copy
```

## 1. Schema (fast, author-time)

```bash
python3 scripts/validate-json-schemas.py --root $T data/animations/<id>/metadata.json data/overlays/<id>/metadata.json data/surface/<id>/metadata.json data/curves/<theme>-settle.json data/curves/<theme>-release.json
```
This is the one gate that does NOT take `$P`: the script resolves a relative file argument
against `--root`, not against the working directory, so the paths are written `data/...`
whichever root is in force (`--root $T` for the scratchpad; drop the flag once the packs are
in the repo's `data/`). A scratchpad-phase `$P/` prefix (`$T/data/...`), or any other cwd-relative
path outside `data/<family>/`, is resolved under the root, lands outside every mapped glob,
and is silently dropped; once `P=data` the two spellings coincide. The last line of output reads `OK (N file(s) validated)`:
N MUST equal the number of files you passed. A file outside `<root>/data/<family>/*/` is
skipped without a message, so `0 file(s)` means the paths or the layout are wrong, not that
the files are fine. With no file args and no `--root` it validates every mapped file under
the repo's `data/`.

## 2. Shader compile: `plasmazones-shader-validate`

Binary: `build/bin/plasmazones-shader-validate` (built by the default target). Needs `glslang`
or `glslangValidator` on PATH for animation and pointer packs, and HARD-FAILS without it.

```bash
build/bin/plasmazones-shader-validate --animation $P/animations/<id>
build/bin/plasmazones-shader-validate --overlay   $P/overlays/<id>
build/bin/plasmazones-shader-validate --surface   $P/surface/<id>
# or auto-detect on several packs at once (the sibling shared/ symlink is what makes detection work):
build/bin/plasmazones-shader-validate $P/animations/<a> $P/overlays/<b> $P/surface/<c>
```
Exit 0 = all OK, 1 = errors, 2 = usage. It checks metadata parse, appliesTo tokens, param
types/ids and slot budget, texture paths, buffer shaders, the strip and main() entry
contracts, and compiles frag (+vert, + buffer passes) with the same scaffold and preamble
the runtime uses. It does NOT check prose, licence, categories, blurRadius slot order, or
visual output.

For every animation class, this CLI compiles the fragment and declared vertex for both
KWin and the Qt-RHI settings preview, plus the default preview vertex when applicable.
Both branches must pass. The bundled bake tests below provide additional runtime coverage;
compilation alone does not establish correct rendering or motion.

`--emit-preamble` writes a `p_generated.glsl` sidecar for editor autocomplete. It is
gitignored; delete it before handing over anyway.

## 3. The bundled-tree tests (only when the packs live under `data/`)

Configure once with `-DBUILD_TESTING=ON`, build with `--parallel 6`, then:

```bash
ctest --test-dir build -N | grep -E 'strip_pack_contract|shell_chrome'   # both are conditional targets, see below
ctest --test-dir build --output-on-failure -R 'shader_validate|animation_shader|strip_pack|pack_validators|animation_pack_bakes|pack_model_detection|zone_entry_scaffold|zone_uniform|shell_chrome|decoration'
```
Gates that iterate every bundled pack:
- `test_animation_shader_param_wiring`: category must be in the canonical list;
  description/author/version/category non-empty; hand-written `#define ... customParams[n]`
  must match slot order (use `p_` accessors and this never bites).
- `test_animation_shader_bake` (the VERTEX stage of every pack under the strict SPIR-V
  target), `test_animation_shader_preamble_bake` (each daemon-eligible `effect.frag`
  through the full runtime assembly), `test_animation_shader_kwin_bake` (kwin dialect,
  every pack, needs GL 4.5 and QSKIPs without it), `test_animation_entry_scaffold`.
- `test_zone_entry_scaffold`: every bundled OVERLAY pack through the zone scaffold. No other
  compile-level sweep of that tree exists apart from `shader_validate_bundled`
  (`test_pack_model_detection` walks it for family detection only). `test_zone_uniform_extension`
  pins the shared overlay header's UBO layout that all of them depend on.
- `test_strip_pack_contract`: a `strip` pack must be identity at zero motion, must not paint
  the gap, must stay inside the edge band. Built only with `USE_KDE_FRAMEWORKS=ON` and
  `BUILD_KWIN_EFFECT=ON`.
- `test_shell_chrome`: loads the whole surface tree. Built only with `BUILD_PHOSPHOR_SHELL=ON`.
- `shader_validate_bundled`, `shader_validate_animations`, `shader_validate_surface`,
  `shader_validate_pointer`.

The `decoration` term matches the decoration settings-controller tests, which iterate no
packs; they are cheap and worth running, but they are not pack sweeps. If the `-N` listing
above lacks the two conditional targets, the strip and surface-tree checks did not run and
the suite reads clean without them: reconfigure with those flags before claiming the gate.

Run the full suite before handing over: `ctest --test-dir build --output-on-failure`
(use `dbus-run-session` if daemon tests complain about the bus).

## 4. Prose and licence (no linter exists, check by hand)

```bash
grep -nE '"(name|description)".*(—|&mdash;| - |;)' $P/animations/<id>/metadata.json $P/overlays/<id>/metadata.json $P/surface/<id>/metadata.json
grep -nE '"#[0-9a-fA-F]{8}"' $P/animations/<id>/metadata.json $P/overlays/<id>/metadata.json $P/surface/<id>/metadata.json
grep -L 'SPDX-License-Identifier' $P/animations/<id>/* $P/overlays/<id>/* $P/surface/<id>/*
head -2 $P/animations/<id>/effect.frag $P/surface/<id>/effect.frag $P/overlays/<id>/effect.frag
```
Expected: no em-dash (typed or as the `&mdash;` escape), no clause-splicing semicolon and no
spaced hyphen in any `name`/`description`; every eight-digit colour is alpha-FIRST
(`#AARRGGBB`), since every family parses colours with `QColor` and a CSS-style `#RRGGBBAA` is
misread silently rather than rejected; every `.frag`/`.vert`/`.glsl` has the header. Licence
per CLAUDE.md "License": a new PlasmaZones-original animation or surface pack says
`LGPL-2.1-or-later` and an overlay says `GPL-3.0-or-later`, while a port of GPL upstream code
stays `GPL-3.0-or-later` with a second `SPDX-FileCopyrightText` line for the upstream author.
The bundled surface tree is mixed (seven packs are GPL: the glass/blur family plus duotone
and mosaic), so a surface sibling saying GPL is not a defect. `metadata.json` and curves carry NO header.

Also check: no rule-of-three flourish, no "not just X, but Y", no "Label: payload" colon,
present tense, reverse leg described.

## 5. Set and profile files

- Filename equals `slugify(name).json`.
- No `baseline` key in set files. Motion sets use `version: 2`; decoration and overlay
  sets use `version: 1`. Non-empty `overrides`.
- Every `path` exists in the domain taxonomy (`libs/phosphor-animation/src/profilepaths.cpp`,
  `libs/phosphor-surface/include/PhosphorSurface/DecorationSupportedPaths.h`).
  Overlay paths instead follow `src/settings/pages/overlayspagecontroller_sets.cpp`:
  `overlay:global` or a layout override. Check registry UUIDs as described in `profiles.md`.
- Every pack id in a chain or `effectId` exists in the generated set or the bundled tree.
- Every parameter override names a declared param of that pack with a value inside min/max.
- Every curve name referenced by a profile exists as a file or a built-in typeId. The
  resolver in `scripts/check-animation-profiles.py` (`curve_reference_resolves(spec,
  shipped_curves)`) encodes the exact rule; its own `data/profiles` target no longer exists,
  so import the function and call it on your set and profile files, passing the NEW theme
  curves' file stems as `shipped_curves`, or every `<theme>-settle` reference reports
  unresolved.
- Motion set: run the mechanical both-halves check from `references/profiles.md`
  ("Motion set: rules that bite") and paste its output. A set that quietly carries no packs
  applies cleanly, changes the durations, and leaves every animation on the pack it already
  had, with no error anywhere to notice.

## 6. Live smoke test (when the user's session is available)

Install into the user dirs (never `cmake --install`, never sudo). `$P` is the scratchpad
copy for `--into user`, the repo copy after `--into repo`:
```bash
cp -r $P/animations/<id> ~/.local/share/plasmazones/animations/
cp -r $P/overlays/<id>   ~/.local/share/plasmazones/overlays/
cp -r $P/surface/<id>    ~/.local/share/plasmazones/surface/
cp $P/curves/<theme>-*.json ~/.local/share/plasmazones/curves/
cp $T/sets/motionsets/<slug>.json     ~/.local/share/plasmazones/motionsets/
cp $T/sets/decorationsets/<slug>.json ~/.local/share/plasmazones/decorationsets/
cp $T/sets/overlaysets/<slug>.json    ~/.local/share/plasmazones/overlaysets/
```
Registries hot-reload on file change (50 ms debounce). A brand-new directory in a watched
root is picked up; if a pack does not appear, that is the case to check first.

Re-run the validator against the INSTALLED copy, not only the source tree:
```bash
build/bin/plasmazones-shader-validate ~/.local/share/plasmazones/animations/<id> ~/.local/share/plasmazones/overlays/<id> ~/.local/share/plasmazones/surface/<id>
```
An installed pack has no sibling `shared/`, so the validator resolves the family's helpers
through the XDG data chain, normally `/usr/share/plasmazones/<family>/shared`. That is a
different lookup from the sibling one a scratchpad or source-tree pack uses, and a pack that
validates there can still fail once installed. It presupposes an installed PlasmaZones. On a
machine without one the auto-detect run warns `no sibling shared/ marker, validating as an
overlay pack` for EVERY family, since no marker was found anywhere: it means the installed
helpers were not found, a non-overlay pack is then checked against the wrong family, and an
overlay pack is by luck checked as the right one and fails its includes. So on the installed
re-run pass the family flags
(`--animation`, `--overlay`, `--surface`) so a failure is the real include miss, and if the
helpers are absent copy `data/<family>/shared` beside the pack in the user dir for the check
only (in the installed layout the sibling is searched first, then the XDG chain), then remove
it.
Then verify in `journalctl --user -f | grep -i plasmazones` that the pack loads with no
"Skipping ... schema validation" and no shader compile warnings, and open Settings →
Appearance → Animations → Library, Settings → Appearance → Decorations → Library and
Settings → Appearance → Overlays → Library to see the live previews. A flat gray preview means the compile failed and Qt
swallowed the log; re-run the validator.

Check each pack at its declared defaults for parameter wiring, rendering, alpha, scaling,
event endpoints and reverse-leg behaviour. If the session is unavailable, report which
runtime checks could not be performed.

## 7. Technical review pass

Dispatch `pz-glsl-shader-reviewer` on the new pack directories and `pz-build-data-reviewer`
on the metadata, curves and set files. Scope their review to shader contracts, integration,
runtime correctness and project rules. Fix technical findings and re-run gates 1 to 5.
These reviewers establish technical correctness. The separate visual development loop in
`visual-development.md` establishes whether rendered output meets the user's brief.
Neither review uses existing shader implementations as an aesthetic standard.
