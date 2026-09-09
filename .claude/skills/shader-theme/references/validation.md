<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Validation gates for a generated theme

Run every gate. A gate you skipped is a claim you cannot make. Report each one's real output.

## 1. Schema (fast, author-time)

```bash
python3 scripts/validate-json-schemas.py data/animations/<id>/metadata.json data/overlays/<id>/metadata.json data/surface/<id>/metadata.json data/curves/<name>.json
```
With no args it validates every mapped file under `data/`.

## 2. Shader compile: `plasmazones-shader-validate`

Binary: `build/bin/plasmazones-shader-validate` (built by the default target). Needs `glslang`
or `glslangValidator` on PATH for animation packs, and HARD-FAILS without it.

```bash
build/bin/plasmazones-shader-validate --animation data/animations/<id>
build/bin/plasmazones-shader-validate --overlay   data/overlays/<id>
build/bin/plasmazones-shader-validate --surface   data/surface/<id>
# or auto-detect on several packs at once:
build/bin/plasmazones-shader-validate data/animations/<a> data/overlays/<b> data/surface/<c>
```
Exit 0 = all OK, 1 = errors, 2 = usage. It checks metadata parse, appliesTo tokens, param
types/ids, texture paths, buffer shaders, and compiles frag (+vert, + buffer passes) with
the same scaffold and preamble the runtime uses. It does NOT check prose, licence, categories,
or visual output.

For every animation class, this CLI compiles the fragment and declared vertex for both
KWin and the Qt-RHI settings preview, plus the default preview vertex when applicable.
Both branches must pass. The bundled bake tests below provide additional runtime coverage;
compilation alone does not establish correct rendering or motion.

`--emit-preamble` writes a `p_generated.glsl` sidecar for editor autocomplete. It is
gitignored; delete it before committing anyway.

## 3. The bundled-tree tests (only when the packs live under `data/`)

Configure once with `-DBUILD_TESTING=ON`, build with `--parallel 6`, then:

```bash
ctest --test-dir build --output-on-failure -R 'shader_validate|animation_shader|strip_pack|pack_validators|shell_chrome|decoration'
```
Gates that iterate every bundled pack:
- `test_animation_shader_param_wiring`: category must be in the canonical list;
  description/author/version/category non-empty; hand-written `#define ... customParams[n]`
  must match slot order (use `p_` accessors and this never bites).
- `test_animation_shader_bake` (daemon SPIR-V, appearance packs), `test_animation_shader_kwin_bake`
  (kwin dialect, every pack, needs GL 4.5), `test_animation_shader_preamble_bake`,
  `test_animation_entry_scaffold`.
- `test_strip_pack_contract`: a `strip` pack must be identity at zero motion, must not paint
  the gap, must stay inside the edge band.
- `test_shell_chrome`: loads the whole surface tree.
- `shader_validate_bundled`, `shader_validate_animations`, `shader_validate_surface`.

Run the full suite before committing: `ctest --test-dir build --output-on-failure`
(use `dbus-run-session` if daemon tests complain about the bus).

## 4. Prose and licence (no linter exists, check by hand)

```bash
grep -n '—\|;' data/animations/<id>/metadata.json data/overlays/<id>/metadata.json data/surface/<id>/metadata.json
grep -L 'SPDX-License-Identifier' data/animations/<id>/* data/overlays/<id>/* data/surface/<id>/*
head -2 data/animations/<id>/effect.frag data/surface/<id>/effect.frag data/overlays/<id>/effect.frag
```
Expected: no em-dash and no clause-splicing semicolon in any `name`/`description`; every
`.frag`/`.vert`/`.glsl` has the header; animations and surface say `LGPL-2.1-or-later`,
overlays say `GPL-3.0-or-later`. `metadata.json` and curves carry NO header.

Also check: no rule-of-three flourish, no "not just X, but Y", no spaced hyphen, no
"Label: payload" colon, present tense, reverse leg described.

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
  resolver in `scripts/check-animation-profiles.py` (`curve_reference_resolves`) encodes the
  exact rule; its own `data/profiles` target no longer exists, so call the function on your
  set and profile files rather than running the script as is.

### Motion set: verify both halves, mechanically

For every entry in the motion set that carries a `"shader"` key:
- the path is in `shaderConsumedLeafEventPaths()` or is an ancestor of one
  (`src/core/types/animationshadersupportedpaths.h`) — otherwise
  `eventPathSupportsShaderLeg()` drops the entry on apply;
- the named pack's `metadata.json` `appliesTo` covers that path's class — nothing validates
  this, so a mismatch is accepted and then never plays;
- the file says `"version": 2`, and carries no `baseline` key.

Script it over the file. A set that quietly carries no packs applies cleanly, changes the
durations, and leaves every animation on the pack it already had — there is no error anywhere
to notice.

## 6. Live smoke test (when the user's session is available)

Install into the user dirs (never `cmake --install`, never sudo):
```bash
cp -r data/animations/<id> ~/.local/share/plasmazones/animations/
cp -r data/overlays/<id>   ~/.local/share/plasmazones/overlays/
cp -r data/surface/<id>    ~/.local/share/plasmazones/surface/
cp data/curves/<name>.json ~/.local/share/plasmazones/curves/
```
Registries hot-reload on file change (50 ms debounce). A brand-new directory in a watched
root is picked up; if a pack does not appear, that is the case to check first.

Re-run the validator against the INSTALLED copy, not only the source tree. An installed pack
resolves its `shared/` prologue from the family root under the same data dir, which is a
different lookup from the sibling-directory one a source-tree pack uses, and a pack that
validates in `data/` can still fail once installed.
Then verify in `journalctl --user -f | grep -i plasmazones` that the pack loads with no
"Skipping ... schema validation" and no shader compile warnings, and open Settings >
Animations, Settings > Decorations and Settings > Snapping > Overlay to see the live previews.
A flat gray preview means the compile failed and Qt swallowed the log; re-run the validator.

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
