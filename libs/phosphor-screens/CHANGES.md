<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-screens — CHANGES

This file documents user-visible behaviour changes (especially QML-payload
shape changes) so consumers can audit their binding code on upgrade. Library
SOVERSION still follows semver; entries here flag the kind of behaviour
churn that won't be caught by an ABI break.

## Phase: phosphor-settings-ui scaffolding

Context: lift-and-shift refactor extracting common QML/Qt6 settings
chrome and helpers (see `libs/phosphor-settings-ui/`). The phosphor-screens
side picked up several QML-payload tweaks in the same window so the new
settings chrome could render screen pickers from the library's POD without
duplicating label-building logic in QML.

### Namespace flattened: `Phosphor::Screens` → `PhosphorScreens`

`phosphor-screens` previously declared its types in the nested namespace
`Phosphor::Screens`. To align with `phosphor-settings-ui` (which uses the
single-level `PhosphorSettingsUi`) and to keep consumer code symmetric
across phosphor-* libs, the entire surface has moved to a single-level
`PhosphorScreens` namespace.

Mechanical equivalence:

| Before                                | After                              |
|---------------------------------------|------------------------------------|
| `namespace Phosphor::Screens { … }`   | `namespace PhosphorScreens { … }`  |
| `Phosphor::Screens::ScreenInfo`       | `PhosphorScreens::ScreenInfo`      |
| `Phosphor::Screens::ScreenIdentity::` | `PhosphorScreens::ScreenIdentity::`|
| `using namespace Phosphor::Screens;`  | `using namespace PhosphorScreens;` |

The CMake target names (`PhosphorScreens::Core`,
`PhosphorScreens::PhosphorScreens`) and the include path
(`<PhosphorScreens/…>`) are unchanged.

### `screenInfoListToVariantList()` payload tweaks

Three changes shipped together in the same commit window. QML consumers
that key off the shape of the emitted `QVariantMap` should be audited:

1. **`width` / `height` are now emitted independently.**

   Previously the emitter dropped both keys when either dimension was
   non-positive. Now each dimension is emitted on its own when it's
   positive. A screen that reports only its width (mid-startup probe,
   partial daemon reply, etc.) will surface a map with `width` set but
   no `height` — QML that binds `map.height` will see `undefined` for
   that row rather than the previous "both keys missing" sentinel.

2. **`resolution` is only emitted when BOTH width and height are positive.**

   The pre-formatted `"<W>×<H>"` string is now strictly a both-positive
   convenience. QML that bound `map.resolution` as a presence flag for
   "have we got geometry?" should switch to checking `map.width > 0 &&
   map.height > 0` (or read `map.resolution !== undefined`, which still
   works because the key remains absent when either dim is missing).

3. **`isVirtualScreen` is ALWAYS present (was: only when true).**

   The flag is now always emitted as a bool. Previously the key was
   omitted entirely for physical screens, which forced QML to test
   `map.isVirtualScreen === true` to dodge `undefined`. Code that uses
   `Object.keys(map).includes('isVirtualScreen')` to disambiguate
   physical-vs-virtual rows will now see the key on every row and must
   switch to testing the boolean value.

QML consumers that walked the variant map by key set (rather than by
known-key lookup) are the most likely to regress.
