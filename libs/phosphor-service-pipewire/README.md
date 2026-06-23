<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-pipewire

Native PipeWire mixer (sinks / sources / per-app streams, default-node switching, volume + mute) for Phosphor-based desktop shells.

## Responsibility

Owns the PipeWire main loop on a dedicated `QThread`, walks the `pw_registry`, and surfaces sinks / sources / streams as Qt + QML types. There is no UI. The shell decides how a volume mixer, OSD, or sound preferences page is rendered.

The library is non-visual (no `Qt::Gui` link) and single-process by design (the loop runs inside the same `QGuiApplication` that hosts the shell). All cross-thread plumbing is hidden, so consumers see a pure GUI-thread API backed by queued signals from the loop thread.

## Key types

| Type                  | Role |
|-----------------------|------|
| `PipeWireConnection`  | Lifecycle owner. Holds the `pw_main_loop` thread, `pw_context`, `pw_core`. Exposes `connected`, `daemonAvailable`, `defaultSinkName`, `defaultSourceName` properties + `error(QString)`, `nodeAdded(PwNode*)`, `nodeRemoved(PwNode*)` signals. Routes per-node writes (`writeVolumes`, `writeMuted`) and WirePlumber metadata writes (`setDefaultSink`, `setDefaultSource`) onto the loop thread. |
| `PwNode`              | One audio node (sink, source, output stream, or input stream). Exposes `id`, `name`, `nick`, `description`, `mediaClass`, `channelCount`, `volumes[]` (linear amplitudes), `muted`, plus the async setters `setVolume(qreal)`, `setVolumes(QList<qreal>)`, `setMuted(bool)`. Vended by `PipeWireConnection`; QML-uncreatable. |
| `PwNodeModel`         | Abstract `QAbstractListModel` filtering a connection's nodes by `mediaClasses`. 10 pinned role names (9 user roles + `Qt::DisplayRole`): `node`, `id`, `name`, `nick`, `description`, `mediaClass`, `channelCount`, `volumes`, `muted`, plus `Qt::DisplayRole` (`display`) that falls back through nick → description → name. |
| `PwSinkModel` / `PwSourceModel` / `PwStreamModel` | Convenience subclasses with the common filters pre-applied. `PwStreamModel` lumps `Stream/Output/Audio` and `Stream/Input/Audio` together. |
| `PipeWireHost`        | QML singleton (`Phosphor.Service.PipeWire 1.0`) that owns the process-wide `PipeWireConnection` and forwards every observable signal. Auto-connects on construction, and exposes `connectToDaemon()`, `disconnectFromDaemon()`, and `reconnect()` for explicit lifecycle control. |

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

QML consumer (volume slider per sink):

```qml
import Phosphor.Service.PipeWire 1.0

ColumnLayout {
    Repeater {
        model: PwSinkModel { connection: PipeWireHost.connection }
        delegate: RowLayout {
            required property var model
            Label { text: model.display + (model.name === PipeWireHost.defaultSinkName ? " (default)" : "") }
            Slider {
                from: 0; to: 1
                value: model.volumes.length > 0 ? model.volumes[0] : 0
                onMoved: model.node.setVolume(value)
            }
            CheckBox {
                checked: model.muted
                text: "mute"
                onToggled: model.node.setMuted(checked)
            }
        }
    }
}
```

The CLI in `examples/phosphor-service-pipewire-cli/` is the canonical C++ usage example: see `phosphor-service-pipewire-cli list sinks`, `set-volume <target> <pct>`, `set-default-sink <name>`, `mute <target>`, `default`, and the rest of the subcommand surface.

## Design notes

- **Threading.** PipeWire's `pw_main_loop` is not a Qt event loop. Every PipeWire API call must happen on the loop's thread. The library owns one `QThread` named `PipeWireLoop` whose `run()` body calls `pw_main_loop_run` directly (Qt's `exec()` never runs there). Work from the GUI thread reaches the loop via `pw_loop_invoke` (PipeWire's documented MT-safe dispatch). Events from the loop bounce back via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Consumers never see a non-GUI-thread signal emission.
- **Pimpl + public-header purity.** `PipeWireConnection.h`, `PwNode.h`, `PwNodeModel.h`, and `PipeWireHost.h` are libpipewire-free, and every `pw_*` and `spa_*` type lives in `src/`. Consumers can `target_link_libraries(... PhosphorServicePipeWire::PhosphorServicePipeWire)` without dragging `libpipewire-0.3` into their own include set.
- **Linear amplitude on the surface.** `PwNode::volumes` is per-channel linear amplitude `[0.0, 1.0]` (PipeWire's storage format). Cubic / perceptual curves for UI sliders live in a higher layer, and round-trips through the lib stay lossless. (U2 resolution.)
- **No auto-reconnect.** Daemon restarts (driver hotplug, kernel module reload) fire `error(QString)` and flip `connected` to false. The shell decides on a backoff and calls `connectToDaemon()` again. `PipeWireHost.reconnect()` is the QML one-liner.
- **WirePlumber default-node switching** goes through the `default` metadata global. Reads track `default.audio.sink` / `default.audio.source` (the runtime defaults). Writes target `default.configured.audio.sink` / `default.configured.audio.source` (the persistent setting WirePlumber promotes on the next reconcile). The connection silently no-ops the write if no WirePlumber-managed metadata is present. Shells can detect WirePlumber absence by issuing a `setDefaultSink` / `setDefaultSource` write and watching for the echo via `defaultSinkNameChanged` / `defaultSourceNameChanged` within a budget. The CLI's `cmdSetDefault` uses this exact pattern, surfacing "no WirePlumber default metadata observed" when the pre-write value was empty and stayed empty through the deadline (vs. "timed out waiting for WirePlumber echo" when the pre-write value was populated but the write didn't round-trip).
- **Per-listener context for node events.** Each `LoopNode` is passed as the `data` for its own `pw_node_add_listener` registration, so the param/info callbacks can recover both the node id and the owning `Private*` without walking the loop-side hashmap on every event.

## Dependencies

- `libpipewire-0.3` (PipeWire 1.0+ baseline, U1 resolution)
- WirePlumber 0.5 at runtime for default-node switching (lib loads inert without it, and `defaultSinkName` / `defaultSourceName` stay empty)
- Qt6 ≥ 6.6 (Core, Qml)
- A running PipeWire daemon at consume time (the lib loads inert without one and reports `daemonAvailable=false`)

## Status

Shipped. The `PipeWireConnection` lifecycle, the registry observer and `PwNode`, the sink / source / stream models, the volume + mute write path, default-sink / -source switching via WirePlumber metadata, QML registration with the `PipeWireHost` singleton, the CLI acceptance harness, and the smoke + role-pinning tests are all in place.
