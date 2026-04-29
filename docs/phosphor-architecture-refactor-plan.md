# Phosphor Architecture Refactor — Plan of Action

**Date:** 2026-04-28
**Branch:** `refactor/phosphor-architecture` (off `v3`)
**Status:** Approved plan; PRs not yet opened.
**Author:** Synthesis of senior-engineer analyses (system-architect agent + researcher agent).

**Predecessors:**
- `docs/library-architecture-audit.md` (2026-04-24) — dependency-graph snapshot.
- `docs/library-architecture-analysis.md` (2026-04-24) — tier-by-tier dep map.
- `docs/library-extraction-survey.md` (2026-04-21) — strategic candidate inventory establishing the compositor/WM/shell trajectory.

This document captures the **next round** of library-level surgery, motivated by smells surfaced during PR #382 review (the `WatchedDirectorySet` consolidation): once the watcher layer was unified, the metadata-pack walker on top stayed copy-pasted across two registries, and the registry layout exposed a deeper category error in `phosphor-shell`.

---

## 1. Context and goals

### 1.1 Driving smells

1. **`phosphor-shell` is two unrelated libraries fused at the link stage.** Wayland-QPA half (`src/qpa/*`, `LayerSurface`, wlr-layer-shell-v1 protocol) coexists with a shader-domain half (`BaseUniforms`, `IUniformExtension`, `ShaderIncludeResolver`, `IWallpaperProvider`, `ShaderRegistry`). Zero overlap; the shader-domain headers contain zero Wayland symbols.
2. **`PhosphorShell::ShaderRegistry` and `PhosphorAnimationShaders::AnimationShaderRegistry` are the same class twice.** Same `addSearchPath` / `setUserShaderPath` / `searchPaths` / `refresh` surface, same metadata.json subdir walk, same caps + signature + user classification, same `LiveReload` / `RegistrationOrder` plumbing. `AnimationShaderRegistry.h` literally cites `PhosphorShell::ShaderRegistry` as the "convention" it matches. PR #382 retired this duplication at the watcher layer; the metadata-pack walker on top is still copy-pasted.
3. **`phosphor-rendering` depends on `phosphor-shell` only for shader-domain types it doesn't own** (`<PhosphorShell/{BaseUniforms,IUniformExtension,ShaderIncludeResolver}.h>`). A pure RHI library is transitively pulling in the layer-shell QPA target. Unusable on non-Wayland platforms for reasons unrelated to rendering.
4. **"Shell" is the wrong word for a sub-library.** Quickshell reserves `Shell` for the runtime/binary. Phosphor-the-WM is the runtime; `phosphor-shell` as a lib name conflates the two and reads as a category error after the rebrand.
5. **Singletons inside `phosphor-animation`** — `PhosphorProfileRegistry::instance()`, `QtQuickClockManager::instance()` — directly contradict the "no singletons, DI default" memory note. The doc on `Profile.h:195` apologises for the pattern but never removed it.

### 1.2 Project goals this refactor advances

From the team's auto-memory (`MEMORY.md`):

- **`project_phosphor_rebrand.md`** — "Phosphor" is the candidate name if PlasmaZones becomes a standalone WM.
- **`project_plugin_based_compositor.md`** — plugin-based compositor/WM/shell direction; no singletons, DI default.
- **`project_phosphor_overlay_scope.md`** — overlay is a `PhosphorLayer` helper; PhosphorLayer is an accepted public dep.
- **`project_layout_source_factory_registry.md`** — `ILayoutSourceFactory` registry pattern in `phosphor-layout-api`; one factory class per source library.
- **`project_scrolling_engine_roadmap.md`** — third layout source planned alongside zones + autotile.
- **`feedback_no_legacy_shims.md`** — refactors update every call site; never leave behind compat shims.

License split (load-bearing): `src/**` apps / daemon / editor / settings = **GPL-3.0-or-later**; `libs/phosphor-*/**` reusable libs = **LGPL-2.1-or-later** so third-party plugins / tools can link without inheriting GPL.

### 1.3 Quickshell as the reference design

The researcher agent's report (transcript at `/tmp/claude-1000/.../tasks/ade5bf5a42644bbcd.output`) established Quickshell's architectural patterns as the closest precedent for what Phosphor wants to be:

- **One QML URI per CMake target.** Quickshell uses `Quickshell.<thing>` namespacing where each `src/<dir>` declares its own QML module via `qt_add_qml_module()` with `URI Quickshell.<Capability>`.
- **`<Brand>` for core, `<Brand>.<Platform>` for protocol code, `<Brand>.<Domain>` for domain libs, `<Brand>.Services.<Service>` for system-service bindings, `<Brand>.Widgets` for UI primitives.**
- **"Shell" is reserved for the runtime/binary, never a lib name.**
- C++ target names kebab-case (`quickshell-wayland`); QML URIs PascalCase (`Quickshell.Wayland`).

**Patterns Phosphor will adopt** (from researcher report §4):
- One QML URI per library
- Generation-based reload pattern (planned for Phase E — see §6)
- A `Reloadable` interface with stable IDs (zones already use UUIDs; extend to shaders/profiles/curves)
- Feature flags (`option(PHOSPHOR_X11_FALLBACK …)`) for downstream packagers
- `Phosphor.Services.*` umbrella for future MPRIS/notifd/PAM bindings

**Patterns Phosphor will NOT adopt** (from researcher report §5):
- No-plugin model (Phosphor needs runtime-pluggable `ILayoutSourceFactory`)
- Three-layer window proxy (solves a Quickshell-specific QQmlEngine teardown problem)
- Single-license whole-binary shape (Phosphor's GPL-app / LGPL-libs split is correct)
- Skipping a custom QPA plugin (`PhosphorLayer` is an accepted public dep)

---

## 2. Naming conventions (Phosphor namespace shape)

Adopted from Quickshell's `<Brand>.<Capability>` discipline:

| Bucket | C++ target (kebab) | QML URI (PascalCase) | Examples |
|---|---|---|---|
| Core | `phosphor-core` *(future, if extracted)* | `Phosphor` | engine generation, reloadable, paths |
| Platform | `phosphor-wayland`, `phosphor-x11` *(future)* | `Phosphor.Wayland`, `Phosphor.X11` | layer-shell QPA, X11 panels |
| Domain | `phosphor-animation`, `phosphor-shaders`, `phosphor-screens`, `phosphor-tiles`, `phosphor-zones` | `Phosphor.Animation`, `Phosphor.Shaders`, `Phosphor.Screens`, `Phosphor.Tiles`, `Phosphor.Zones` | typed registries, runtime APIs |
| Services *(future)* | `phosphor-services-mpris`, `…-notifd`, `…-pam` | `Phosphor.Services.Mpris`, `Phosphor.Services.Notifications`, `Phosphor.Services.Pam` | system-service bindings |
| UI primitives *(future)* | `phosphor-widgets` | `Phosphor.Widgets` | shared QML widgets |
| Internal | `phosphor-fsloader`, `phosphor-engine-api`, `phosphor-layout-api` | (no QML facade) | C++-only contracts |

**Rules:**
1. **One QML URI per CMake target.** No more `*-layer` + `*-qml` arbitrary splits unless they truly export distinct QML modules.
2. **`Phosphor.<Platform>` for protocol code, `Phosphor.<Domain>` for everything else.** Reserve "Shell" for the Phosphor runtime.
3. C++ targets kebab-case; QML URIs PascalCase.
4. Domain libs that publish a QML facade get an `internal QML` sibling target inside the same lib (not a separate library).

---

## 3. Target library tree

| Library | One-line purpose | Owns | Does NOT own |
|---|---|---|---|
| **phosphor-fsloader** | Filesystem scan/watch + JSON envelope + **`MetadataPackScanStrategy<Payload>` (new)** | `WatchedDirectorySet`, `IScanStrategy`, `JsonEnvelopeValidator`, `MetadataPackScanStrategy` | Domain schema parsing |
| **phosphor-shaders** *(new)* | Shader-domain primitives + unified registry + QML facade `Phosphor.Shaders` | `BaseUniforms`, `IUniformExtension`, `ShaderIncludeResolver`, `IWallpaperProvider`, **`ShaderPackRegistry`** | Wayland, RHI |
| **phosphor-wayland** *(rename of `phosphor-shell`)* | Wayland integration: wlr-layer-shell-v1 QPA plugin + `LayerSurface` | `LayerSurface`, `phosphorshell-qpa` plugin (renamed), wayland-scanner protocols | Shaders, rendering |
| **phosphor-rendering** | Qt RHI shader rendering | `ShaderCompiler`, `ShaderEffect`, `ShaderNodeRhi` | Shader-pack discovery |
| **phosphor-animation** | Motion primitives + Profile/Curve registries — **DI-only** | `CurveRegistry`, `PhosphorProfileRegistry` (DI), `QtQuickClockManager` (DI) | QML wrappers |
| **phosphor-animation-qml** *(new — promoted from `phosphor-animation/qml/`)* | Qt Quick wrappers for the motion runtime; QML module `Phosphor.Animation` | `PhosphorMotionAnimation`, `Animated*` QML types | C++ motion primitives |
| **phosphor-animation-shaders** | Animation-transition effect schema + `ShaderProfile`/`ShaderProfileTree` (consumes `phosphor-shaders`) | `AnimationShaderEffect`, `ShaderProfile(Tree)`, parser glue supplying `AnimationShaderEffect` payload | Registry plumbing (gone) |
| **phosphor-animation-layer** | ISurfaceAnimator bridging `phosphor-layer` + `phosphor-animation` | `SurfaceAnimator` | — |
| **phosphor-engine-api** | Engine contracts + base + value types (incl. former `engine-types` headers) | `IPlacementEngine`, `PlacementEngineBase`, `IPlacementState`, `NavigationContext` | — |
| **phosphor-layout-api** | Layout-preview contract + factory registry | unchanged | — |
| **phosphor-zones** | Manual-zone layout primitives | unchanged | Snap runtime |
| **phosphor-snap-engine** | Snap placement engine | unchanged | Layout schema |
| **phosphor-tiles** | Tiling-algorithm primitives + scripted runtime | unchanged | Engine state |
| **phosphor-tile-engine** | Autotile placement engine | unchanged | — |
| **phosphor-layer** | wlr-layer-shell surface lifecycle (DI'd) | unchanged; opt-in `phosphor-wayland` transport renamed | — |
| **phosphor-surfaces** | Managed surface helpers atop `phosphor-layer` | unchanged | — |
| **phosphor-screens** | Screen topology + virtual screens | unchanged | — |
| **phosphor-fsloader** | (already listed above) | | |
| **phosphor-config** | Pluggable config backends + schema migrations | unchanged | Defaults policy |
| **phosphor-protocol** | D-Bus wire types | unchanged | Adaptors |
| **phosphor-identity** | Window-id parser/builder | unchanged | — |
| **phosphor-geometry** | Pure geometry math | unchanged | — |
| **phosphor-shortcuts** | Pluggable global-shortcut backends | unchanged | — |
| **phosphor-audio** | Audio spectrum provider | unchanged | — |
| ~~phosphor-engine-types~~ | Folded into `phosphor-engine-api` | — | — |

**Removed:** `phosphor-engine-types` (folded into `phosphor-engine-api` to fix the namespace mismatch — its namespace was already `PhosphorEngineApi`).

**Renamed:** `phosphor-shell` → `phosphor-wayland`.

**Created:** `phosphor-shaders`, `phosphor-animation-qml`.

---

## 4. Migration plan

Six PRs across four phases. Each PR is atomic per the no-shims rule — no compat aliases, every call site updates in the same commit.

### Phase A — independent (parallelizable)

#### A1: Promote `phosphor-animation/qml/` → `libs/phosphor-animation-qml/`

**Scope:** CMake-only sibling lib promotion. Module URI stays `org.phosphor.animation` (eventually moves to `Phosphor.Animation` post-rebrand — separate concern).
**Files moved:** entire `libs/phosphor-animation/qml/` tree → `libs/phosphor-animation-qml/`.
**Call sites:** parent `libs/CMakeLists.txt`, every `target_link_libraries(... PhosphorAnimation::PhosphorAnimationQml)` consumer.
**Blast radius:** Small.
**Blocks:** nothing.

#### A2: Fold `phosphor-engine-types` headers → `phosphor-engine-api`

**Scope:** Header relocation; namespace already correct (`PhosphorEngineApi`); lib `phosphor-engine-types` deleted.
**Files moved:** `libs/phosphor-engine-types/include/PhosphorEngineApi/{IPlacementState.h, NavigationContext.h}` → `libs/phosphor-engine-api/include/PhosphorEngineApi/`.
**Call sites:** consumers that linked `PhosphorEngineTypes::PhosphorEngineTypes` switch to `PhosphorEngineApi::PhosphorEngineApi`. `find` and grep for the explicit target name.
**Blast radius:** Small (no namespace churn — only the link target changes).
**Blocks:** nothing.

#### A3: Kill `PhosphorProfileRegistry::instance()` + `QtQuickClockManager::instance()`

**Scope:** Delete both `instance()` accessors and the function-local statics behind them. Composition roots (daemon, editor, settings) instantiate via DI; QML side gets the registry via context property.
**Files modified:**
- `libs/phosphor-animation/include/PhosphorAnimation/PhosphorProfileRegistry.h:77` — delete `instance()` declaration
- `libs/phosphor-animation/src/phosphorprofileregistry.cpp:12` — delete the static
- `libs/phosphor-animation/qml/QtQuickClockManager.h:128` + `.cpp:47` — same
- `libs/phosphor-animation/include/PhosphorAnimation/Profile.h:195` — remove the apology comment
- Call sites: `libs/phosphor-animation-layer/src/surfaceanimator.cpp:612`, `phosphormotionanimation.cpp:263,296,310`
- Composition roots: daemon `setupAnimationProfiles()`, editor + settings instantiation
- QML registration: context property handoff in each composition root's QML engine setup
**Blast radius:** Medium — touches every composition root.
**Blocks:** nothing for this phase, but unblocks future plugin-compositor work.

### Phase B — load-bearing extraction

#### B1: Create `libs/phosphor-shaders/`; migrate shader-domain types out of `phosphor-shell`

**Scope:** Move shader-domain headers + sources into a new `phosphor-shaders` library. Includes the existing `ShaderRegistry` move (renamed to `ShaderPackRegistry`). The unification with `AnimationShaderRegistry` happens in Phase C.

**Files moved (`phosphor-shell` → `phosphor-shaders`):**
- `libs/phosphor-shell/include/PhosphorShell/BaseUniforms.h` → `libs/phosphor-shaders/include/PhosphorShaders/BaseUniforms.h`
- `libs/phosphor-shell/include/PhosphorShell/IUniformExtension.h` → `…/IUniformExtension.h`
- `libs/phosphor-shell/include/PhosphorShell/ShaderIncludeResolver.h` + `src/shaderincluderesolver.cpp` → `…`
- `libs/phosphor-shell/include/PhosphorShell/IWallpaperProvider.h` + `src/wallpaperprovider.cpp` → `…`
- `libs/phosphor-shell/include/PhosphorShell/ShaderRegistry.h` + `src/shaderregistry.cpp` → `…/ShaderPackRegistry.{h,cpp}` (rename)

**New CMakeLists:** `libs/phosphor-shaders/CMakeLists.txt` declaring `Phosphor.Shaders` QML module via `qt_add_qml_module()`.

**Dep flips:**
- `libs/phosphor-rendering/CMakeLists.txt:105`: `PhosphorShell::PhosphorShell` → `PhosphorShaders::PhosphorShaders`
- `libs/phosphor-rendering/include/PhosphorRendering/ShaderNodeRhi.h:8-9`: `<PhosphorShell/...>` → `<PhosphorShaders/...>`
- `libs/phosphor-rendering/src/{shadercompiler,shadereffect,shadernoderhisetters}.cpp`: same include sweep

**Call sites in `src/`:** ~28 `PhosphorShell::` references in `src/` related to shader-domain types — every one rewrites to `PhosphorShaders::`. Find them with:
```
grep -rn 'PhosphorShell::\(BaseUniforms\|IUniformExtension\|ShaderIncludeResolver\|IWallpaperProvider\|ShaderRegistry\)' src/
```

**`PlasmaZones::ShaderRegistry` (the subclass at `src/core/shaderregistry.h`):** rebases on `PhosphorShaders::ShaderPackRegistry`. The Q_INVOKABLE conveniences (`userShadersEnabled`, `userShaderDirectory`, `openUserShaderDirectory`) stay.

**Blast radius:** Largest of all PRs — header sweep across `src/`, `libs/phosphor-rendering/`, `libs/phosphor-shell/`, plus the new lib's CMake.
**Blocks:** Phase C and Phase D.

### Phase C — kill the duplicated metadata-pack walker

#### C1: Add `MetadataPackScanStrategy<Payload>` to `phosphor-fsloader`

**Scope:** Templated (or virtual `IMetadataPackPayload`) scan strategy that owns the metadata.json subdir walk, reverse-iterate first-wins, kMax cap, SHA-1/QHash signature, isUser classification, sorted-by-id output. Tests-first using existing `test_shaderregistry.cpp` and `test_animationshaderregistry.cpp` as the contract.

**Files added:**
- `libs/phosphor-fsloader/include/PhosphorFsLoader/MetadataPackScanStrategy.h` (templated header)
- `libs/phosphor-fsloader/tests/test_metadatapackscanstrategy.cpp`

**Blast radius:** Small (additive).
**Blocks:** C2.

#### C2: Re-implement `ShaderPackRegistry` on top of C1; collapse `AnimationShaderRegistry` registry plumbing

**Scope:** `ShaderPackRegistry` (in `phosphor-shaders` after B1) becomes a thin specialisation of `MetadataPackScanStrategy<ShaderInfo>`. `AnimationShaderRegistry` (in `phosphor-animation-shaders`) becomes a thin specialisation of `MetadataPackScanStrategy<AnimationShaderEffect>` — **survives as a distinct lib** because `ShaderProfile`/`ShaderProfileTree` are unambiguously animation-domain.

**Files modified:**
- `libs/phosphor-shaders/src/shaderpackregistry.cpp` — collapse onto `MetadataPackScanStrategy`
- `libs/phosphor-animation-shaders/src/animationshaderregistry.cpp` — collapse onto `MetadataPackScanStrategy`
- Both files lose ~150 lines each of duplicated walker/cap/signature code

**Opportunistic follow-up** (separate PR if scope expands): `libs/phosphor-tiles/src/scriptedalgorithmloader.cpp` — third metadata-pack walker — can also collapse onto `MetadataPackScanStrategy` once the template generalises across non-shader payloads. NOT required for Phase C completion.

**Blast radius:** Medium — both registry implementations rewrite, but their public APIs are unchanged.
**Blocks:** nothing.

### Phase D — finalize the rename

#### D1: Rename `phosphor-shell` → `phosphor-wayland`

**Scope:** Pure mechanical rename after Phase B emptied the lib of shader-domain code. Only `LayerSurface` and the `phosphorshell-qpa` plugin remain.

**Files renamed:**
- `libs/phosphor-shell/` → `libs/phosphor-wayland/`
- Namespace `PhosphorShell::` → `PhosphorWayland::` everywhere (LayerSurface, the QPA plugin)
- QPA plugin name `phosphorshell-qpa` → `phosphorwayland-qpa` (or `phosphor-qpa` — TBD during PR)
- Logging categories `phosphorshell.*` → `phosphorwayland.*`

**Dep flips:**
- `libs/phosphor-layer/CMakeLists.txt`: `PHOSPHORLAYER_WITH_PHOSPHORSHELL` → `PHOSPHORLAYER_WITH_PHOSPHORWAYLAND`
- Every consumer of `PhosphorShell::LayerSurface` updates to `PhosphorWayland::LayerSurface`

**Blast radius:** Medium — namespace sweep, but at this point only ~5 consumers of `LayerSurface` survive.
**Blocks:** nothing (final phase).

### Phase E — Reloadable + Generation pattern *(deferred — post-rebrand)*

**Scope:** Adopt Quickshell's whole-graph atomic reload pattern on top of the existing per-registry change-only emission. Introduces a `Reloadable` interface (stable id + `reload(prevInstance)`), a `Generation` concept that builds a fresh shadow state from scratch and atomically swaps on success, and a `RootWrapper` that owns the live generation pointer.

**Trigger for activation:** When user-authored QML or scripted layouts can reference fsloader-discovered IDs across registries (e.g., a profile referencing a shader id that's mid-rename), cross-registry consistency starts to matter. Today's per-registry atomicity is sufficient for the loose coupling that exists.

**Status:** Documented as future work; not blocking any current phase.

---

## 5. Migration ordering and dependencies

```
        ┌──────┐  ┌──────┐  ┌──────┐
        │  A1  │  │  A2  │  │  A3  │   (parallel, independent)
        └──┬───┘  └──┬───┘  └──┬───┘
           └─────────┴─────────┘
                     │
                ┌────▼────┐
                │   B1    │   (load-bearing — biggest blast radius)
                └────┬────┘
                     │
              ┌──────┴──────┐
              │             │
         ┌────▼─┐      ┌────▼─┐
         │  C1  │──▶───│  C2  │   (C2 depends on C1)
         └──────┘      └────┬─┘
                            │
                       ┌────▼─┐
                       │  D1  │   (mechanical rename)
                       └──────┘

                    ┌──────────────┐
                    │  E (future)  │   (post-rebrand)
                    └──────────────┘
```

**PR sequencing:** A1/A2/A3 ship in parallel as three separate PRs. B1 lands once they're all merged. C1 and C2 land sequentially. D1 closes out the chain.

**Total: 6 PRs across 4 phases.** Phase E is a future trigger, not part of this refactor.

---

## 6. Resolved open questions

| # | Question | Resolution | Reasoning |
|---|---|---|---|
| 1 | Should `AnimationShaderRegistry` survive Phase C as a thin specialisation, or collapse into `phosphor-shaders` entirely? | **Survive** | `ShaderProfile`/`ShaderProfileTree` are unambiguously animation-domain — they fan out per `MotionEvent`. The lib retains its identity even after the registry plumbing lifts out. |
| 2 | `phosphor-shaders` C++-only, or expose QML facade `Phosphor.Shaders`? | **QML facade** | Mirrors Quickshell's `Quickshell.Services.*` pattern: registry-style libs that QML consumes get a thin QML facade. `Phosphor.Shaders.ShaderPackRegistry` becomes importable from QML. |
| 3 | Adopt Quickshell's Reloadable + Generation pattern? | **Phase E (deferred)** | Today's per-registry atomic-swap model is sufficient for the loose cross-registry coupling that exists. Generation pattern becomes load-bearing once user-authored QML or scripted layouts cross-reference fsloader-discovered IDs. |
| 4 | Where does `phosphor-engine-types` merge? | **Into `phosphor-engine-api`** | The namespace mismatch (`PhosphorEngineApi` namespace inside a `phosphor-engine-types` lib) was always going to be a rebrand trap. Headers move into `engine-api/`; the artificial split disappears. |

---

## 7. License compliance

Every move respects the `src/**` GPL-3.0-or-later / `libs/phosphor-*/**` LGPL-2.1-or-later split:

| New / renamed lib | License | Justification |
|---|---|---|
| `phosphor-shaders` | LGPL-2.1-or-later | Reusable lib; third-party shells must be able to link |
| `phosphor-animation-qml` | LGPL-2.1-or-later | Reusable lib (matches sibling `phosphor-animation`) |
| `phosphor-wayland` (renamed from `phosphor-shell`) | LGPL-2.1-or-later | Inherits from `phosphor-shell`'s existing license |

SPDX headers on every moved/created file. Per the no-shims rule, original copies are deleted, not aliased.

---

## 8. Testing posture

- **A1**: Existing `phosphor-animation` QML tests run unchanged after the move (test target follows the lib).
- **A2**: Engine consumer tests under `tests/unit/engine*/` validate the merged headers.
- **A3**: Composition-root smoke tests (daemon startup, editor open, settings load) catch missing DI wiring. Tests that constructed via `instance()` rebind to the DI'd registry.
- **B1**: Header sweep validated by full-suite `ctest --output-on-failure`. The shader-domain test files (`test_shaderregistry.cpp`, etc.) move from `phosphor-shell/tests/` to `phosphor-shaders/tests/`.
- **C1**: New test file `test_metadatapackscanstrategy.cpp` with the contract pinned (existing registry tests inform the test list).
- **C2**: Both registry test files (`test_shaderregistry.cpp` rebased on `ShaderPackRegistry`; `test_animationshaderregistry.cpp`) continue to pass — public API unchanged.
- **D1**: Rename test discovery confirms no stale `PhosphorShell::` references survive.

---

## 9. Out of scope

The following surfaced during analysis but are **deferred** to follow-up surveys:

1. **`BakeCache` singleton in `phosphor-rendering`** (`shadercompiler.cpp:52`) — under the plugin-compositor model, a process-wide content-addressed shader-bake cache is beneficial, but it should become an explicit DI'd `IShaderBakeCache` so plugin teardown is well-defined.
2. **Compositor-plugin SDK formalization** — flagged as 4/5 feasibility in `docs/library-extraction-survey.md`. The single most strategic extraction for the compositor/WM/shell endgame, but blocked on this round of cleanup landing first.
3. **Generation/Reloadable** — Phase E (above).
4. **`phosphor-tiles::scriptedalgorithmloader.cpp` collapse onto `MetadataPackScanStrategy`** — opportunistic follow-up to Phase C if the template generalises cleanly.
5. **Quickshell-style `phosphor://` URL scheme** — useful when third-party plugins start contributing assets; not yet warranted.
6. **`Phosphor.Services.*` umbrella** — future MPRIS/notifd/PAM bindings; aspirational only at this stage.

---

## 10. Provenance

- **Internal audit:** system-architect agent transcript at `/tmp/claude-1000/.../tasks/aa1d6e8b4546be6d2.output`
- **External research:** researcher agent transcript at `/tmp/claude-1000/.../tasks/ade5bf5a42644bbcd.output`
- **Quickshell source:** https://git.outfoxxed.me/outfoxxed/quickshell
- **Quickshell docs:** https://quickshell.outfoxxed.me/docs/types/
- **Astal libraries reference:** https://aylur.github.io/astal/guide/libraries/references
- **Predecessor docs:** `docs/library-architecture-audit.md`, `docs/library-architecture-analysis.md`, `docs/library-extraction-survey.md`
- **Project memory:** `~/.claude/projects/-home-nlavender-Documents-PlasmaZones2/memory/` — `project_phosphor_rebrand.md`, `project_plugin_based_compositor.md`, `feedback_no_legacy_shims.md`
