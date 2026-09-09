<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Profiles, sets, curves: the "apply the theme" artefacts

Source of truth: `src/settings/stores/shadersetstore.cpp`, `src/settings/services/motionsetdomain.cpp`,
`src/settings/pages/decorationpagecontroller_sets.cpp`, `src/settings/pages/animationspagecontroller_overrides.cpp`,
`libs/phosphor-animation/src/curveloader.cpp`, `src/config/configdefaults_shaders.h`
(`ConfigDefaults::decorationProfileTree()` is the canonical well-formed chain example).

There is NO single theme/bundle object in PlasmaZones. A theme is applied through the
independent artefacts below. Generate all of them.

| artefact | where | carries |
|---|---|---|
| Decoration set | `~/.local/share/plasmazones/decorationsets/<slug>.json` | surface pack chains + params per surface path |
| Motion set | `~/.local/share/plasmazones/motionsets/<slug>.json` | duration/curve AND the animation pack per event path |
| Overlay set | `~/.local/share/plasmazones/overlaysets/<slug>.json` | zone-overlay shader: the global baseline plus a per-layout override each |
| Curves | `~/.local/share/plasmazones/curves/<name>.json` | named easing presets referenced by name |
| Config trees | `~/.config/plasmazones/config.json` | `Animations.ShaderProfileTree` (pack per event), `Animations.MotionProfileTree` (timing per event), `Decorations.DecorationProfileTree`, and `Overlays.OverlayShaderTree` (zone overlay shader, baseline plus per-layout overrides) |

Animation pack selection (`effectId`) lives in `Animations.ShaderProfileTree` in config.json,
and per-event timing beside it in `Animations.MotionProfileTree`. A format-2 MOTION SET
carries BOTH, so a set captures the whole per-event unit the way a decoration set captures the
whole per-surface one. Overlay sets carry the global shader and optional per-layout overrides
in `Overlays.OverlayShaderTree`. Use the set import and apply controls to assign the theme;
the config trees also describe the resulting stored configuration.

## Set envelope (motion and decoration share it)

```json
{
    "name": "Jelly",
    "description": "One sentence of plain prose.",
    "version": 2,
    "overrides": [
        { "path": "<taxonomy path>", "profile": { ... } }
    ]
}
```

`version` above is a MOTION set. A decoration set is `1`. Do not paste a comment into the
file: these are parsed as strict JSON and a `//` line makes the whole set unreadable.

Hard rules (validated on apply and import, whole file refused on any failure):
- Filename MUST equal `slugify(name) + ".json"`. slugify = lowercase letters/digits, every other
  run becomes one `-`, trimmed. "Jelly Theme" -> `jelly-theme.json`. A file whose name does not
  slugify back to its stem is silently skipped by the list.
- `version` is 1 for decoration, 2 for motion. A missing key reads as the domain's CURRENT
  version (so an omitted key on a motion set is read as 2); write it explicitly anyway. A
  `baseline` key, even empty, is REFUSED.
- A set is SELF-CONTAINED: it captures the whole look of what it covers, including
  values that come from a built-in default rather than from something the user
  changed. A decoration set carries its seeded surfaces; a motion set carries the
  per-path animation defaults (window-morph on the placement legs, a fade on the
  OSD and popup legs). Applying one de-seeds on the way in, so a default is
  reproduced rather than frozen as a user override.
- Set files are written IMMEDIATELY. Saving, renaming, deleting or importing a set is not
  staged in any of the three domains. Applying one is a pending settings change in all three,
  so Apply rides Settings Apply/Discard.
- `overrides` must be a non-empty array of `{path, profile}` objects. Every path must be valid
  for the domain. Apply is a MERGE: paths not in the set keep their live values.
- The store writes 4-space indented JSON with alphabetically sorted keys. Match that.

### Motion set profile fields

Timing, at the top of `profile`: `duration` (ms int), `curve` (string), `minDistance`,
`sequenceMode`, `staggerInterval`, `presetName`. That list is the allowlist: any other key
beside a real one is dropped with a journal warning, and an entry whose timing half holds
ONLY unknown keys refuses the whole set.

The animation pack, under a nested `shader` key (format 2, `version: 2`):
```json
"profile": {
    "curve": "ink-settle", "duration": 280,
    "shader": { "effectId": "ink-bloom", "parameters": { "grain": 0.5 } }
}
```
Three states, all round-tripped: `effectId` with a pack id assigns it, `effectId: ""` is the
engaged-empty sentinel for "this event deliberately runs no pack" (it also blocks a parent's
pack from cascading in, so it is NOT the same as omitting the override), and a `shader` object
with `parameters` but no `effectId` overrides only the map over an inherited pack. Omitting the
`shader` key entirely means "leave this event's pack alone", which is what every format-1 set
means. An entry may carry the shader half alone, with no timing keys.
All optional (omit = inherit from parent path). Paths: any built-in event path from
`libs/phosphor-animation/src/profilepaths.cpp` (see animations.md table; parents like
`window.appearance`, `window.movement`, `desktop`, `popup`, `osd` are real cascade parents).

### Motion set: rules that bite

A motion set that carries only timing is the single most likely thing to go wrong, and it
fails silently: the set applies, the durations change, and every animation keeps whatever
pack it had. Format 1 (timing only) is still READ for old files, so nothing warns you. The
rules that actually bite:

- **`"version": 2` is mandatory** when any entry carries a `shader` key. A build older than
  format 2 refuses a v2 set outright, which is the correct clean failure. Do not write
  version 1 with shader keys hoping for the best.
- **A `shader` key is only legal on a path the daemon consumes as a shader leg.** The SSOT is
  `shaderConsumedLeafEventPaths()` in `src/core/types/animationshadersupportedpaths.h`, plus
  every ancestor of those leaves. `eventPathSupportsShaderLeg()` refuses anything else, and
  the refusal is of the WHOLE SET on apply and import (`motionsetdomain.cpp`), with a
  `motionset: path carries a shader half but supports no shader leg` warning in the journal.
  Likewise a `shader.effectId` naming a pack this build lacks refuses the whole set. Read that
  header; do not infer the list from the event taxonomy.
- **The pack's `appliesTo` must cover the path's class.** Nothing validates this: an
  `appliesTo: ["desktop"]` pack on `window.appearance.open` is accepted and then never plays.
- **The format permits the pack half ALONE**, which preserves existing timing. For a new
  theme, supply both halves so its reviewed pacing survives application. Use shader-only
  entries when the user asks to retain timing, and record that dependency in the report.
- **No `baseline` key**, at any version.
- Filename MUST be `slugify(name) + ".json"`, 4-space indent, alphabetically sorted keys.

Then VERIFY the file rather than reading it back: for every entry carrying a `shader`, confirm
the path appears in the SSOT header and that the named pack's `metadata.json` `appliesTo`
covers that path's class. A one-off script over the set file is the right amount of effort;
eyeballing a sixteen-entry file is not.

Curve string forms: `"x1,y1,x2,y2"` inline cubic-bezier (what the UI writes),
`"spring:omega,zeta"`, or a bare curve NAME (`"jelly-settle"`) which resolves to a curve file
or built-in easing (`elastic-out`, `bounce-out`, ...). Unknown spec falls back to OutCubic with
a warning, so verify names.

Applying a motion set writes each override's TIMING half into
`Animations.MotionProfileTree` and its pack half into `Animations.ShaderProfileTree`.
Both are config keys, in `PhosphorAnimation::ProfileTree`'s serialized shape:
```json
"Animations": {
  "MotionProfileTree": {
    "baseline": {},
    "overrides": [
      { "path": "window.appearance.open", "profile": { "curve": "spring:14.00,0.55", "duration": 380 } }
    ]
  }
}
```
Write that directly for the same effect as applying the set. Schema v7 and earlier
kept these in `~/.local/share/plasmazones/profiles/<event.path>.json`; those files are
read once on the upgrade and ignored afterwards.

### Decoration set profile fields

```json
"profile": {
    "chain": ["jelly-glass", "border-jelly", "shadow"],
    "parameters": { "jelly-glass": { "contentOpacity": 0.85 } },
    "disabledPacks": []
}
```
`chain` is ordered (first = bottom). A profile must engage at least one of the three fields.
Every pack id in a chain must exist AND belong to the surface family: an unknown id, or a
pointer pack on a surface path, refuses the whole decoration set on apply and import
(`decorationpagecontroller_sets.cpp`), unlike a parameter typo, which is ignored.
Supported surface paths (`libs/phosphor-surface/include/PhosphorSurface/DecorationSupportedPaths.h`,
verified 2026-09-07). Leaves: `window.tiled`, `window.snapped`, `window.floating`, `osd`,
`popup.snapAssist`, `popup.zoneSelector`, `popup.layoutPicker`, `popup.cheatsheet`,
`shell.panel`, `shell.appletPopup`, `shell.phosphor.{bar,popout,osd,notification,picker,lock}`,
and `pointer` (baseline-isolated, pointer packs only; never written by a theme set).
Cascade parents: `window`, `popup`, `shell`, `shell.phosphor`. A theme set writes `window`
(covers all three placement states), `osd`, `popup` or the four popup leaves, `shell.panel`,
`shell.appletPopup`. Re-read that header before writing paths. There are no focused/unfocused
slots; packs read focus themselves via `focusDim()`.
Parameter values must name declared params of that pack. Nothing validates them: an
undeclared id is silently ignored at stage compose (`libs/phosphor-surface/src/surfacechaincompose.cpp`
walks the pack's declared parameters and looks each one up in the override), so a typo
leaves the default in place with no warning.

## Curves

```json
{
  "name": "jelly-settle",
  "displayName": "Jelly settle (spring, two overshoots)",
  "typeId": "spring",
  "parameters": { "omega": 14, "zeta": 0.55 }
}
```
- `name` == file stem, and must NOT collide with a built-in typeId. The built-ins are exactly
  `bezier`, `cubic-bezier`, `spring`, `elastic-in`, `elastic-out`, `elastic-in-out`,
  `bounce-in`, `bounce-out`, `bounce-in-out`.
- `typeId` `spring` takes `omega`, `zeta`; `cubic-bezier` takes `x1,y1,x2,y2`. Params numeric
  only, and EVERY one is optional with a silent default (spring omega 12 / zeta 0.8;
  cubic-bezier 0.33, 1, 0.68, 1). A misspelled key is ignored without a warning and the curve
  becomes the default, so after writing a curve confirm each intended key by name (grep), or
  check that the produced curve differs from the default.
- Bundled curves go in `data/curves/`; two per theme is the phosphor convention
  (`<theme>-settle` for arrivals, `<theme>-release` for departures).

## Animations.ShaderProfileTree (config.json)

```json
"Animations": {
  "ShaderProfileTree": {
    "baseline": {},
    "overrides": [
      { "path": "window.appearance.open",  "profile": { "effectId": "jelly-bloom", "parameters": { "wobble": 0.8 } } },
      { "path": "window.appearance.close", "profile": { "effectId": "jelly-bloom" } },
      { "path": "window.movement",         "profile": { "effectId": "jelly-stretch" } },
      { "path": "window.movement.move",    "profile": { "effectId": "jelly-wobble" } },
      { "path": "desktop.switch",          "profile": { "effectId": "desktop-jelly" } }
    ]
  }
}
```
The settings app refuses an `effectId` that is not in the registry. It does NOT check that
the pack's `appliesTo` covers the path's class (the picker merely filters incompatible packs
out of its list), so check that yourself: a hand-written override pairing a `desktop` pack
with `window.appearance.open` is accepted and then never plays. Only leaf paths from
`shaderConsumedLeafEventPaths()` and their ancestors survive; `shell.*` is isolated.

Here `baseline: {}` IS correct (config tree), unlike set files where it is refused.

## How to apply

A motion set now carries BOTH halves of every event it covers (the pack and the timing),
and a decoration set has always carried a whole surface. So the normal route is two clicks
and no file surgery:

1. Write the set files to `~/.local/share/plasmazones/{motionsets,decorationsets,overlaysets}/`.
2. Settings → Appearance → Animations → Library → Motion Sets → Apply, Settings → Appearance →
   Decorations → Library → Decoration Sets → Apply, and Settings → Appearance → Overlays →
   Library → Overlay Sets → Apply. Apply is a pending settings change in each of the three, so it needs the page's
   Apply button as well.

Since schema v8 the zone overlay shader is a set like the other two. It is no longer a
per-layout property of the layout file: the assignment lives in `Overlays.OverlayShaderTree`
in config.json, as one global baseline plus a per-layout override keyed by layout UUID. An
overlay set carries the baseline and every override together.

Overlay `shaderId` is the registry's braced UUID, not the metadata slug used by animation
and surface packs. The parser in `libs/phosphor-shaders/src/shaderregistry_parse.cpp`
derives it with UUIDv5 from `shaderNamespaceUuid()` and the metadata `id`. Resolve it
through the registry or that parser before writing a set. Encode the global default as
an `overrides` entry with `path: "overlay:global"` and
`profile: { "shaderId": "<registry UUID>", "parameters": {} }`, never an envelope
`baseline` key. No two overrides may name the same path. A profile that omits `shaderId`
is the legal "suppress the inherited shader" node, not an error.

A per-layout override names a layout by UUID, and UUIDs are per-installation, so an overlay
set generated without knowing the target machine's layout ids should carry the BASELINE only.
Applying a set merges, and it skips overrides for layouts the machine does not have rather
than refusing the whole file. A set naming a shader pack the machine lacks is refused
outright, so only name packs you know are installed.

### Headless, when the user wants no GUI

The settings app owns config.json while it is open.
1. Confirm `plasmazones-settings` is not running.
2. Merge (never replace) the override arrays into `Animations.ShaderProfileTree`
   (packs), `Animations.MotionProfileTree` (timing) and
   `Decorations.DecorationProfileTree`, keyed by `path`, using a small python script in the
   scratchpad. Keep every unrelated key byte-identical.
3. Nothing watches config.json. Tell the daemon to re-read it (verified on the live bus,
   `src/dbus/settingsadaptor/settingsadaptor.cpp` `reloadSettings` calls `Settings::load()`):
   ```bash
   gdbus call --session --dest org.plasmazones --object-path /PlasmaZones --method org.plasmazones.Settings.reloadSettings
   ```
   The daemon then broadcasts `settingsChanged`, and the KWin effect re-reads the decoration
   tree over D-Bus on that signal. Confirm with `journalctl --user -f | grep -i plasmazones`
   before claiming the theme is applied.

Report which route you used, and say plainly if you could not run it.
