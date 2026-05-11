<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-overlay

> Per-screen layer-shell shell hosts, slot vocabulary, and animator-config
> wiring for multi-slot overlays.

## Responsibility

Helper library on top of [`phosphor-layer`](../phosphor-layer/README.md),
[`phosphor-surfaces`](../phosphor-surfaces/README.md), and
[`phosphor-animation`](../phosphor-animation/README.md). Given a
consumer-provided surface factory, `ShellHost` owns the per-screen
layer-shell shell lifecycle (create / destroy / rekey), the slot map
keyed by consumer-chosen slot names, animator-driven slot hides, and
per-role animator-config registration. The library does not know about
zones, layouts, or any specific content: consumers wire their own
slot vocabulary through callbacks.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorOverlay::ShellHost`             | Owns the per-screen `ShellState` map, lifecycle methods, animator wiring. |
| `PhosphorOverlay::ShellState`            | Per-screen mechanism state: layer-shell surface, QQuickWindow, physical screen, slot map. |
| `PhosphorOverlay::SlotEntry`             | One slot entry: `QPointer<QQuickItem> item` + `PhosphorLayer::Role role`. |
| `PhosphorOverlay::makePerInstanceRole()` | Build a per-instance Role by appending `-{screenId}-{generation}` to a base scope prefix. |

## Typical use

```cpp
using namespace PhosphorOverlay;

ShellHost host;
host.setSurfaceAnimator(&animator);                          // from phosphor-animation

host.setSurfaceFactory([&](const QString& sid, QScreen* s) {
    const auto role = makePerInstanceRole(MyShellBaseRole, sid, mgr.nextScopeGeneration());
    return mgr.createSurface(/* role, qml url, screen, ... */);
});

host.setPostCreateCallback([&](const QString& sid, ShellState& state) {
    // Look up content slot Items by their QML object names, wire QML
    // signals, prime the RHI pipeline. Insert one SlotEntry per slot:
    auto* item = qvariant_cast<QQuickItem*>(state.shellWindow()->property("osdSlotItem"));
    state.slots.insert(QStringLiteral("osd"), SlotEntry{item, MyOsdRole});
});

host.setPreDestroyCallback([&](const QString& sid) {
    // Drop any parallel consumer-side per-screen state so a stale signal
    // firing during teardown doesn't dereference a dangling pointer.
});

// Lifecycle:
host.registerConfigForRole(MyOsdRole, buildOsdConfig());
host.ensureShell(screenId, physScreen);
host.syncSurfaceState(screenId, /*anyVisible=*/true, /*anyInputGrabbing=*/false);
host.hideSlot(screenId, QStringLiteral("osd"), [&]{ /* on hide-leg settle */ });
host.destroyShell(screenId);
```

## Design notes

- **Stable pointers via raw owning pointers.** `ShellState` entries are
  heap-allocated; the host stores `QHash<QString, ShellState*>`.
  Consumers cache the `ShellState*` returned by `ensureShell` /
  `stateFor` in parallel per-screen state and rely on the pointer
  staying valid across rehashes. `std::unique_ptr` cannot live in
  `QHash` (Qt 6's hash requires copy-constructible values);
  `std::shared_ptr` would add refcount overhead the lib does not need
  (there is one owner, the host, plus any number of borrowed observers).
- **Callback-driven content boundary.** Three function-object hooks
  (`SurfaceFactory`, `PostCreateCallback`, `PreDestroyCallback`) carry
  every consumer-specific decision: which role / qmlSource the surface
  uses, which slot QML object names to look up, which signal handlers
  to wire, which parallel content state to clear at teardown. The
  library owns surface lifecycle; the consumer owns content.
- **Slot role pinned at wire time.** Each `SlotEntry` carries the
  `PhosphorLayer::Role` the slot animates as, so `hideSlot` can drive
  `SurfaceAnimator::beginHide` without the caller re-specifying the
  role at every dismiss / cancel call site.
- **`syncSurfaceState` is mechanism-only.** Consumers compute
  `anyVisible` and `anyInputGrabbing` from their content slot
  visibility (e.g. modal vs non-modal classification); the library
  decides surface mapping + `Qt::WindowTransparentForInput` toggling.
- **Sticky per-screen creation failure.** When the surface factory
  returns nullptr for a screen, the host flags it. Subsequent
  `ensureShell` calls short-circuit until `clearFailure(screenId)` is
  called: typically on hot-plug. `failureScreenIds()` exposes the
  current set so consumers with their own id grammar (e.g. virtual-
  screen prefixes) can clear by prefix without the library learning
  the grammar.

## Dependencies

- `QtCore`, `QtQuick`
- [`phosphor-layer`](../phosphor-layer/README.md): `Role`, `Surface`,
  wlr-layer-shell wire primitives.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md) -
  UI-pattern recipes the consumer composes base roles from.
- [`phosphor-surfaces`](../phosphor-surfaces/README.md): managed
  surface lifecycle + scope-generation counter.
- [`phosphor-animation`](../phosphor-animation/README.md) -
  `SurfaceAnimator` that drives every overlay show / hide leg.
- [`phosphor-screens`](../phosphor-screens/README.md): screen
  topology + stable identifiers (consumer-facing dep; the lib itself
  only forward-declares `QScreen`).

## See also

- [`phosphor-layer`](../phosphor-layer/README.md): lower-level
  surface / role primitives this library coordinates.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md) -
  axis-2 UI-pattern recipes consumers use to build their base roles.
