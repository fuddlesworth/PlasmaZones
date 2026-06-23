<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-shortcuts

> Pluggable global-shortcut backends behind a single registration API:
> KGlobalAccel (Plasma-native), XDG Portal
> (`xdg-desktop-portal.GlobalShortcuts`), and a D-Bus ad-hoc fallback.

## Responsibility

"Super+1 snaps to zone 1" needs to register a global shortcut from a
userspace process. On Plasma that goes through KGlobalAccel. On generic
Wayland compositors that support the portal, it goes through
`org.freedesktop.portal.GlobalShortcuts`. On compositors with neither,
there's no standard, so the ad-hoc D-Bus fallback exists for
compositor-script or compositor-provided shortcut dispatch.

The consumer shouldn't care which backend is active. `phosphor-shortcuts`:

- Defines a **single registration interface** (`Registry::bind`) for a
  shortcut id, default sequence, description, and callback.
- Dispatches through a **pluggable backend** (`IBackend`) that the
  application chooses at startup, or auto-selects by probing available
  services.
- Provides a **factory helper** (`createBackend(BackendHint)`) that builds
  the right backend given a compositor or desktop hint.
- Separates "system shortcuts" (persistent, configurable per-user) from
  **ad-hoc registrations** (`IAdhocRegistrar`) for transient UI like
  modal-capture dialogs.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorShortcuts::Registry`        | Front-end: `bind()` a shortcut id and callback |
| `PhosphorShortcuts::IBackend`        | Abstract backend. Shipped implementations: KGlobalAccel, XDG-Portal, D-Bus |
| `PhosphorShortcutsIntegration::IAdhocRegistrar` | Short-lived registrations that skip persistent storage |
| `PhosphorShortcuts::createBackend`   | Selects the right backend based on running environment |

## Typical use

```cpp
#include <PhosphorShortcuts/Registry.h>
#include <PhosphorShortcuts/Factory.h>

using namespace PhosphorShortcuts;

auto backend = createBackend();  // picks KGlobalAccel on Plasma
Registry registry(backend.get());

registry.bind(
    /*id*/          QStringLiteral("snap-to-quick-1"),
    /*defaultSeq*/  QKeySequence(QStringLiteral("Meta+1")),
    /*description*/ tr("Snap active window to Quick Layout 1"),
    /*callback*/    [] { engine.snapTo(1); },
    /*persistent*/  true
);
```

Switching backends is a one-liner. The `Registry` API stays the same, and you
pass a different `IBackend`.

```cpp
auto portalBackend = createBackend(BackendHint::Portal);
Registry adhoc(portalBackend.get());
adhoc.bind(id, seq, desc, cb, /*persistent*/ false);
```

## Design notes

- **No direct KGlobalAccel include in callers.** Callers never see the
  Plasma-specific API. This makes the whole suite compilable without KF6
  by swapping in a portal-only backend build.
- **`IAdhocRegistrar` is a separate interface.** Binds that should never
  end up in the user's persistent shortcut editor live behind a different
  API, so the backend only persists the ones that asked for it.
- **`createBackend(BackendHint::Auto)`** probes at runtime. First preference is
  KGlobalAccel (fastest, native Plasma integration), then the
  portal, and last D-Bus.

## Dependencies

- `QtCore`, `QtGui` (for `QKeySequence`), `QtDBus`
- **Optional**: `KF6::GlobalAccel` (Plasma backend), compiled in
  when `-DPHOSPHORSHORTCUTS_USE_KGLOBALACCEL=ON`
