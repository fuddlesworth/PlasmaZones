---
name: pz-crash-triage
description: "Read-only. Takes a PlasmaZones crash, abort or hang (a plasmazonesd, plasmazones-settings or plasmazones-editor core, a kwin_wayland core with frames in kwin_effect_plasmazones.so, a crashing ctest binary, or a daemon that stopped answering) and walks it back from the faulting frame to the mechanism: which object was gone or which state was wrong, and which path freed or wrote it. Give it the PID or coredumpctl match, the log path (nested daemon.log or the journal unit), the build dir the binary came from, and anything you already know. Use it to keep full backtraces and log dumps out of the main context."
model: sonnet
tools: Bash, Read, Grep, Glob
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

You triage one PlasmaZones crash or hang. You change nothing in the repository and you never build. Put scratch files in the session scratchpad, never in `/tmp` or the repo. Read `CLAUDE.md` first, and `.claude/skills/pz-verify-live/SKILL.md` for the nested session.

## Never touch the user's desktop

The user runs PlasmaZones on the desktop this session runs on. **Never attach gdb, `eu-stack` or any ptrace tool to the host `kwin_wayland`, `plasmashell` or `plasmazonesd`.** Stopping the compositor freezes the user's whole session, including the terminal they would use to recover. Live attach is only for processes of the nested session (`$PZ_NESTED_DIR`, default `$XDG_RUNTIME_DIR/pz-nested`, whose `daemon.pid` names the nested daemon) or for a test binary you were pointed at. A host crash is read from its core, after the fact.

## 1. Get the stack

```bash
coredumpctl list --no-pager plasmazonesd           # or kwin_wayland, plasmazones-settings, a test binary name
coredumpctl info --no-pager <PID>                   # signal, exe path, and the short stack
coredumpctl debug <PID> --debugger-arguments="-batch -ex 'thread apply all bt'" > "$SCRATCH/bt.txt" 2>&1
```

Read `bt.txt` with `grep -n` and in ranges, never whole. Find the faulting thread first (the one with the signal frame).

- **Which binary.** `coredumpctl info` gives the exe path. `/usr/bin/plasmazonesd` and `/usr/lib/qt6/plugins/kwin/effects/plugins/kwin_effect_plasmazones.so` are the user's install. `build*/bin/...` is a build tree. The `debug` and `relwithdebinfo` presets have line info, `release` does not. If the installed build has no symbols, say so and map offsets only against a build of the same commit (`addr2line -f -C -i -e <so> 0xOFF`).
- **Qt, KWin and system frames.** `DEBUGINFOD_URLS` is set on this machine, so gdb fetches their symbols. A frame in KWin is a lead to the PlasmaZones frame above it, not the cause.
- **An effect crash is a KWin crash.** Look for `kwin_effect_plasmazones.so` or `kwin_effect_plasmazones_overview.so` frames in the `kwin_wayland` core, and for the effect's logging categories (`plasmazones.effect`, `kwin.effect.plasmazones.*`).
- **A daemon crash loops.** `plasmazones.service` has `Restart=on-failure`, so several cores in a row are usually one bug. Triage the first.
- **A hang.** In the nested session, `eu-stack -p <pid>` on the nested process. On the host, only what the journal and the D-Bus timeouts in the effect's log say.

## 2. Logs around it

```bash
journalctl --user --no-pager -u plasmazones.service --since "<time of crash - 2min>" | tail -200
journalctl --user --no-pager -u plasma-kwin_wayland.service --since "..." | grep -n plasmazones
grep -n "" "$PZ_NESTED_DIR/daemon.log" | tail -200     # nested daemon
```

An empty log section where a path should have logged means that path never ran. That is evidence.

## 3. Walk back to the mechanism

From the faulting frame, find the object or value that was bad, then where it came from: who freed it, who wrote it, which signal or D-Bus reply delivered it. Read only the source on that chain (`grep -rn` the symbol in `plasmazones/` and `phosphor-libs/libs/`). Shapes that recur here:

| Shape | Where to look |
| --- | --- |
| A lambda or queued call runs after its window or handler died | Raw `EffectWindow*` or `this` captured without `QPointer` or an `isDeleted()` check, connections that outlive the receiver, D-Bus `QDBusPendingCallWatcher` replies after teardown |
| A handler dereferenced during effect or daemon shutdown | Teardown order: one handler is destroyed before another that still calls it (`m_effect->snapHandler()` style accessors) |
| A crash only after a monitor, desktop or activity change | State keyed per (screen, desktop, activity) read with a stale key, or a screen pointer kept across an output removal |
| A crash on the second drag, retry or relayout | State that is right on the first call and stale on the repeat: one-shot flags, retry counters, cached ids |
| An abort in Qt with an assert message | The `Q_ASSERT` text names the broken contract. Find the caller that broke it. |

## Report

- The result first, in a few lines: the mechanism, the `file:line` that does the wrong thing, and the trigger.
- The chain, one link per line: the frame or log line, what it shows, and how you know it (backtrace, log, source read, or inference).
- Every link you did not read directly, labeled `HYPOTHESIS:`, with the check that would confirm it (a nested-session repro, a test, a log category to enable).
- What a fix must cover, not the fix itself.
