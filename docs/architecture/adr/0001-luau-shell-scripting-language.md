<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0001 — Adopt Luau as the shell scripting & configuration language

| | |
|---|---|
| **Status** | Accepted (direction); implementation staged |
| **Date** | 2026-05-30 |
| **Deciders** | Nathan (fuddlesworth) |
| **Scope** | Shell/compositor scripting surface; first increment = scripted autotiling |
| **Supersedes** | — |
| **Spike** | `spike/luau-autotile-embed` (throwaway, `spikes/luau-autotile-embed/`) |

> This is the first ADR in the PlasmaZones series. The `ADR-001…ADR-026`
> references elsewhere in the tree belong to the bundled claude-flow tooling and
> are unrelated to this series.

## Context

PlasmaZones is evolving from a window-tiling tool into a full **shell +
compositor** (a Qt/QML shell has already been started). A shell needs a
user-facing scripting and configuration surface far larger than tiling:
keybindings, window rules, widgets, IPC/automation, and compositor plugins. The
language for that surface is a foundational decision — it gets ~10× more
expensive to change after those surfaces exist.

Today, scripted autotiling runs on Qt's **QJSEngine** (JavaScript):

- ~3,285 LOC of C++ glue in `libs/phosphor-tiles` — loader, a **225-line
  sandbox** (`scriptedalgorithmsandbox.cpp`), an interrupt watchdog, builtins
  injection, split-tree handling.
- **25 algorithms / ~2,157 LOC of JS** in `data/algorithms/`.
- QJSEngine ships *inside Qt6*: zero extra dependencies, native `QJSValue`↔Qt
  marshalling. But it provides **no true sandbox** ("JavaScript can do anything
  the C++ can") and **no per-script memory cap** — the latter is structural.

**Ecosystem convergence.** Lua is the config/scripting lingua franca of the
Wayland tiling niche: AwesomeWM, Neovim, WezTerm, WirePlumber. **Hyprland fully
moved its config to Lua**, and **Noctalia v5 (Quickshell) is moving to Lua**.
Our target users already speak Lua for shell scripting; JavaScript is the
QML/web language, not the WM-config language.

## Decision

Adopt **Luau** ([luau.org](https://luau.org), MIT) as the **shell-wide scripting
and configuration language**.

1. Luau is the language for **new** shell scripting surfaces (config, keybinds,
   rules, widgets, plugins).
2. The scripted autotiling engine migrates from QJSEngine to Luau as the **first
   concrete increment** — but as part of the shell-wide rollout, **not before
   it**. Tiling follows the shell-level decision; it does not drive it.
3. Target architecture: **C++ compositor core + QML for first-party UI + Luau for
   user config/scripting/plugins.** The **QML↔Luau boundary is an explicit,
   owned interface**, designed up front.

### Why Luau specifically

| Need | Luau | Stock Lua 5.x / LuaJIT | QJSEngine (status quo) |
|---|---|---|---|
| Sandbox for untrusted plugins | ✅ `luaL_sandbox` (readonly globals/stdlib) | ❌ none built-in | ❌ "no sandboxing" |
| Per-script memory cap | ✅ custom allocator | ✅ custom allocator | ❌ structural gap |
| Runaway-script interrupt | ✅ interrupt callback | ⚠️ debug hooks | ✅ `setInterrupted` |
| Author-time typing | ✅ gradual types + `luau-analyze` | ❌ | ❌ |
| Ecosystem fit (Wayland WMs) | ✅ where it converged | ⚠️ partial | ❌ |
| License / deps | MIT, zero deps, CMake | MIT | bundled w/ Qt |

## Evidence — embedding spike

A throwaway standalone binary (`spikes/luau-autotile-embed/`, pinned Luau 0.723,
Qt6::Core) proved the path end-to-end. **All green:**

- **Build:** FetchContent Luau + Qt6 link in **30s**, one `cmake` command. Static
  binary **2.8M**; precompiling algorithms to bytecode lets the runtime ship only
  `Luau.VM` (**1.4M**), dropping the compiler+ast.
- **Sandbox is free:** `luaL_sandbox` + readonly globals **replaces the entire
  225-line `scriptedalgorithmsandbox.cpp` plus the freeze machinery**. An escape
  probe confirmed: `io` absent, `os` restricted (no `execute`/`exit`/`getenv`),
  no `loadstring`/`load`, stdlib read-only. **Note:** `getfenv`/`setfenv` are
  *not* removed, but the sandbox makes their environment read-only so a
  `getfenv(0).x = 1` write **fails** — the guarantee holds for a subtler reason
  than "removed."
- **Watchdog is safer:** an atomic flag + interrupt callback + `luaL_error` abort
  kills `while true do end` in ~150ms and the host recovers. The watchdog
  **never touches the `lua_State`** — eliminating the use-after-free lock
  discipline the QJSEngine watchdog requires.
- **Marshalling cost measured: 57 LOC** (35 in / 22 out) — the honest net-new
  burden replacing free `QJSValue::setProperty`.
- **Ergonomics:** `master-stack` is **~14 LOC** against a typed `pz` Rect API,
  versus the JS version delegating to a ~100-line injected helper.

## Consequences

### Positive

- Deletes ~400 LOC of the **most dangerous, highest-maintenance C++** (sandbox +
  per-helper freeze + the `eval`/`Function`-escape IIFE hack), every line of
  which is security-critical and rot-prone.
- Gains a **real sandbox + memory cap + author-time typing** — the correct
  foundation for an untrusted-plugin ecosystem.
- **One scripting language** across the shell, aligned with user and ecosystem
  expectations.
- Watchdog becomes simpler and safer.

### Negative / costs

- A **new vendored third-party dependency** (was zero — QJSEngine is in Qt):
  build complexity, version pinning, the compile-to-bytecode step.
- **~57 LOC of manual C↔Lua marshalling** replaces free `QJSValue::setProperty`
  (plus similar for hooks/metadata).
- One-time **rewrite**: ~25 algorithms (~2,157 LOC JS → Luau) + redirecting the
  C++ glue.
- A **permanent QML↔Luau bridge** to design and maintain — QML is JS-native, so
  Luau is a *second* language in the stack (the cost Hyprland does not have).
- Luau's C API has **no formal stability guarantee** — pin a version, expect
  occasional additive/breaking churn.

**Net C++ glue diff ≈ −350 LOC** (≈ −400 security ceremony, +~150
marshalling/setup); watchdog ≈ wash but safer.

## Migration & sequencing

Each step is its own branch with per-phase commits; PRs target `main`.

1. **Decide & document** — this ADR.
2. **Build the shared Luau host** — sandbox, interrupt watchdog, marshalling,
   the `pz` standard library, typed `.d.lua` definitions, and a `luau-analyze`
   import-time type gate. Placement TBD: a new LGPL `libs/phosphor-luau` vs.
   inside `libs/phosphor-tiles` (see open follow-ups).
3. **Roll out on new shell surfaces first** (config/keybinds/rules) to validate
   the QML↔Luau bridge before touching working code.
4. **Migrate autotiling** — port the 25 algorithms; replace `ScriptedAlgorithm`'s
   QJSEngine internals while keeping the `ITileAlgorithm` interface stable.
5. **Remove the QJSEngine scripted path** once parity + tests pass.

## Alternatives considered

- **Stay on QJSEngine (JS).** Zero new deps, consistent with the QML UI — but no
  sandbox, no memory cap, JS is not the ecosystem's config language, and the
  security ceremony stays forever. Rejected: wrong foundation for a plugin
  ecosystem.
- **Stock Lua 5.x / LuaJIT.** Smaller embedding effort, full stdlib — but no
  built-in untrusted-code sandbox, no gradual typing, and not where the
  ecosystem (esp. Hyprland) converged. Rejected: Luau's sandbox + typing are the
  whole point.
- **All-Lua UI (AwesomeWM-style, drop QML).** Removes the dual-language bridge —
  but discards the QML head start and means a far larger rewrite. Rejected:
  throws away existing investment.

## Open follow-ups

- **Host library placement & license** — LGPL `phosphor-luau` vs. inside
  `phosphor-tiles`.
- **Memory-cap allocator** and the **`luau-analyze` import gate** — designed in
  the spike, not yet built.

A competitor-architecture study (how Hyprland / Noctalia-Quickshell structure
their Lua and QML↔Lua boundaries) was considered and **deliberately skipped** —
the QML↔Luau bridge will be validated directly via the incremental rollout
(migration step 3), not by pre-studying other projects.
