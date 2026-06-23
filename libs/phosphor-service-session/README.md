<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-session

A logind session manager for Phosphor-based desktop shells.

## Responsibility

Surfaces the system session and power actions over `org.freedesktop.login1` and,
because we own the compositor and the session, manages the logind inhibitor
locks that let the shell lock before the machine sleeps and own the power / lid
keys. It is the logind edge of the shell, covering capabilities, actions,
inhibitors, and the session and sleep signals. It has no UI of its own and binds
no Wayland protocols. The logind D-Bus calls are private to the implementation,
and the public surface is clean Qt/QML types plus an injectable `QDBusConnection`
(the test / advanced-wiring seam).

- Read the power capabilities (`CanSuspend` and siblings) up front and keep them
  current.
- Issue the capability-gated actions (lock, logout, suspend, hibernate,
  hybrid-sleep, suspend-then-hibernate, reboot, power-off, halt).
- Hold a logind delay inhibitor on `sleep` so the shell can lock before suspend,
  and a block inhibitor on the power / suspend / hibernate / lid keys so the
  shell, not logind's defaults, decides what they do.
- Surface logind's `PrepareForSleep` and the session's `Lock` / `Unlock`
  signals.

This is more than a `systemctl` wrapper. A plugin shell (Quickshell / Noctalia)
has no logind inhibitor integration, so it cannot lock before an
externally-initiated suspend (lid, `systemctl suspend`, idle) and races the lock
surface. Owning the session, we close that race.

## Key types

| Type          | Role                                                                                  |
|---------------|---------------------------------------------------------------------------------------|
| `SessionHost` | The logind session host. Exposes each capability as an `Availability` `Q_PROPERTY` (`canSuspend`, `canPowerOff`, ...); the actions as invokables (`lock()`, `logout()`, `suspend()`, `reboot()`, `powerOff()`, ...), each gated by its capability; `allowSleep()` for the lock-before-sleep handshake; and the `aboutToSleep` / `lockRequested` / `unlockRequested` / `prepareForSleep` signals. `interactive` selects whether actions route auth through a polkit agent. A plain instantiable QML type, not a singleton. |
| `SessionHost::Availability` | `Yes` / `No` / `NotApplicable` / `Challenge` / `Unknown`, parsed from logind's `yes` / `no` / `na` / `challenge` capability strings (`Unknown` when logind is unreachable or the answer is unrecognised). |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceSession/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceSession::registerQmlTypes();
    // ... load shell.qml
}
```

QML power menu, plus the lock-before-sleep handshake wired against
`phosphor-service-lock` (the two services stay independent, and the shell wires
them):

```qml
import Phosphor.Service.Lock 1.0
import Phosphor.Service.Session 1.0

SessionHost {
    id: session

    // Lock before the system sleeps: logind holds on our delay inhibitor until
    // we release it, so lock first and release once the surface is up.
    onAboutToSleep: lock.lock()
    onLockRequested: lock.lock()
}

LockService {
    id: lock
    onLockedChanged: if (locked) session.allowSleep()
}

// power-menu buttons bind enablement to the capability and call the action
Button {
    text: "Suspend"
    enabled: session.canSuspend === SessionHost.Yes || session.canSuspend === SessionHost.Challenge
    onClicked: session.suspend()
}
```

The CLI doubles as the worked example and the acceptance harness:

```sh
# print the logind session capabilities
phosphor-service-session-cli status
# lock this session through logind
phosphor-service-session-cli lock
# suspend / reboot / power off (capability-gated; exits non-zero if unavailable)
phosphor-service-session-cli suspend
```

## Design notes

- **Capabilities are methods, cached as properties.** logind exposes `CanPowerOff`
  and siblings as `Manager` methods returning a string, not as D-Bus properties.
  `SessionHost` reads them asynchronously at construction, caches the parsed
  `Availability`, and re-reads on `refreshCapabilities()` (the answers move with
  inhibitor locks, lid state, and swap availability).
- **Inhibitors are the point.** A delay inhibitor on `sleep` drives the
  lock-before-sleep handshake. On `PrepareForSleep(true)` the host emits
  `aboutToSleep()` while holding the inhibitor, and the shell locks. Once the lock
  surface is up the shell calls `allowSleep()`, which drops the inhibitor and
  lets suspend proceed with the session already locked. A safety timeout drops it
  anyway if the shell never confirms, so a missing lock cannot wedge suspend past
  logind's `InhibitDelayMaxSec`. A block inhibitor on
  `handle-power-key:handle-suspend-key:handle-hibernate-key:handle-lid-switch`
  gives the shell ownership of those keys. Each inhibitor fd is held in a
  `QDBusUnixFileDescriptor` and released on teardown.
- **Auth routes through our own agent.** Actions pass logind `interactive=true`
  by default, so an action that needs authorization (a `Challenge` capability)
  prompts through the in-session polkit agent (`phosphor-service-polkit`) for a
  native experience. The CLI sets `interactive=false` (no agent in a dev shell).
- **Complementary to `phosphor-service-lock`, not overlapping.** The lock service
  authenticates and owns the `ext-session-lock-v1` surface, and this service owns
  the logind edge. The shell wires `aboutToSleep` / `lockRequested` to the lock
  service and the lock service's `locked` back to `allowSleep()`. Neither library
  depends on the other.
- **A single active host, not a model.** Like `phosphor-service-lock` /
  `phosphor-service-polkit` / `phosphor-service-idle`, the session is a single
  host with cached state, not a list model. No `ObjectManager` (logind's Manager
  is a single fixed object).

## Dependencies

- `phosphor-dbus` (private link) for the system-bus `Client`, the signal
  subscriptions, and the `Inhibit` fd call.
- Qt6 >= 6.6 (Core, Qml, DBus). The CLI uses Qt6 Core only.
- A running logind (`org.freedesktop.login1`) on the system bus for the live
  path. The library loads inert without it (capabilities `Unknown`, actions
  no-op with a warning, inhibitors not taken).
- `phosphor-service-lock` and `phosphor-service-polkit` are
  collaborators wired in the shell, not link dependencies.

## Status

Shipped. `SessionHost` reads the seven logind capabilities into an
`Availability` enum, issues the capability-gated session / power actions with the
`interactive` flag, takes the delay-on-`sleep` and block-on-keys inhibitors, runs
the `PrepareForSleep` lock-before-sleep handshake (`aboutToSleep` →
`LockService.lock()` → `allowSleep()`), and surfaces the session `Lock` /
`Unlock` signals. The `examples/phosphor-service-session-cli` demo drives
`status` and the actions against a live logind. Three test binaries pin the
surface: the smoke harness (registration idempotency, inert construction, the
handshake and session-signal slot logic), the QML-engine facade load, and the
behaviour test against an in-process fake logind Manager + Session (capability
parsing, action routing with the interactive flag, capability-gated refusal,
inhibitor what/mode, the `PrepareForSleep` handshake over the bus, and
lock / terminate / signal surfacing). The shell wires the handshake to
`phosphor-service-lock` in `SessionLockCoordinator.qml`. The lock surface itself
is the shell's lock screen.
