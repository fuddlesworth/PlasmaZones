<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-polkit

A PolicyKit authentication agent for Phosphor-based desktop shells, via the
`polkit-qt6` binding.

## Responsibility

When an application requests a privileged action, `polkitd` calls into the
authentication agent registered for the session; the agent drives the PAM
conversation that authenticates the user. This library is that agent. No UI; it
surfaces the request and a respond / cancel path, and a shell renders the
authentication dialog.

- Register with `polkitd` as the session's authentication agent (opt-in, since
  becoming the agent intercepts every authentication).
- Surface the active authentication request (action, message, identities, and
  each PAM prompt) and drive the `polkit-qt6` `Agent::Session` that answers it.
- Pass the user's response straight through to PAM without retaining it.

The authentication dialog itself is a Phase 3 / 4 consumer of this library.

## Key types

| Type          | Role                                                                                     |
|---------------|------------------------------------------------------------------------------------------|
| `PolkitAgent` | Registers as the session's authentication agent and surfaces the active request + a respond / cancel path. Wraps `polkit-qt6`'s `Agent::Listener` privately, so its public surface carries no polkit-qt types. |
| `AuthRequest` | One decoded authentication request polkit is waiting on (action / message / icon / details / identities + the selected identity). |

## Design notes

- **Wraps polkit-qt privately.** `PolkitAgent` is a plain `QObject`; the
  `PolkitQt1::Agent::Listener` it subclasses lives in the `.cpp`. polkit-qt6 is
  a private link, so consumers neither include nor link it.
- **Registration is explicit.** `registerAgent()` opts into becoming the
  session's agent; the constructor has no side effects. Exactly one agent serves
  a session, so when the desktop's agent (KDE / GNOME) already holds it,
  registration fails and the object stays inert (`registered() == false`).
- **Surface the request, never the secret.** The user's response flows straight
  into `Agent::Session::setResponse`; the library never stores, logs, or echoes
  it.
- **Dependency injection for tests.** The agent takes an injectable session id +
  object path, so registration is exercised against a synthetic session with no
  real `polkitd` and no chance of intercepting the tester's authentications.

## Dependencies

- `polkit-qt6` (Core + Agent), discovered via `find_package(PolkitQt6-1)`. A
  running `polkitd` on the system bus for the live path (the lib loads inert
  without it). The polkit-qt6 GUI action-button widgets are not used.
- Qt6 ≥ 6.6 (Core, Qml). Gui arrives transitively via polkit-qt6's agent library.

## Status

Phase 2.6: in progress. Milestones 1+2 (skeleton + CMake + the `PolkitAgent`
registration plumbing; the listener-registration work folded into the
milestone-1 commit) and 3 (the `initiateAuthentication` → `AuthRequest` decode,
surfaced as the active request) landed; milestones 4-8 (the `Agent::Session` PAM
conversation, the QML facade, the CLI agent demo, tests, and README
finalisation) follow per the plan.
