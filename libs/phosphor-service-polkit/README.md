<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-polkit

A PolicyKit authentication agent, built on the `polkit-qt6` binding, for
Phosphor-based desktop shells.

## Responsibility

When an application requests a privileged action, `polkitd` calls into the
authentication agent registered for the session, and the agent drives the PAM
conversation that authenticates the user. This library is that agent. It has no
UI. It surfaces the request and a respond / cancel path, and a shell renders the
authentication dialog.

- Register with `polkitd` as the session's authentication agent (opt-in, since
  becoming the agent intercepts every authentication).
- Surface the active authentication request (action, message, identities, and
  each PAM prompt) and drive the `polkit-qt6` `Agent::Session` that answers it.
- Pass the user's response straight through to PAM without retaining it.

The authentication dialog itself is a future shell consumer of this library.

## Key types

| Type          | Role                                                                                     |
|---------------|------------------------------------------------------------------------------------------|
| `PolkitAgent` | Registers as the session's authentication agent and surfaces the active request + a respond / cancel path. Wraps `polkit-qt6`'s `Agent::Listener` privately, so its public surface carries no polkit-qt types. |
| `AuthRequest` | One decoded authentication request polkit is waiting on (action / message / icon / details / identities + the selected identity). |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServicePolkit/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServicePolkit::registerQmlTypes();
    // ... load shell.qml
}
```

QML authentication dialog (drives one request to completion):

```qml
import Phosphor.Service.Polkit 1.0

PolkitAgent {
    id: agent
    Component.onCompleted: registerAgent()

    onAuthenticationRequested: (request) => authDialog.show(request)
    // promptRequested is an EVENT, not a property change: it fires once per PAM
    // prompt INCLUDING a same-text retry after a wrong answer. Drive the dialog's
    // input field from it (not from a binding on request.prompt, which a
    // same-text retry would not re-notify). echo is false for secrets.
    onPromptRequested: (prompt, echo) => authDialog.askFor(prompt, echo)
    onAuthenticationCompleted: (gained) => authDialog.finish(gained)
}

AuthDialog {
    id: authDialog
    // request.message + request.identities choose who; askFor() shows each PAM
    // prompt as it arrives.
    onAuthenticate: agent.authenticate()
    onAnswered: (text) => agent.respond(text)   // straight to PAM, never stored
    onDismissed: agent.cancel()
}
```

The CLI doubles as the worked example and the acceptance harness. It runs as the
agent itself:

```sh
# register as the session agent and answer prompts (stop the desktop agent first)
phosphor-service-polkit-cli
# in another terminal, trigger an action:
pkexec true
```

## Design notes

- **Wraps polkit-qt privately.** `PolkitAgent` is a plain `QObject`, and the
  `PolkitQt1::Agent::Listener` it subclasses lives in the `.cpp`. polkit-qt6 is
  a private link, so consumers neither include nor link it.
- **Registration is explicit.** `registerAgent()` opts into becoming the
  session's agent, and the constructor has no side effects. Exactly one agent serves
  a session, so when the desktop's agent (KDE / GNOME) already holds it,
  registration fails and the object stays inert (`registered() == false`).
- **Surface the request, never the secret.** The user's response flows straight
  into `Agent::Session::setResponse`. The library never stores, logs, or echoes
  it.
- **Dependency injection for tests.** The agent takes an injectable session id +
  object path, so registration is exercised against a synthetic session with no
  real `polkitd` and no chance of intercepting the tester's authentications.

## Dependencies

- `polkit-qt6` (Core + Agent), discovered via `find_package(PolkitQt6-1)`. A
  running `polkitd` on the system bus for the live path (the lib loads inert
  without it). The polkit-qt6 GUI action-button widgets are not used.
- Qt6 ≥ 6.6 (Core, Qml).

## Status

Shipped. The `PolkitAgent` registers as the session's authentication
agent (explicit, inert when another agent owns the session), decodes polkit's
`initiateAuthentication` into a typed `AuthRequest` (action / message / icon /
details / identities), and drives the `polkit-qt6` `Agent::Session` PAM
conversation: `authenticate()` starts it, the PAM prompt surfaces on
`AuthRequest`, `respond()` answers it straight to PAM without retaining the
secret, and `completed` / `cancel` resolve polkit's result exactly once. The
`examples/phosphor-service-polkit-cli` standalone-agent demo runs as the agent,
logs requests, answers prompts, and exercises the live
path against `pkexec`. Three test binaries pin the deterministic surface with no
`polkitd`: the C++ smoke harness (inert construction, registration-fail, the
active-request / authenticate / respond / cancel guards), the QML-engine facade
load test, and the decode unit test over real polkit-qt `Details` / `Identity`
values. The authentication dialog UI is a future shell consumer.
