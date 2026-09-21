---
name: "PZ Build & Test"
description: "Configure, build and test PlasmaZones correctly, avoiding the configure flags that make a suite silently run nothing and the greps that call a warning-laden build green. Use when: build, rebuild, run the tests, run ctest, verify it compiles, is it green, check the build, before committing."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PZ Build & Test

Two configure flags in this repo default to `OFF` and **fail as success**. A
build dir configured without them produces no tests at all, and `ctest` then
prints "No tests were found", which reads like a pass. Everything below exists
because that failure mode is invisible.

## Build dirs that already exist

Check what a dir actually is before trusting a green result from it:

```bash
grep -E "^(BUILD_TESTING|BUILD_PHOSPHOR_SHELL|BUILD_TOOLS|CMAKE_UNITY_BUILD):" build*/CMakeCache.txt
```

| dir | testing | shell tier | unity | use for |
|---|---|---|---|---|
| `build` | ON | **OFF** | ON | day-to-day app/daemon/effect work |
| `build-noshell` | ON | OFF | ON | confirming a shell-tier change still builds shell-OFF |
| `build-nounity` | ON | **ON** | OFF | shell tier, packager parity, and the clangd compile database |

`build` has `BUILD_PHOSPHOR_SHELL=OFF`. Anything under `libs/phosphor-shell*`,
the bar, control center, launcher, power or popout libraries is **not built
there and its tests do not run**. Use `build-nounity` for shell work.

## Configure

```bash
cmake -B build -DBUILD_TESTING=ON -DBUILD_PHOSPHOR_SHELL=ON
```

- `BUILD_TESTING=ON` or there are no tests. Not optional.
- `BUILD_PHOSPHOR_SHELL=ON` for anything in the shell tier. Requires
  `USE_KDE_FRAMEWORKS=ON` (the default); those libraries need KF6 Kirigami for
  icon rendering and have no Qt-only fallback.
- `BUILD_TOOLS=ON` adds `shader-render` and friends.
- The `shader_validate_animations` and `shader_validate_pointer` gates shell out
  to `glslangValidator` (or `glslang`) and **hard-fail when neither is on PATH**
  rather than skipping. Install the distro glslang package before running ctest.

## Build

```bash
cmake --build build --parallel 6
```

`--parallel 6`, not `$(nproc)`. The machine has 8 cores and a higher guess has
overloaded it before.

## The build is not green just because it has no errors

The repo builds with `-Wall -Wextra` but **not** `-Werror`, so an error-only
grep sails straight past warnings, and the user treats warnings as defects.

```bash
cmake --build build --parallel 6 2>&1 | tee /tmp/pzbuild.log
# check the BUILD's status, not the pipeline's - grep -c exits 1 on zero matches
grep -cE "warning:" /tmp/pzbuild.log
grep -cE "error:"   /tmp/pzbuild.log
```

Zero warnings is the bar. An incremental build only re-surfaces warnings for the
TUs it recompiled, so after a batch of edits `touch` the changed files and
rebuild before claiming warning-clean.

Known trap: adding a trailing member to a brace-initialised aggregate needs a
`= {}` NSDMI, or every site that omits the initialiser warns.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Test targets carry a `TEST_LAUNCHER` of
`dbus-run-session --config-file=tests/unit/test-session-bus.conf --`. That conf
declares **no** service dirs on purpose: a stock `dbus-run-session` still reads
the standard service dirs, so the installed `plasmazonesd` gets activated on the
private bus, reparents when the bus dies, holds the test's stdout pipe open, and
**ctest hangs after the test passes**. New tests must be added ABOVE that block
in `tests/unit/CMakeLists.txt`, alongside the shared `ENVIRONMENT` block.

Never `pkill -f plasmazonesd`; that kills the user's real desktop daemon. Kill
only `build/bin/` test binaries.

## Never delegate the build to subagents

Reviewer and audit subagents must **not** run `ctest`, `cmake --build`, or extra
`cmake` configures. Six agents doing that concurrently once spawned enough
daemon activations to crash the machine. Reviewers read and grep; a single
compiled standalone probe is fine. The main session runs the build and the suite
once, serially.

## Non-unity is a separate gate

`build` uses unity blobs, which pull a symbol in from whichever sibling TU
happens to include it. A file can therefore call something it never included and
stay green, and only a packager's non-unity build fails. After touching daemon,
engine, effect, or shell sources:

```bash
cmake --build build-nounity --parallel 6
```

The nastiest shape is not a missing include but a genuine cross-TU dependency on
a function with internal linkage (a `static` in an anonymous namespace), which
only ever links under unity.

Shell-tier changes need **both** a shell-ON and a shell-OFF configure
(`build-nounity` and `build-noshell`). The Makefile generator rejects ninja's
`-k 0`.

## Conventions gate

```bash
python3 scripts/check-conventions.py
```

Stdlib only, runs on the whole tree in about a second, and also runs in CI and
on pre-commit. See `--list-rules`.

## Green does not mean it works

A passing suite is not evidence the behaviour is right. The engine invariants
that matter have historically been caught by live verification and by invariant
checks, not by the unit suite. For anything touching placement, use the
`pz-verify-live` skill before claiming a fix works.
