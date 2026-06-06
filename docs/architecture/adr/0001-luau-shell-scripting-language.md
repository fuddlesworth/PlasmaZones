<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0001 — Adopt Luau as the shell scripting & configuration language

| | |
|---|---|
| **Status** | Implemented (2026-05-30) — tiling increment shipped; see follow-ups |
| **Date** | 2026-05-30 |
| **Deciders** | Nathan (fuddlesworth) |
| **Scope** | Shell/compositor scripting surface; first increment = scripted autotiling |
| **Supersedes** | — |
| **Spike** | `spike/luau-autotile-embed` (throwaway, `spikes/luau-autotile-embed/`) |
| **Impl plan** | [luau-migration-impl-plan.md](../luau-migration-impl-plan.md) |

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
4. **Library factoring:** a new generic **`libs/phosphor-scripting`** (LGPL-2.1)
   owns the Luau host (engine, sandbox, watchdog, compile/load, marshalling
   core) with no tiling knowledge, so future shell surfaces reuse it.
   `libs/phosphor-tiles` depends on it and implements the tiling binding
   (`LuauTileAlgorithm`, the `pluau` stdlib + `.d.luau`, params/state/tree
   marshalling). **Luau is vendored** as a committed source tarball by default
   (opt-in `-DPLASMAZONES_SYSTEM_LUAU=ON`) because it is packaged for only Arch
   and Nix — not Debian, Fedora, or openSUSE — and has no stable ABI.

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
- **Ergonomics:** `master-stack` is **~14 LOC** against a typed `pluau` Rect API,
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

The full phased breakdown lives in the
[implementation plan](../luau-migration-impl-plan.md) (one `feat/luau-scripting`
branch off `v3.1`, per-phase commits; PRs target `v3.1`). In summary:

1. **Decide & document** — this ADR + the implementation plan.
2. **Build `phosphor-scripting`** — the generic Luau host (engine, sandbox,
   watchdog, compile/load, marshalling core), graduating the spike's `LuauHost`
   into a tested LGPL library.
3. **Migrate autotiling as the first consumer** — the tiling binding in
   `phosphor-tiles` (`LuauTileAlgorithm`, `pluau` stdlib, params/state/tree
   marshalling), port the 25 algorithms under a golden parity test, swap the
   loader backend, keep the `TilingAlgorithm` interface stable.
4. **Remove the QJSEngine scripted path** once parity + tests pass.

**Sequencing refinement.** This ADR originally proposed rolling out "new shell
surfaces first." We now lead with **tiling as `phosphor-scripting`'s first
consumer**: it has a contained contract and an existing test suite, and its path
is pure **C++↔Luau with no QML bridge** — so it validates the embedding core
without entangling the harder QML↔Luau UI boundary. The QML↔Luau bridge remains
separate future work for actual shell UI surfaces.

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

**Done.** The full tiling increment shipped — impl-plan phases 0–7 are complete:
`libs/phosphor-scripting` (LGPL Luau host: engine, `luaL_sandbox`, interrupt
watchdog, a per-script **heap cap** via a custom capped `lua_Alloc` enforced
once sandboxed, compile/load, QVariant marshalling), `libs/phosphor-tiles`
(`LuauTileAlgorithm` + the `pluau` stdlib), all 25 algorithms ported under a
golden-snapshot parity test, the loader swapped to `.luau`, the QJSEngine path
deleted, and a CI **`luau-analyze` gate** over the bundled algorithms + `pluau`
stdlib. Vendoring landed as a **committed source tarball**
(`extern/luau-0.723.tar.gz`, extracted at configure time from that local file
via `file(ARCHIVE_EXTRACT)`) rather than a submodule or the unpacked tree, so source
tarballs stay self-contained for every distro with no network, while the repo
carries one ~2 MB blob instead of ~950 files;
`-DPLASMAZONES_SYSTEM_LUAU=ON` still links a system Luau. An end-user
[Luau algorithm authoring guide](../luau-algorithm-authoring.md) ships with it.

**Remaining:**

- **QML↔Luau bridge design** — deferred to the eventual shell-UI surfaces; not
  needed for the tiling migration, which is pure C++↔Luau.

Both spike-designed safety nets are now built: the interrupt watchdog bounds CPU
*time*, and the capped allocator bounds *heap* (default 64 MiB, configurable per
engine; enforcement starts at `sandbox()` so trusted init/preludes can't be
spuriously OOM-killed). A runaway allocation surfaces as a catchable Luau OOM,
not a host crash.

A competitor-architecture study (how Hyprland / Noctalia-Quickshell structure
their Lua and QML↔Lua boundaries) was considered and **deliberately skipped** —
the QML↔Luau bridge will be validated directly via the incremental rollout, not
by pre-studying other projects.
