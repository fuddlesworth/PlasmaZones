<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# phosphor-ipc — deferred follow-ups (from PR #539 audit)

Items raised by the multi-pass audit of PR #539 (`feat/phosphor-ipc`) that
were intentionally **not** applied in that PR. Each entry documents the
finding, the reason for deferring, and the change shape so a future PR can
pick it up without re-auditing.

The PR shipped the protocol, router, engine bridge, CLI, tests, and demo
with all in-scope findings resolved. The items below either (a) add new
defense-in-depth features outside the PR's stated scope (Phase 1.4: ship
the IPC surface), or (b) require a fleet-wide change across other demos
that already share the same pattern.

---

## 1. Per-process `MaxConnections` cap on the router

**Where:** `libs/phosphor-ipc/src/ipcrouter.cpp`, `handleNewConnection()`.

**Finding:** the accept loop drains all pending connections without any
threshold check. A same-uid attacker (sandbox-escaped browser tab, hostile
script) could open the per-process fd budget worth of connections before
the kernel listen-backlog refuses further connects.

**Why deferred:** the documented threat model is filesystem-permission
based (socket file `0600` + `XDG_RUNTIME_DIR` `0700`). A connection cap is
defense-in-depth, not a regression — the current code is correct for the
declared model.

**Change shape:**
- Add `constexpr int MaxConnections = 64;` in the anon namespace.
- Track `m_connectionCount` (incremented in `handleNewConnection` after
  `nextPendingConnection`, decremented in `handleClientDisconnected`).
- When the cap would be exceeded, `socket->abort()` the new accept and
  log a single rate-limited warning.

---

## 2. Idle-connection timeout

**Where:** `libs/phosphor-ipc/src/ipcrouter.cpp`, per-socket connect path.

**Finding:** a peer can `connectToServer()` and never send a byte. The
fd, the `QLocalSocket` QObject, and (lazily) the empty
`m_subscriptionsBySocket[socket]` entry stay live until process exit or
the OS rlimit cuts them off. AF_UNIX has no kernel keepalive.

**Why deferred:** strict defense-in-depth — no measurable resource leak
beyond one fd per idle peer, and the existing `MaxConnections` cap (item
1) would bound the total.

**Change shape:**
- Per-socket `QTimer*` started on accept, reset on every `readyRead`,
  expiring after 5 minutes — `abort()` on expiry.
- Cleanup in `handleClientDisconnected` (the timer is a child of the
  socket so reparenting handles it).

---

## 3. `SO_PEERCRED` peer credential check

**Where:** `libs/phosphor-ipc/src/ipcrouter.cpp`, `handleNewConnection()`.

**Finding:** the router accepts any peer that can connect to the socket
path. Filesystem permissions limit this to same-uid attackers, but a
defense-in-depth `getsockopt(SO_PEERCRED)` would explicitly reject any
peer whose `cred.uid != geteuid()`.

**Why deferred:** matches the existing niri / hyprland precedent of
relying on socket-file perms exclusively. Adding the check is portable to
Linux/macOS but introduces a platform-specific code path the rest of the
library doesn't yet need.

**Change shape:**
- On accept, call `getsockopt(socket->socketDescriptor(), SOL_SOCKET,
  SO_PEERCRED, ...)`.
- Compare `cred.uid` against `geteuid()`; on mismatch, log + abort.
- Add a `#ifdef Q_OS_LINUX` guard (Linux-only socket option — macOS uses
  `LOCAL_PEEREPID`/`LOCAL_PEERCRED` differently).

---

## 4. QML demos: `qsTr()` → `i18n()` migration

**Where:** `examples/phosphor-ipc-demo/Main.qml` and the peer demos
`examples/phosphor-popout-demo/Main.qml`,
`examples/phosphor-registry-demo/Main.qml`.

**Finding:** CLAUDE.md mandates `i18n()` / `i18nc()` via
`PhosphorLocalizedContext` for QML. The phosphor-ipc demo uses `qsTr(...)` at
six call sites, matching the convention already established by the two
peer demos.

**Why deferred:** the divergence is fleet-wide, not PR-local. Migrating
just the ipc demo would leave the other two demos inconsistent. Should be
one follow-up PR that wires `PhosphorLocalizedContext` into every
`examples/*-demo/main.cpp` and switches every demo at once.

**Change shape:**
- Add `PhosphorLocalizedContext` to each demo's `main.cpp` (single
  `engine.rootContext()->setContextObject(...)`).
- `grep -rl qsTr examples/` → switch to `i18n` / `i18nc` per file.
- Add the demos' `.qml` files to `lupdate` if not already covered.

---

## 5. Hardcoded `font.family: "monospace"` in QML

**Where:** as of audit pass 11, `grep -rln '"monospace"' examples/ src/`
finds 6 sites across 3 files (line numbers shift as the files evolve;
re-run the grep before starting the work):
- `examples/phosphor-ipc-demo/Main.qml` (3 sites)
- `examples/phosphor-registry-plugin-demo/plugins/cpu-meter/cpumeter.cpp` (1 site)
- `src/editor/qml/DimensionTooltip.qml` (2 sites)

**Finding:** CLAUDE.md says QML should not hardcode appearance —
`Kirigami.Theme` for colors, `Kirigami.Units` for spacing, and (by
project precedent) `Tokens.*` for font-family choices. There is no
`Tokens.font_family_mono` accessor today.

**Why deferred:** the missing token applies to all sites and to any
future phosphor-theme consumer that needs a monospace surface. The
fix is "add a token to `phosphor-theme/Tokens.qml`, then route every
consumer through it" — both edits should land together.

**Change shape:**
- Add `readonly property string font_family_mono: "monospace"` to
  `libs/phosphor-theme/Tokens.qml`.
- Replace each `font.family: "monospace"` site with
  `font.family: Tokens.font_family_mono` in a single sweep:
  `grep -rln '"monospace"' examples/ src/` lists every consumer.

---

## Decisions — no action required

These items came up during the audit and were intentionally NOT
deferred (the code is correct as-is). Documented here so a future
reviewer doesn't re-flag them and start new work on a fixed point.

### Demo IPC startup failure UX

**Where:** `examples/phosphor-ipc-demo/main.cpp:64-73`.

When `router.start()` fails, the demo launches the window anyway with
three `IpcTarget` items that each log `"target '...' is not
registered"` warnings on subsequent `emitEvent()` calls. The status
panel binds to `demoController.status` which reads `"router failed to
start (see logs)"`, and the cheat-sheet panel shows a placeholder
instead of an empty `export PHOSPHOR_SOCKET=` line.

**Why no action:** behaviour matches `libs/phosphor-ipc/README.md:117`
("Application can continue without IPC; failure is non-fatal"). The
warnings are a debugging affordance.

---

## Reference

Audit run: `/code-audit pr 539` (multi-pass, partitioned reviewers).
Branch: `feat/phosphor-ipc`.
Base: `feat/phosphor-shell`.
