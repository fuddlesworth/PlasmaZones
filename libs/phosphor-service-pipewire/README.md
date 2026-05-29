<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-pipewire

Native PipeWire mixer (sinks / sources / per-app streams, default-node switching, volume + mute) for Phosphor-based desktop shells.

## Responsibility

Owns the PipeWire main loop on a dedicated `QThread`, walks the `pw_registry`, and surfaces sinks / sources / streams as Qt + QML types. No UI; the shell decides how a volume mixer, OSD, or sound preferences page is rendered.

The library is non-visual (no `Qt::Gui` link) and single-process by design (the loop runs inside the same `QGuiApplication` that hosts the shell). All cross-thread plumbing is hidden: consumers see a pure GUI-thread API backed by queued signals from the loop thread.

## Key types

| Type                  | Role |
|-----------------------|------|
| `PipeWireConnection`  | Lifecycle owner. Holds the `pw_main_loop` thread, `pw_context`, `pw_core`. Exposes `connected` / `daemonAvailable` properties + an `error(QString)` signal. Higher-level models (sinks / sources / streams) hang off this object in later milestones. |

(Milestones 3-5 add `PwNode`, `PwSinkModel`, `PwSourceModel`, `PwStreamModel`. Milestone 6 adds the WirePlumber-backed default-node switching helper. See `docs/phosphor-shell-design/04-implementation-plan.md` § 2.1.)

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServicePipeWire/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServicePipeWire::registerQmlTypes();
    // ... QQmlApplicationEngine engine; engine.load(shellUrl); etc.
    return app.exec();
}
```

QML consumer:

```qml
import Phosphor.Service.PipeWire 1.0

PipeWireConnection {
    id: pwHost
    Component.onCompleted: connect()
    onError: (msg) => console.warn("PipeWire:", msg)
}

Text { text: pwHost.connected ? "PipeWire ready" : "Connecting…" }
```

## Design notes

- **Threading.** PipeWire's `pw_main_loop` is not a Qt event loop. Every PipeWire API call must happen on the loop's thread. The library owns one `QThread` named `PipeWireLoop` whose `run()` body calls `pw_main_loop_run`; all PipeWire work is dispatched onto that thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Events from PipeWire (core info, done, error) bounce back to the GUI thread the same way before mutating any state visible to QML bindings. Consumers never see a non-GUI-thread signal emission.
- **Pimpl + public-header purity.** `PipeWireConnection.h` is libpipewire-free; every `pw_*` type lives in `src/`. Consumers can `target_link_libraries(... PhosphorServicePipeWire::PhosphorServicePipeWire)` without dragging `libpipewire-0.3` into their own include set. Mirrors how `phosphor-service-sni` keeps `dbustypes.h` private.
- **No auto-reconnect.** Daemon restarts (driver hotplug, kernel module reload) fire `error(QString)` and flip `connected` to false. The shell decides on a backoff and calls `connect()` again. Auto-retry policy in a system library would fight every consumer's chosen recovery scheme; keep it explicit.
- **WirePlumber default-node switching** lands in milestone 6 via the `wp_default_nodes_api` metadata path; it is the only supported mechanism for changing the default sink / source on the public surface. Raw `pw_metadata` is left as a private fallback if WirePlumber is absent (logged), not advertised.

## Dependencies

- `libpipewire-0.3` (PipeWire 1.0+ baseline, U1 resolution)
- `libwireplumber-0.5` (added in milestone 6 for default-node switching)
- Qt6 ≥ 6.6 (Core, Qml)
- A running PipeWire daemon at consume time (the lib loads inert without one and reports `daemonAvailable=false`)

## Status

Phase 2.1: in flight on `feat/phase-2.1-pipewire-service`. Milestones 1 (skeleton + CMake plumbing) and 2 (`PipeWireConnection` lifecycle) have landed; sinks / sources / streams models (M3-5), default-node switching (M6), CLI demo (M8), and the rest of the API surface are next. See `docs/phosphor-shell-design/04-implementation-plan.md` § 2.1 for the full milestone list, dependencies, risks, and U1-U6 unknowns.
