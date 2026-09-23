<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-screens — CHANGES

This file documents user-visible behaviour changes (especially QML-payload
shape changes) so consumers can audit their binding code on upgrade. Library
SOVERSION still follows semver; entries here flag the kind of behaviour
churn that won't be caught by an ABI break.

## Phase: IScreenProvider renamed to IPhysicalScreenSource

`PhosphorScreens::IScreenProvider` is now `PhosphorScreens::IPhysicalScreenSource`,
and its header moved from `IScreenProvider.h` to `IPhysicalScreenSource.h`. The
interface is otherwise unchanged. No method signatures, signals or semantics
moved with the rename.

The old name collided with the unrelated `PhosphorLayer::IScreenProvider`. The
two are different seams at different layers and neither library links the other,
so the collision never reached a compiler, but it did mean a search for
`IScreenProvider` returned both hierarchies as though they were one. This one
answers which physical outputs exist and what just happened to them, in
`PhysicalScreen` value snapshots. The layer one answers which `QScreen*` a
surface should attach to. Deliberately not merged. This interface avoids
handing out `QScreen*` precisely so a test can synthesise outputs, which is the
whole reason the seam exists.

The implementations and the injection point follow the interface, matching how
`IPanelSource` and `IConfigStore` name theirs in this library:

| Old                                  | New                                        |
|--------------------------------------|--------------------------------------------|
| `IScreenProvider`                    | `IPhysicalScreenSource`                    |
| `QtScreenProvider`                   | `QtPhysicalScreenSource`                   |
| `FakeScreenProvider` (test double)   | `FakePhysicalScreenSource`                 |
| `ScreenManagerConfig::screenProvider`| `ScreenManagerConfig::physicalScreenSource`|

**Action for downstream consumers:** this is a source-compatibility break with
no deprecated alias. Update the type name, the header include, and any
designated initialiser that sets the config field. Every use is a compile
error until it is updated, so nothing fails silently at runtime.

## Phase: Core split into Core (POD) + Runtime (ScreenManager)

`PhosphorScreens::Core` previously bundled the pure POD/serialiser surface
(`ScreenInfo`, `ScreenIdentity`, `Swapper`, `QtScreenProvider`, `VirtualScreen`,
the config/panel interfaces) together with the live `ScreenManager`, whose
LayerSurface-backed geometry sensors pull in `PhosphorWayland`. Every consumer
that linked `::Core` therefore acquired `PhosphorWayland` at runtime even if it
only touched POD types.

### New target layering

`ScreenManager` (and its header `Manager.h`) moved to a new
`PhosphorScreens::Runtime` target. `::Core` is now pure POD — Qt6::Core/Gui only,
no Wayland.

| Consumer need                                              | Link target                |
|------------------------------------------------------------|----------------------------|
| Only POD types (ScreenInfo/ScreenIdentity/VirtualScreen/…) | `PhosphorScreens::Core`     |
| Drives a `ScreenManager` (`Manager.h`)                     | `PhosphorScreens::Runtime`  |
| D-Bus surface (DBusScreenAdaptor/Resolver/PlasmaPanelSource)| `PhosphorScreens::PhosphorScreens` |

`Runtime` PUBLIC-links `Core`, and the umbrella `PhosphorScreens` now links
`Runtime`, so existing consumers of those two targets are unaffected.

**Action for downstream consumers:** a target that previously linked
`PhosphorScreens::Core` *and* constructs/calls a `ScreenManager` must re-point to
`PhosphorScreens::Runtime`. Consumers that only use POD types stay on `::Core`
and shed the `PhosphorWayland` dependency. `Manager.h`'s export macro changed
from `PHOSPHORSCREENSCORE_EXPORT` to `PHOSPHORSCREENSRUNTIME_EXPORT` (this macro
name is internal to the header — no source change at call sites).

## Phase: phosphor-control scaffolding

Context: lift-and-shift refactor extracting common QML/Qt6 settings
chrome and helpers (see `phosphor-libs/libs/phosphor-control/`). The phosphor-screens
side picked up several QML-payload tweaks in the same window so the new
settings chrome could render screen pickers from the library's POD without
duplicating label-building logic in QML.

### Namespace flattened: `Phosphor::Screens` → `PhosphorScreens`

`phosphor-screens` previously declared its types in the nested namespace
`Phosphor::Screens`. To align with `phosphor-control` (which uses the
single-level `PhosphorControl`) and to keep consumer code symmetric
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

Five changes shipped together in the same commit window. QML consumers
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

4. **`x` / `y` screen-space position are now emitted (always).**

   Each map carries the output's top-left position in the compositor's
   global coordinate space, so consumers can lay outputs out in their real
   arrangement (e.g. a proportional multi-monitor map). Unlike `width` /
   `height`, position has no sentinel: `0` is a valid origin and negative
   coordinates are normal for outputs placed left of / above the primary,
   so `x` / `y` are emitted unconditionally — they are always present.

5. **`displayLabel` is a precomputed, always-present label string.**

   A single source-of-truth label the serializer builds so QML consumers stop
   duplicating label logic: for a physical screen, vendor + model (falling back
   to the output name); for a virtual screen, `VS<n> — <monitor>`. Both gain a
   ` (W×H)` suffix when the resolution is known and a ` · <connector>` suffix to
   disambiguate otherwise-identical panels. Consumers should bind `displayLabel`
   rather than reassembling the name / manufacturer / model / resolution fields
   by hand.

QML consumers that walked the variant map by key set (rather than by
known-key lookup) are the most likely to regress.
