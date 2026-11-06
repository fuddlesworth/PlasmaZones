---
name: pz-algorithm-api
description: "Extend the Luau tiling-algorithm API: a new ctx input, a new tile() or hook output, a new pluau helper, or a new metadata field or capability flag. Covers pluau.luau, pluau.d.luau, LuauTileAlgorithm marshalling and validation, the capability chain out to the picker, D-Bus and the scaffold, limits, caching, docs and tests. Use when: new ctx field, pluau helper, pluau.d.luau, algorithm metadata, capability flag, customParams, onWindowResized output, LuauTileAlgorithm, tiling script API."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Extend the tiling-algorithm API

A script is `return pluau.algorithm { metadata = {...}, tile = function(ctx) ... end }` plus optional hooks.
`LuauTileAlgorithm` (`phosphor-libs/libs/phosphor-tiles/src/luautilealgorithm.cpp`) owns the whole contract:
`parseMetadata`, `buildContext`, `buildStateMap`, `variantToRects` and `onWindowResized` all read or write a fixed list
of keys. **A key that list does not name is dropped with no message**, in both directions. `pluau.d.luau` (same lib,
`src/pluau/`) is the author-facing contract, and nothing checks it against the runtime. CI's `luau-analyze` job
checks the stub's syntax and internal consistency only (its own comment says agreement is reviewed by hand), and
`plasmazones/data/algorithms/.luaurc` declares `pluau` only by name in nonstrict mode, so the analyzer does not hold
an algorithm's helper calls to the stub.

Follow a finished example end to end: `grep -rn supportsSingleWindow phosphor-libs plasmazones/src plasmazones/dbus`.

## A new input (a ctx field)

1. **The field.** Add it to `TilingParams` in `include/PhosphorTiles/TilingParams.h`, or derive it from `params.state`.
2. **Every builder.** The live path fills it in `phosphor-tile-engine/src/autotileengine/layout_apply.cpp`. The two
   preview paths, `AutotileLayoutSource` (`autotilelayoutsource.cpp`) and the settings app's
   `AlgorithmService::generateAlgorithmPreview` (`plasmazones/src/settings/services/algorithmservice.cpp`), start
   from `TilingParams::forPreview` and set only `customParams`. A new field is absent there, so `tile()` must work
   without it. Declare it optional (`?`) in the `Context` type unless `buildContext` always sets it.
3. **Marshal it** in `buildContext`. `Marshal::pushVariant` (`phosphor-scripting/src/luaumarshal.cpp`) knows bool,
   the integer and float types, `QString`, `QStringList`, `QVariantList` and `QVariantMap`. Any other type becomes a
   number if `canConvert<double>()`, otherwise nil. Convert a `QRect` with `rectToVariant`. Cap a per-window
   list at `MaxZones`, as `marshalWindowList` does. Indices handed to scripts are 0-based tiled indices.
4. **Hooks see a different table.** `onWindowAdded`/`Removed`/`Resized` get `buildStateMap` (`HookState`), not `ctx`.
   Add the field there too if a hook needs it, and declare it in `HookState`.

## A new output

- **tile()** returns an array of `{x, y, width, height}` in **pixels** inside `ctx.area`, not relative 0..1.
  `variantToRects` reads those four keys with `toInt()` and nothing else, truncates past `MaxZones`, and
  `clampZonesToArea` replaces an out-of-area zone with the full area. `layout_apply.cpp` refuses the whole layout
  (a warning only) when the zone count differs from `windowCount`. A table with both an array part and string keys
  marshals as the list and loses the string keys (`luaumarshal.h`), so an extra field cannot ride on the zones
  array. Changing the return shape means changing `variantToRects` and `calculateZones`.
- **onWindowResized** returns a control table. `splitRatio` is a reserved key, applied after an
  `isNumericMetaType` + `std::isfinite` gate. Every other key becomes the script-state bag, but only with
  `supportsScriptState`, after `TilingState::sanitizeScriptState` (the `ScriptStateMax*` caps in
  `AutotileConstants.h`). A new reserved key must be read there **and** removed before the bag is stored, as
  `splitRatio` is, or it round-trips into `ctx.state`. Document it in the `Algorithm` comment in `pluau.d.luau`.
- `onWindowAdded`/`onWindowRemoved` return values are ignored.
- Validate untrusted values the way the file does. `toDouble()`/`toInt()`/`toBool()` turn nil or a table into a
  plausible 0 or false, so check the metatype first (`finiteNumber`, `resolveBool`), then range, then log.

## A new pluau helper

No C++ function is bound into the VM. Every helper is plain Luau in `src/pluau/pluau.luau`, compiled into the
library through `src/pluau.qrc`. A host-implemented function would need new `LuauEngine` API, because `lua_State`
is private to phosphor-scripting.

1. Write it as a `local function` in `pluau.luau`, then add it to the `pluau = { ... }` export table at the end.
   One that is not exported is nil to scripts, so `tile()` errors and returns no zones.
2. Declare it in the `Pluau` type in `pluau.d.luau`. Take `area: Zone`, not `Rect`, unless it calls a Rect
   method (the comment above the geometry builtins explains why).
3. Constants at the top of `pluau.luau` mirror `AutotileConstants.h` by hand. `MIN_ZONE_SIZE` is also
   `PhosphorGeometry::GeometryDefaults::MinRectSizePx`, and all three move together.
4. The prelude runs before `sandbox()` freezes globals. A script cannot add a helper at load time.
5. Test it in `plasmazones/tests/unit/scripting/test_pluau_helpers.cpp`, which loads the source `pluau.luau`. An
   edit to a helper the bundled algorithms use must keep `test_luau_parity` green. Regenerate its golden file with
   `P_GENERATE_GOLDEN=1` only for an intended output change.

## A new metadata field or capability

1. **Declare** it in the `Metadata` type in `pluau.d.luau`, with its default and clamp in the comment.
2. **Store** it in `ScriptMetadata` (`include/PhosphorTiles/TileScriptMetadata.h`).
3. **Parse** it in `parseMetadata`. Numbers go through `finiteNumber`/`readInt`. A missing bool is false, so a
   default-true flag needs the `m.contains()` form `supportsMinSizes` uses. Check a string against its allowed values
   and log anything else, as `overlapStacking` does in `cacheMetadataAndOverrides`.
4. **Override function**, only if a script may compute it: a `resolveInt`/`resolveBool`/`resolveReal` call in
   `cacheMetadataAndOverrides`, and an entry in the `Algorithm` type. It runs once at load and is cached.
5. **Accessor.** A virtual on `TilingAlgorithm` (`TilingAlgorithm.h` and `tilingalgorithm.cpp`) and the
   `LuauTileAlgorithm` override. Follow the noexcept rule in the comment in `TilingAlgorithm.h`.
6. **To the picker.** `AlgorithmMetadata` (`phosphor-layout-api/include/PhosphorLayoutApi/AlgorithmMetadata.h`),
   `buildMetadata` in `autotilelayoutsource.cpp`, `writeAlgorithmFlat` in
   `plasmazones/src/common/layoutpreviewserialize.cpp`, then the QML (`plasmazones/src/shared/CapabilityBadgeRow.qml`,
   the filters in `plasmazones/src/settings/qml/pages/layouts/LayoutBrowserPage.qml`).
7. **To D-Bus `algorithmInfo`**, if needed. Change all of these together, or the bus round trip breaks:
   `AlgorithmInfoEntry` and its signature comment (`phosphor-protocol/include/PhosphorProtocol/AutotileTypes.h`),
   both operators in `phosphor-protocol/src/marshalling.cpp`, the `type` and DocString in
   `plasmazones/dbus/org.phosphor_works.Placement.Autotile.xml`, the fill in `plasmazones/src/dbus/autotileadaptor/autotileadaptor.cpp`,
   and the signature assertion in `plasmazones/tests/unit/compositor-common/test_wire_types.cpp`.
8. **The new-algorithm wizard**, for a capability toggle: `Capabilities` in
   `plasmazones/src/settings/services/algorithmscaffold.h`, the writer in `algorithmscaffold.cpp`, the read in
   `algorithmservice.cpp`, and `plasmazones/src/settings/qml/pages/tiling/NewAlgorithmDialog.qml`.
9. **Bundled algorithms.** Set the flag where it is true. It must match what `tile()` does
   (`pz-luau-algorithm-reviewer` checks this).

`name`, `description` and each `customParams` `description` are user-facing prose under the CLAUDE.md plain-prose
rules. `check-conventions.py` only reaches double-quoted `description = "..."` in `plasmazones/data/algorithms/*.luau`.
Check `name`, other quoting styles and the scaffold's generated text yourself.

**customParams.** `parseCustomParam` reads `name`, `type`, `default`, `description`, `min`, `max` and `options`.
`CustomParamDef::toVariantMap` keys are mirrored by `ParamDefKeys` in
`plasmazones/src/settings/pages/tilingalgorithmcontroller.cpp`. The host never puts `default` into `ctx.custom`:
`layout_apply.cpp` passes only saved values, filtered by `hasCustomParam` and not range-checked. A script repeats its
default and clamps the value itself (`theater.luau`: `pluau.clamp(ctx.custom and ctx.custom.widthRatio, ...) or 0.6`).

## Sandbox, limits, errors

- Limits: `ScriptWatchdogTimeoutMs` (100) per call, `ChunkTimeoutMs` (1000, `luauengine.cpp`) for the module chunk,
  `LuauEngine::DefaultMemoryCapBytes` (64 MiB), `MaxScriptSizeBytes` (1 MiB), `MaxZones` (256),
  `MaxRuntimeTreeDepth` (50), and `Marshal::MaxDepth`/`MaxNodes`/`MaxBytes` on every result. Put a new limit in
  `AutotileConstants.h` and state it with units in the docs.
- Bundled scripts share one VM (`ensureSharedEngine`). Each user script gets its own capped VM.
- Every failure is a `qCWarning(lcTilesLib)` with `script=` and a fallback, never a user-visible error. A failed
  `tile()` gives no zones. Keep that shape: log, fall back to the default, never coerce a bad value into range.

## Caching and reload

- Metadata and override results are cached once at load (`cacheMetadataAndOverrides`). Nothing re-reads per retile.
- `ScriptedAlgorithmLoader` reuses a loaded algorithm whose file kept its (size, mtime), so `touch` the file in a
  manual test. The daemon (`init_adaptors.cpp`) and the settings app (`settingscontroller.cpp`) each run their own
  loader and registry. `AutotileLayoutSource` drops its preview cache when the registry changes.
- `pluau.luau` is a compiled-in resource: a running daemon sees an edit only after rebuild, install and restart.
  Tests read algorithms and `pluau.luau` from the source tree, the daemon reads the installed copies.

## Documents and tests

- `pluau.d.luau` comments, `phosphor-libs/libs/phosphor-tiles/README.md` for structural changes, `CHANGELOG.md`, and
  the authoring guide in the sibling repo `../phosphor-works.github.io/src/pages/guides/tiling.astro` (its own commit).
- Tests under `plasmazones/tests/unit/`: `scripting/test_luau_tile_algorithm.cpp` (metadata parsing, clamps,
  dropped and malformed values, hooks, custom params), `scripting/test_pluau_helpers.cpp`, `scripting/test_luau_parity.cpp`,
  `autotile/algorithms/test_algorithm_registry.cpp` and `test_scripted_algorithm_loader.cpp`,
  `core/layout/test_layout_preview_serialize.cpp`, `compositor-common/test_wire_types.cpp`, and
  `settings/services/test_algorithm_*scaffold*.cpp`.
- Build and run with the `pz-build` skill. To narrow the run, filter ctest with
  `-R 'luau|algorithm|tiling_algo|wire_types|layout_preview_serialize'`. Run `python3 scripts/check-conventions.py` too.
