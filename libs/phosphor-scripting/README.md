<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-scripting

> A generic, sandboxed [Luau](https://luau.org/) host: VM lifecycle,
> `luaL_sandbox`, a CPU-time watchdog, a per-engine heap cap, bytecode
> compile/load, and a `QVariant`↔Lua marshalling layer — with **no** knowledge
> of tiling or any other domain.

## Responsibility

PlasmaZones runs user-authored scripts (today: tiling algorithms; later: other
shell surfaces). This library owns the **embedding** of Luau so each domain
binding can stay small: marshal its params into `QVariant`, call a script, read
the result back. It deliberately knows nothing about what the scripts compute.

- **`LuauEngine`.** Owns one `lua_State`. Lifecycle: construct → `init`
  (open restricted stdlib, wire the interrupt callback, install the capped
  allocator) → `runPrelude` (zero or more, to install host globals such as a
  domain standard library) → `sandbox` (freeze globals + stdlib) → `loadModule`
  (per script) → `callModule` / `moduleField`. The public surface is entirely
  `QVariant`-based; `lua_State` is forward-declared and never leaks across the
  shared-library boundary.
- **`LuauWatchdog`.** A shared supervisor thread that bounds CPU time: it only
  ever flips a co-owned atomic flag the engine's interrupt callback reads, and
  never touches the `lua_State` — so a late fire during teardown is safe. One
  watchdog can be shared across many engines.
- **Heap cap.** A custom `lua_Alloc` tracks live/peak bytes and fails any
  allocation that would cross the cap, turning a runaway script into a catchable
  Luau out-of-memory error rather than a host crash. Default 64 MiB, configurable
  per engine. Enforcement is armed only around the `lua_pcall` of sandboxed
  script execution, so trusted init/preludes and host-side QVariant marshalling
  (which touch the VM outside any protected call) can never be spuriously
  OOM-killed into an uncatchable abort.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorScripting::LuauEngine`   | The sandboxed VM; `QVariant`-only API (init / runPrelude / sandbox / loadModule / releaseModule / moduleField / hasFunction / callModule) |
| `PhosphorScripting::LuauWatchdog` | Shared CPU-deadline supervisor; aborts runaway scripts via the interrupt callback |

## Typical use

```cpp
#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

using namespace PhosphorScripting;

auto watchdog = std::make_shared<LuauWatchdog>();
LuauEngine engine(watchdog);          // default 64 MiB heap cap
engine.init();
engine.runPrelude(QStringLiteral("pz"), preludeSource);  // install host globals
engine.sandbox();                     // freeze globals + stdlib

QString err;
const int mod = engine.loadModule(QStringLiteral("script"), source, &err);
const auto out = engine.callModule(mod, QStringLiteral("tile"), {ctx}, 100 /*ms*/);
if (out.status == LuauEngine::CallStatus::Ok) {
    // out.result is a QVariant marshalled from the script's return value.
}
```

## Vendored Luau

Luau is vendored as a committed source tarball (`extern/luau-<ver>.tar.gz`,
pinned to 0.723), extracted at configure time via `file(ARCHIVE_EXTRACT)` and
built as part of this library — so source tarballs and offline distro builds are
self-contained with no network. Pass `-DPLASMAZONES_SYSTEM_LUAU=ON` to link a
system-provided Luau instead. The Luau libraries are linked **PRIVATE** and the
VM is built PIC, with `-fexceptions` force-enabled and unity builds disabled on
the vendored targets only (Luau needs C++ unwinding for `lua_pcall`, and its
file-local symbol names collide under unity batching) — independent of the
project's global build settings.

## Design notes

- **Two safety nets, two axes.** The watchdog bounds CPU *time*; the capped
  allocator bounds *heap*. Both surface a violation as a catchable error and let
  the engine recover for the next call.
- **`lua_State` is private.** Public headers forward-declare it and expose only
  `QVariant`; the Luau libraries are a PRIVATE link dependency, so no Luau
  symbols cross the `.so` boundary. Domain bindings marshal their own params.
- **Not thread-safe.** Like the underlying VM, all calls on one engine must
  occur on its owning thread.

## Dependencies

- `QtCore`, `Threads`
- Vendored Luau (`extern/luau-*.tar.gz`), or a system Luau via `-DPLASMAZONES_SYSTEM_LUAU=ON`

## See also

- [`phosphor-tiles`](../phosphor-tiles/README.md) — the first consumer: `LuauTileAlgorithm` + the `pz` tiling standard library.
- [`docs/architecture/adr/0001-luau-shell-scripting-language.md`](../../docs/architecture/adr/0001-luau-shell-scripting-language.md) — why Luau.
