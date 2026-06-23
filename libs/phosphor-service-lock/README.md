<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-lock

A session-lock + authentication service for Phosphor-based desktop shells.

## Responsibility

Authenticates the session user through PAM and coordinates the
`ext-session-lock-v1` lock state with the compositor. It is the policy /
state-machine layer over `phosphor-wayland`'s `SessionLock` client (an
`ext-session-lock-v1` client) and a PAM authentication backend. It composes them
rather than binding the protocol or talking to PAM in its public surface, so its
public types are clean Qt/QML types with no Wayland or PAM types leaking out.

- Verify the session user's password through PAM, off the GUI thread.
- Ask the compositor to lock the session and track the lock lifecycle.
- Release the lock once authentication succeeds.

The lock *surfaces* (the per-output graphics shown while the session is locked)
are a shell concern wired up by a future shell consumer. This service owns
authentication and the lock lifecycle, not rendering.

## Key types

| Type             | Role                                                                                  |
|------------------|---------------------------------------------------------------------------------------|
| `LockService`    | The lock host. `lock()` asks the compositor to lock (state `Locking` → `Locked`). `unlock(password)` authenticates the session user (`Authenticating`) and, on success, releases the lock (`Unlocked`, `unlocked()`). On failure `authenticationFailed()` fires and the state returns to `Locked`. Exposes `supported`, `state`, and `locked`. A plain instantiable QML type, not a singleton. |
| `PamAuthenticator` / `IAuthenticator` | The standalone credential-check surface: `authenticate(user, password)` runs PAM off the GUI thread and reports `succeeded()` / `failed(reason)`. The PAM service name is configurable (default `login`). Use it to verify a password independently of the compositor lock. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceLock/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceLock::registerQmlTypes();
    // ... load shell.qml
}
```

QML lock screen (a shell lock surface drives the service):

```qml
import Phosphor.Service.Lock 1.0

LockService {
    id: lock
    onUnlocked: console.log("session unlocked")
    onAuthenticationFailed: (reason) => console.log("denied:", reason)
}

// request the lock when the session should be secured
Component.onCompleted: lock.lock()

// the password field submits an unlock attempt
TextField {
    echoMode: TextInput.Password
    onAccepted: lock.unlock(text)
}
```

The CLI doubles as the worked example and the acceptance harness:

```sh
# verify a password for the current user through PAM (prompts; echo disabled)
phosphor-service-lock-cli authenticate
# authenticate a specific user against a specific PAM stack
phosphor-service-lock-cli authenticate alice --service phosphor-lock
# report whether the compositor advertises ext-session-lock-v1
phosphor-service-lock-cli supported
```

## Design notes

- **Composes the foundation primitive.** The `ext-session-lock-v1` client lives
  in `phosphor-wayland` (`SessionLock`). This library links it privately and
  builds the authentication + lock state machine on top. It binds no protocols
  itself. The protocol-correct teardown rules (never destroy a locked session;
  honour the must-stay-locked-if-the-client-dies guarantee) live in the client.
- **Authenticates off the GUI thread.** `PamAuthenticator` runs the blocking PAM
  transaction (`pam_authenticate` + `pam_acct_mgmt`) on the global thread pool
  and marshals the result back to the GUI thread, so a PAM module that sleeps
  (faildelay, a network backend) never stalls the event loop. One transaction at
  a time. The password answers only PAM's echo-off prompt, is moved to a sole
  owner, and is wiped (`explicit_bzero`) after the transaction. It is never
  logged.
- **Seams for testability.** The lock state machine is driven through an
  `ISessionLock` seam and the `IAuthenticator` seam, so the whole lock policy
  (request → locked → authenticate → unlock, with auth-failure retry and
  compositor-driven teardown) is unit-tested with fakes and no live compositor or
  PAM stack.
- **A single active lock, not a model.** Like `phosphor-service-polkit` /
  `phosphor-service-idle` and unlike `phosphor-service-clipboard`, the lock is a
  single state, exposed as a `State` enum rather than a list model.
- **Lock surfaces are deferred.** A real lock screen must create an
  `ext_session_lock_surface_v1` per output before the compositor presents the
  locked frame. That rendering layer is a shell concern owned by a future
  consumer. Until then the compositor decides, per its own policy, when to
  confirm the lock.

## Dependencies

- `phosphor-wayland` (private link; provides the `SessionLock`
  `ext-session-lock-v1` client and the vendored protocol code). No separate
  `wayland-protocols` dependency.
- PAM (`libpam`, private link) for authentication.
- Qt6 >= 6.6 (Core, Qml; Concurrent privately for the off-thread PAM
  transaction). The CLI additionally uses Qt6 Gui for `QGuiApplication`.
- A compositor advertising `ext-session-lock-v1` for the live lock path (the
  library loads inert without it, and authentication still works).

## Status

Shipped. `LockService` authenticates the session user through
`PamAuthenticator` (PAM, off the GUI thread, configurable service, default
`login`) and coordinates the lock state with the compositor through
`PhosphorWayland::SessionLock` (`ext-session-lock-v1`), running the state
sequence `Unlocked → Locking → Locked → Authenticating`, releasing the lock on a
successful unlock and staying locked on a failed attempt. The `examples/phosphor-service-lock-cli` demo
provides `authenticate` (PAM, prints success/failure) and `supported` against a
live session. Four test binaries pin the deterministic surface with no compositor
or valid credentials: the smoke harness (registration idempotency, inert
construction), the QML-engine load test, the PAM authenticator test (configured
service, fast-fail, the concurrency guard, real-PAM rejection of a nonexistent
user), and the state-machine test (the full lock policy on fakes). The per-output
lock surfaces and the lock-screen UI are a future shell consumer of this library.
