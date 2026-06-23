<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-idle

A Wayland idle-management service for Phosphor-based desktop shells.

## Responsibility

Watches the session for inactivity through a configurable multi-stage timeout
policy, and inhibits idle on request. It is the policy layer over the raw Wayland
idle clients in `phosphor-wayland`. It composes them rather than binding the
protocols itself, so its public surface is a clean Qt/QML type with no Wayland
types leaking out. It reports which idle stage is active and lets the shell
decide what each stage does (dim, lock, display-off). No UI is provided here.

- Monitor inactivity as an ordered ladder of stages, each firing after its own
  timeout (`ext-idle-notify-v1`, via `PhosphorWayland::IdleNotifier`).
- Reference-count idle inhibition so callers can keep the session awake while a
  video plays or a long task runs.
- Stay a mechanism rather than a policy. The library ships no default stages.

## Key types

| Type          | Role                                                                                     |
|---------------|------------------------------------------------------------------------------------------|
| `IdleService` | The session idle host. Configure `stages` (`{ name, timeoutMs }` entries); read the live `currentStage` / `idle`; `inhibit()` / `release(cookie)` to reference-count inhibition. A plain instantiable QML type, not a singleton. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceIdle/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceIdle::registerQmlTypes();
    // ... load shell.qml
}
```

QML idle policy (the shell maps each stage to an action):

```qml
import Phosphor.Service.Idle 1.0

IdleService {
    id: idle
    stages: [
        { "name": "dim",  "timeoutMs":  5 * 60 * 1000 },
        { "name": "lock", "timeoutMs": 10 * 60 * 1000 },
        { "name": "off",  "timeoutMs": 15 * 60 * 1000 }
    ]

    onIdled: (stage) => shellPolicy.enter(idle.currentStageName)
    onResumed: shellPolicy.wake()
}

// While a fullscreen video plays, keep the session awake:
//   const cookie = idle.inhibit()
//   ... later ...
//   idle.release(cookie)
```

The CLI doubles as the worked example and the acceptance harness. It logs each
stage as it fires:

```sh
# watch a 5s 'dim' and a 10s 'lock' stage; leave input idle to fire them
phosphor-service-idle-cli --stage dim:5 --stage lock:10
# hold an inhibition for the first 8s, then release it
phosphor-service-idle-cli --stage dim:5 --inhibit-for 8
```

## Design notes

- **Composes the foundation primitives.** The `ext-idle-notify-v1` and
  `zwp-idle-inhibit-v1` clients already live in `phosphor-wayland`
  (`IdleNotifier` / `IdleInhibitor`), which also vendors the protocol XML. This
  library links `phosphor-wayland` privately and builds the policy on top. It
  binds no protocols itself.
- **A monotonic stage ladder.** Stages sort by ascending timeout. Each stage's
  source fires in timeout order and advances `currentStage`. The first activity
  resets the whole ladder to active. The shell reads the stage and decides the
  action.
- **Inhibition pauses monitoring.** `inhibit()` / `release(cookie)` reference-count
  inhibition. While any cookie is held the ladder is disarmed (so the compositor
  delivers no idle notifications) and the service reports active. This is the
  surface-less inhibition a shell needs, distinct from the surface-bound
  `PhosphorWayland::IdleInhibitor` a QML window uses to keep its own output awake.
- **Single host, no model.** Like `phosphor-service-polkit` (one active request),
  the service is one `IdleService` host with the current state and signals, not a
  `QAbstractListModel`.
- **Inert without the protocol.** When the compositor advertises no
  `ext-idle-notify-v1`, `supported` is false and the service stays inert.

## Dependencies

- `phosphor-wayland` (private link; provides `IdleNotifier` / `IdleInhibitor`
  and the generated protocol code). No separate `wayland-protocols` dependency.
- Qt6 >= 6.6 (Core, Qml). The CLI additionally uses Qt6 Gui for
  `QGuiApplication`.
- A compositor advertising `ext-idle-notify-v1` for the live path (the library
  loads inert without it).

## Status

Shipped. `IdleService` watches the session through a configurable
multi-stage timeout ladder built on `PhosphorWayland::IdleNotifier`
(`ext-idle-notify-v1`): `stages` configure the ladder, `currentStage` /
`currentStageName` / `idle` report the live position, and `idled(stage)` /
`resumed()` mark the transitions. `inhibit()` / `release(cookie)` reference-count
idle inhibition, disarming the ladder while held. The
`examples/phosphor-service-idle-cli` demo registers stages and logs each fire
with a timestamp against a live session. Five test binaries pin the deterministic
surface with no compositor: the smoke harness (registration idempotency, inert
construction), the facade test (stage round trip + sort, inhibition toggle), the
QML-engine load test, the stage state machine (advance / reset / reconfigure /
pause), and the inhibition ref-count. The idle-action policy (dim, lock, display
power) is a future shell consumer, and suspend / power coupling lives in the
session service (session / logind).
