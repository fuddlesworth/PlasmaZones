<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-ipc

> Typed JSON-over-Unix-socket invocation channel for
> phosphor-shell. Lets compositor keybinds, external scripts, and
> other shells drive Phosphor actions by verb (`phosphorctl call
> launcher.toggle`) without rebuilding the existing D-Bus surface.

## Responsibility

Modern Wayland shells (DMS, Noctalia, Quickshell) all ship a typed
CLI built on a single socket, with JSON-shaped requests and
schema-described targets. `phosphor-ipc` is Phosphor's equivalent. It
sits **alongside** the existing D-Bus adaptors. Service libraries register both an IPC
target and (where appropriate) a D-Bus method for each callable.

The library owns:

- **`IpcRouter`**: central dispatcher backed by a `QLocalServer`
  listening on `$XDG_RUNTIME_DIR/phosphor.sock`. Registers QObject
  "targets" by name, walks their `QMetaObject` to generate JSON
  schemas, dispatches sync calls via `QMetaMethod::invoke`, and
  routes signal-broadcast events to per-target subscribers.
- **`IpcTarget`**: QML element. Plugin authors declare one per
  exposed verb namespace and put `Q_INVOKABLE`-style functions on it
  directly. `IpcTarget.emitEvent("name", [args])` pushes a JSON
  event to every subscriber on (target, name).
- **`IpcSchemaGenerator`**: `QMetaObject` → JSON Schema. Every
  documented QMetaType maps to a JSON Schema fragment, and unknown types
  degrade to `{"description": "<QMetaType::name>"}` without a `type`
  constraint.
- **`IpcEngine::install`**: small bridge that stashes the
  application's `IpcRouter` on the `QQmlEngine` so `IpcTarget`
  instances find it in `componentComplete`.

Ships alongside the **`phosphorctl`** CLI at `cli/phosphorctl/`,
which provides `call`, `list`, `schema`, and `subscribe`
subcommands.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorIpc::IpcRouter` | Per-application dispatcher. Owns one `QLocalServer`, the registry of named targets, and the per-socket subscription set. Single-threaded (GUI thread). |
| `PhosphorIpc::IpcTarget` | QML element. Each instance binds to one target name. Functions declared on the instance become callable, and the explicit `emitEvent(name, args)` pushes wire events. |
| `PhosphorIpc::IpcProtocol` | Wire-format constants + parser/serialiser. Shared between the library (server) and the `phosphorctl` binary (client). |
| `PhosphorIpc::IpcEngine` | Bridge between an application-owned `IpcRouter` and QML-side `IpcTarget` instances. |
| `PhosphorIpc::IpcSchemaGenerator` | `QMetaObject` → JSON Schema. Used by the schema response and by the CLI for client-side argument validation. |

## Wire protocol

One JSON object per line, `\n`-terminated, UTF-8. Same NDJSON shape
as niri-ipc and hyprland-ipc. Stable across protocol version 1.
The version is a build-time invariant only (the
`PHOSPHOR_IPC_PROTOCOL_VERSION` compile definition is cross-checked
by a `static_assert` in `IpcProtocol.h`). It is NOT transmitted on
the wire and there is currently no version-negotiation handshake.
Incompatible bumps surface as malformed-request errors against an
older peer.

### Requests (client → server)

```jsonc
// Sync call
{"type":"call","id":42,"target":"greet","fn":"sayHello","args":["nate"]}
// Enumerate targets
{"type":"list","id":43}
// Schema for one target
{"type":"schema","id":44,"target":"greet"}
// Subscribe to a signal. Server streams events tagged with this id
// until unsubscribe / disconnect
{"type":"subscribe","id":45,"target":"count","signal":"countChanged"}
// Cancel a subscription
{"type":"unsubscribe","id":46,"subscriptionId":45}
```

### Responses (server → client)

```jsonc
{"type":"reply","id":42,"result":"Hello, nate"}
{"type":"event","subscriptionId":45,"args":[7]}
{"type":"error","id":42,"code":"NO_SUCH_TARGET","message":"...","detail":{}}
```

Error codes: `NO_SUCH_TARGET`, `NO_SUCH_FN`, `NO_SUCH_SIGNAL`,
`NO_SUCH_SUBSCRIPTION`, `INVALID_ARG`, `INVOCATION_FAILED`,
`MALFORMED_REQUEST`.

## Typical use

### C++ shell composition root

```cpp
#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    // Declare `router` BEFORE `engine` so reverse-order destruction
    // tears the engine down FIRST; otherwise QML-side IpcTarget
    // instances would unregister against a dead router.
    PhosphorIpc::IpcRouter router;
    if (!router.start()) {  // binds $XDG_RUNTIME_DIR/phosphor.sock
        qWarning("IPC router failed to start "
                 "(XDG_RUNTIME_DIR unset, another live listener on "
                 "the same path, or insufficient permissions)");
        // Application can continue without IPC; failure is non-fatal.
        // The router auto-recovers from STALE socket files (left
        // behind by a crashed previous run) — those don't cause
        // start() failures.
    }

    QQmlApplicationEngine engine;
    PhosphorIpc::IpcEngine::install(&engine, &router);
    engine.loadFromModule(...);  // shell QML
    return app.exec();
}
```

The shell QML can now declare `IpcTarget` instances anywhere in the
component tree, and each one auto-registers via the router stashed on
the engine.

### QML side

```qml
import Phosphor.Ipc
import Phosphor.Theme  // for Theme singleton; omit if not needed

IpcTarget {
    target: "theme"

    signal modeChanged(string mode)  // schema-visible

    function setMode(mode: string) {
        Theme.mode = mode           // apply state mutation
        modeChanged(mode)           // notify in-QML observers
        emitEvent("modeChanged", [mode])  // broadcast on the WIRE
    }
}
```

The schema generator surfaces both `Q_INVOKABLE` methods and
declared signals, so `phosphorctl schema theme` shows the
subscribable surface.

**Calling the QML signal (e.g. `modeChanged(mode)`) does NOT
broadcast to wire subscribers**. It only fires in-QML connections.
Wire delivery happens through `emitEvent(name, args)` exclusively.
The split is by design. Introspecting Qt signals via `qt_metacall`
collides with moc-generated dispatch, and explicit `emitEvent`
matches the "wire-visible state transitions are an explicit
contract" model. Plugin authors must call both for in-QML AND wire
delivery.

### CLI side

```
phosphorctl call theme.setMode --arg mode=dark
phosphorctl list
phosphorctl schema theme | jq
phosphorctl subscribe theme.modeChanged    # stays connected, streams events
```

Socket-path priority: `--socket PATH` > `$PHOSPHOR_SOCKET` >
`$XDG_RUNTIME_DIR/phosphor.sock`.

### Acceptance harness

`examples/phosphor-ipc-demo/` is the canonical end-to-end exercise.
The demo window includes a live event-log panel that mirrors the
broadcast stream. Running `phosphorctl call count.increment` from a
single sidecar terminal makes events appear in the demo window in
real time, with no separate `phosphorctl subscribe` terminal needed:

```
# terminal 1
./bin/phosphor-ipc-demo --socket /tmp/p.sock
# terminal 2
export PHOSPHOR_SOCKET=/tmp/p.sock
phosphorctl call count.increment        # demo's event log gains an entry
phosphorctl subscribe count.countChanged   # optional: same events on stdout
```

## Arbitration / lifecycle

- **Duplicate target id arbitrated.** Re-registering the SAME
  `QObject` under the SAME name is a silent idempotent success (no
  warning, no signal re-emit). Registering a DIFFERENT `QObject`
  under an existing name is rejected with a `qWarning`, and the
  incumbent registration is preserved. `registerTarget` returns
  `bool` so wrappers like `IpcTarget` can detect rejection and
  skip the paired unregister on teardown.
- **Signal name validated at subscribe.** Subscribe rejects with
  `NO_SUCH_SIGNAL` if the signal isn't declared on the target's
  metaobject. Typos surface immediately instead of silently
  ignoring later `emitEvent` calls.
- **Subscription scope is per-socket.** A subscription id is unique
  per connection, not globally. The wire identity is `(socket,
  subscriptionId)`.
- **Disconnect prunes subscriptions.** When a subscriber socket
  disconnects, all its subscriptions are dropped automatically.
- **Stale socket file recovery.** On `start`, if `listen()` fails
  and the socket path is occupied, the router probes with a quick
  connect. If no live listener answers, the stale path is unlinked
  and `listen()` retries. A live listener is left alone (start
  fails cleanly rather than clobbering).
- **No subscribe auto-broadcast from Qt signals.** Plugin authors
  call `IpcTarget.emitEvent("name", args)` explicitly. Firing the
  QML signal (e.g. `modeChanged(...)`) only notifies in-QML
  subscribers, NOT wire subscribers. This split is by design.
  Introspecting arbitrary Qt signals via `qt_metacall` clashes with
  Q_OBJECT moc-generated code, and explicit `emitEvent` matches the
  "wire-visible state transitions are an explicit contract" model.

## Dependencies

- `QtCore` (always)
- `QtGui` (transitive, only via the static QML plugin `PhosphorIpcQml`. The runtime library has no QtGui usage.)
- `QtQml` (for `IpcTarget` QML registration)
- `QtNetwork` (for `QLocalServer` / `QLocalSocket`)

## See also

- `cli/phosphorctl/`: typed CLI shipped alongside the library.
- `examples/phosphor-ipc-demo/`: acceptance harness. Three
  `IpcTarget` instances (greet / count / set-value) exercise the
  full call / list / schema / subscribe wire surface.
