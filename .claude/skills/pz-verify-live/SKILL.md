---
name: "PZ Verify Live"
description: "Verify a placement, tiling, scrolling or effect change against a real running compositor using the nested-KWin harness, instead of inferring it from a green test suite. Use when: does this actually work, verify the fix, test it live, reproduce the bug, check on a real desktop, multi-monitor repro, before claiming a fix works."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PZ Verify Live

A green suite is not evidence a placement fix works. The unit suite has never
caught a real engine bug on its own; what catches them is a live run plus
checking state invariants. Anything touching the snap, tile or scroll engines,
window tracking, or the KWin effect needs a live check before it is called done.

The nested harness exists because **the effect `.so` never reloads in a live
session**. Rebuilding it and expecting the running desktop to pick it up gives a
false negative every time. A nested compositor starts fresh on every launch, and
needs no logout.

## Start a session

```bash
scripts/nested-kwin/run-nested.sh [output-count] [width height] [scale]
```

- `width/height/scale` apply to **every** virtual output; kwin_wayland's flags
  are global. Genuinely mixed per-output scale needs a seeded
  `kwinoutputconfig.json` in the nested config home.
- `PZ_NESTED_VISIBLE=1` runs it as a window on the host desktop instead of
  headless, so you can watch and drive it with pointer and keyboard. Defaults to
  1600x900 when no size is given.
- `PZ_NESTED_XWAYLAND=1` for X11 test clients.
- `PZ_NESTED_BUILD` selects a configure dir other than `build/`. Use
  `build-nounity` for anything in the shell tier, since `build/` has
  `BUILD_PHOSPHOR_SHELL=OFF`.

State lives in `$PZ_NESTED_DIR` (default `$XDG_RUNTIME_DIR/pz-nested`). In every
follow-up shell:

```bash
. "$XDG_RUNTIME_DIR/pz-nested/env.sh"
```

**One run at a time per state directory.** Two sessions sharing a
`PZ_NESTED_DIR` evict each other's daemon and overwrite each other's pid file.
Concurrent sessions need a distinct `PZ_NESTED_SOCKET` *and* a distinct
`PZ_NESTED_DIR`.

## Start the daemon

```bash
scripts/nested-kwin/daemon.sh
```

Runs the build-tree `plasmazonesd` inside the nested session with a known pid
and log. It never touches the host session's daemon.

Never `pkill -f plasmazonesd` — that kills the user's real desktop daemon.

## Inspect

```bash
scripts/nested-kwin/dump-windows.sh
```

Committed geometry and KWin output assignment for every normal window, read
from inside the compositor via a throwaway KWin script. **Committed geometry is
the ground truth for boundary bugs; screenshots are not.**

```bash
scripts/nested-kwin/capture-output.py <OutputName> <out.png>
```

Caveat that has misled before: this render **bypasses the effect chain**, so it
shows raw committed geometry and never effect-side suppression. Use it to verify
geometry, not rendering. Needs `dbus-python` and Pillow.

## Reading the logs

The daemon log is the only log file; the compositor's output stays on the
terminal that launched it. For the host session:

```bash
journalctl --user -f -u plasma-kwin_wayland.service
```

Set `QT_FORCE_STDERR_LOGGING=1` when a log looks empty. **An empty log section
is evidence, not an absence of it**: a silent `lcEffect` where a code path
should have logged means that path never ran, which is usually the actual
finding.

## What counts as verified

- The invariant you claimed to fix, checked in `dump-windows.sh` output, before
  and after the change.
- A proxy check that cannot fail is not verification. If the assertion would
  pass on the unfixed build too, it proves nothing.
- Confirm the change is in the binary you are running. A stale
  `/usr/share/plasmazones/*.qml` shadows the worktree copy, and a stale installed
  effect shadows the build tree.

## Host session, not nested

Only the host session can show real multi-monitor hardware, fractional scale on
real outputs, and real application behaviour. The cost is that effect changes
need a full logout to load. For effect work, iterate in the nested session and
confirm once on the host.
