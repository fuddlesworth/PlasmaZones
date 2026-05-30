<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Luau Migration — Implementation Plan

| | |
|---|---|
| **Status** | Planned — not started |
| **Date** | 2026-05-30 |
| **Decision** | [ADR-0001](adr/0001-luau-shell-scripting-language.md) |
| **Spike** | `spike/luau-autotile-embed` (`spikes/luau-autotile-embed/`, all green) |
| **Branch** | `feat/luau-scripting` (one branch, per-phase commits) |

Implementation plan for moving the scripted autotiling engine from Qt's
QJSEngine to **Luau**, by way of a new generic **`phosphor-scripting`** library.
ADR-0001 holds the *why*; this document holds the *how*.

## Library factoring

Two layers, with a deliberate separation so the Luau host is reusable by future
shell surfaces (keybindings, window rules, widgets, plugins), not bound to
tiling:

### `libs/phosphor-scripting` (NEW — LGPL-2.1)

Generic Luau embedding. **No tiling knowledge.** Graduates the spike's
`LuauHost` into a tested library:

- `LuauEngine` — state lifecycle (`luaL_newstate`/`openlibs`), `luaL_sandbox`,
  prelude injection, per-script `luaL_sandboxthread`.
- `LuauWatchdog` — interrupt-callback watchdog (atomic flag + `luaL_error`
  abort). Adopt the existing `ScriptedAlgorithmWatchdog`'s proven
  deadline/generation/shared-thread design; the watchdog never touches the
  `lua_State`.
- compile/load — `luau_compile` → `luau_load`, bytecode cache, load-error
  capture.
- marshalling core — `QVariant`↔Lua, table push/read helpers, module-table
  extraction, registry refs, error-string capture.
- (optional, later) `luau-analyze` import gate.

### `libs/phosphor-tiles` (EXISTING)

Gains a dependency on `phosphor-scripting` and implements only the
**tiling-specific binding**:

- `LuauTileAlgorithm : TilingAlgorithm` — replaces `ScriptedAlgorithm`.
- the `pz` tiling stdlib in Luau (Rect API + the 25 C++ builtin helpers
  reimplemented) + typed `.d.lua` definitions.
- `TilingParams` / `TilingState` / `SplitTree` ↔ Lua marshalling.

**Blast radius is small:** the registry
(`ITileAlgorithmRegistry::registerAlgorithm(id, TilingAlgorithm*)`),
`AutotileEngine`, editor, and settings all talk to the `TilingAlgorithm`
interface — none know about the scripting engine. Only the concrete algorithm
class, the helpers, and the loader backend change.

## Contract `LuauTileAlgorithm` must reproduce

`ScriptedAlgorithm` is more than "compute zones." The full surface that must be
preserved (verified against the current implementation):

- **Hot path** — `calculateZones(params)`, where `params` carries `area`,
  `innerGap`, `splitRatio`, `masterCount`, `minSizes[]`, the split **`tree`**
  (memory-aware algorithms), `windows[]{appId,focused}`, `focusedIndex`,
  `screen{id,portrait,aspectRatio}`, and `custom{}`.
- **~12 metadata / override accessors** — `name`, `description`,
  `masterZoneIndex`, `supportsMasterCount`, `supportsSplitRatio`,
  `defaultSplitRatio`, `minimumWindows`, `defaultMaxWindows`,
  `producesOverlappingZones`, `zoneNumberDisplay`, `centerLayout`,
  `supportsMinSizes`, `supportsMemory` — each resolved three-tier
  (override fn → cached → metadata).
- **`prepareTilingState`** — builds the BSP split tree for memory-aware
  scripts. Stays C++, unchanged.
- **v2 lifecycle hooks** — `onWindowAdded/Removed(state, idx)` with a
  marshalled `state` object (`windowCount`, `masterCount`, `splitRatio`,
  `windows[]`, `focusedIndex`, `countAfterRemoval`).
- **v2 custom params** — `customParamDefList`, `hasCustomParam`,
  `customParamDefs`, declared in metadata.
- **Split-tree marshalling** (`splitNodeToJSValue`) — read-only deep copy of
  the tree → Lua. The trickiest piece (`dwindle-memory`).

The `metadata` table replaces *both* the JS `metadata` object and the
`// @key value` comment form — pick the table form only.

## Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| New library | `phosphor-scripting` (LGPL-2.1), generic | Reused by future shell surfaces, not tiling-bound |
| Vendoring | git submodule `extern/luau` @ 0.723, **static, default**; opt-in `-DPLASMAZONES_SYSTEM_LUAU=ON` | Luau packaged for only 2 of 5 targets (Arch, Nix); not Debian/Fedora/openSUSE. No stable ABI + ~weekly releases ⇒ pin and vendor. System switch for Arch/Nix no-bundling packagers. |
| Bundled algorithms | `.luau` **source**, installed to `plasmazones/algorithms` | Readable, hot-reloadable, learnable. Bytecode precompile deferred. |
| `.js` → `.luau` | Clean break, no auto-migration | Per CLAUDE.md no-ad-hoc-backcompat. Documented manual porting guide for users with custom `.js`. |

### Luau packaging by release target (2026-05-30, via Repology)

| Target | Packaged? | Version |
|---|---|---|
| Arch (`extra`) | ✅ | 0.723 |
| Nix / nixpkgs | ✅ | 0.703 stable / 0.720 unstable |
| Fedora (COPR/RPM) | ❌ | — |
| openSUSE (OBS) | ❌ | — |
| Debian | ❌ | — |

## Phased work (branch `feat/luau-scripting`, per-phase commits)

| Phase | Work | Exit criteria |
|---|---|---|
| **0. Foundations** | Submodule Luau @ 0.723; confirm name/ext/format (done); CMake `SYSTEM_LUAU` option | submodule builds in-tree |
| **1. phosphor-scripting** | New LGPL lib (mirror other phosphor-* CMake/export/install); `LuauEngine`, `LuauWatchdog`, compile/load, marshalling core; unit tests (sandbox/escape probe, watchdog kill, compile-error surfacing, round-trip marshalling) | spike `LuauHost` is now a tested library |
| **2. Tiling binding** | `pz` Luau stdlib (Rect + 25 helpers) + `.d.lua`; `LuauTileAlgorithm` implementing the *full* contract incl. tree/hooks/custom-params; port `test_scripted_algorithm.cpp` | one algorithm runs end-to-end with hooks + memory |
| **3. Port 25 algorithms** | `data/algorithms/*.js` → `*.luau` using `pz`; **golden parity tests** (JS vs Luau zone output across counts/gaps/ratios/min-sizes); `luau-analyze` clean | byte-identical zones for all 25 |
| **4. Loader / discovery** | Swap loader backend to create `LuauTileAlgorithm`, discover `.luau`; bytecode-aware hot-reload; install `.luau` | hot-reload works; registry unchanged |
| **5. Host integration** | Verify daemon (`AutotileEngine`), editor (preview + custom-params UI), settings (algo list) untouched via `TilingAlgorithm`; update editor "new algorithm" template to `.luau` + ship `.d.lua` | end-to-end in real app |
| **6. Remove JS path** | Delete `scriptedalgorithm*`, `builtins.qrc`, sandbox; drop `Qt6::Qml` from phosphor-tiles if only QJSEngine needed it; update CLAUDE.md/docs | QJSEngine gone |
| **7. Packaging / CI / docs** | Vendored static Luau in packaging; CI `luau-analyze` gate; ADR → Implemented; author guide for `.luau` algorithms | shippable |

## Cross-cutting risks

1. **Parity testing is the linchpin.** All 25 algorithms must produce identical
   layouts or users see regressions. Phase 3's golden harness (run old JS + new
   Luau engines, diff zones) gates the migration. Non-negotiable.
2. **Split-tree marshalling** (memory-aware `dwindle-memory`) is the hardest
   binding — budget extra care.
3. **The `pz` API must grow to cover all 25** — the spike did only
   columns/rows/split. Three-column, deck, spiral, paper, dwindle,
   centered-master each stress it. **Min-size handling moves host-side** (a big
   simplification, but needs design).
4. **User-facing break** — `.js` custom algorithms stop working. Ship a porting
   guide (manual, not auto-migration).

## Relationship to ADR-0001

ADR-0001 originally sequenced "shell surfaces first, then tiling." This plan
refines that: **tiling is `phosphor-scripting`'s first consumer** because it has
a contained contract and an existing test suite, and the algorithm path is pure
C++↔Luau with **no QML bridge**. The QML↔Luau bridge remains separate future
work for actual shell UI surfaces; tiling validates the C++ embedding half of
`phosphor-scripting`.
