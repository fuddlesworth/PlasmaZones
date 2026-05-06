# kwin-effect refactor plan

Branch: `refactor/kwin-effect-split` (off `v3`).

## Why

`kwin-effect/plasmazoneseffect.cpp` is **6075 lines**. CLAUDE.md caps source files at 800 lines. Three files violate the cap today:

| File | Lines | Notes |
|---|---|---|
| `kwin-effect/plasmazoneseffect.cpp` | **6075** | This phase. |
| `kwin-effect/plasmazoneseffect.h` | 1195 | Phase 2 (deferred). |
| `kwin-effect/autotilehandler.cpp` | 981 | Phase 3 (deferred). |

This is a pure file split. No public API change, no member moves between classes, no behavior change. Methods stay member functions of `PlasmaZonesEffect`; only the .cpp containing each method body changes.

## Phase 1 target layout

```
kwin-effect/
├── plasmazoneseffect.h                           (1195, unchanged)
├── plasmazoneseffect.cpp                         (~250 lines)
└── plasmazoneseffect/
    ├── lifecycle.cpp                              (~700 lines)
    ├── window_lifecycle.cpp                       (~600 lines)
    ├── window_identity.cpp                        (~160 lines)
    ├── window_filtering.cpp                       (~240 lines)
    ├── mouse_drag.cpp                             (~120 lines)
    ├── daemon_bringup.cpp                         (~680 lines)
    ├── screens.cpp                                (~390 lines)
    ├── daemon_apply.cpp                           (~660 lines)
    ├── drag_snap.cpp                              (~620 lines)
    ├── borders.cpp                                (~170 lines)
    ├── paint_pipeline.cpp                         (~520 lines)
    └── shader_transitions.cpp                     (~960 lines, see note)
```

### Per-file member assignments

Line ranges below are **source line numbers in the current `plasmazoneseffect.cpp`**.

#### `plasmazoneseffect.cpp` (kept thin)

- File-level includes that everything needs (top of the existing file).
- The `KWIN_EFFECT_FACTORY_SUPPORTED` macro (currently at the bottom).
- The `#include "plasmazoneseffect.moc"` line (must stay in this exact TU — `Q_OBJECT` is defined here and AutoMoc emits `plasmazoneseffect.moc` keyed to this filename).
- Effect-interface members:
  - `supported` (1072–1078)
  - `enabledByDefault` (1079–1083)
  - `reconfigure` (1084–1090)
  - `isActive` (1091–1109)
  - `grabbedKeyboardEvent` (1110–1123)

#### `plasmazoneseffect/lifecycle.cpp`

- `PlasmaZonesEffect::PlasmaZonesEffect` (425–982, 558 lines)
- `PlasmaZonesEffect::~PlasmaZonesEffect` (983–1071, 89 lines)

The constructor is huge (558 lines) but a single function — splitting it would be a function-level refactor, out of scope here.

#### `plasmazoneseffect/window_lifecycle.cpp`

- `slotWindowAdded` (1124–1223)
- `slotWindowClosed` (1224–1284)
- `slotWindowActivated` (1285–1296)
- `setupWindowConnections` (1297–1573)
- `notifyWindowClosed` (4235–4251)
- `notifyWindowActivated` (4252–4318)
- `findWindowById` (4319–4350)
- `findAllWindowsById` (4351–4385)

#### `plasmazoneseffect/window_identity.cpp`

- `getWindowId` (2090–2125)
- `getWindowInstanceId` (2126–2137)
- `getWindowAppId` (2138–2159)
- `pushWindowMetadata` (2160–2180)
- `flushPendingFrameGeometry` (2181–2196)
- `isPlasmaShellSurface` (2197–2209)

#### `plasmazoneseffect/window_filtering.cpp`

- `shouldHandleWindow` (2210–2265)
- `isTileableWindow` (2266–2288)
- `hasOtherWindowOfClassWithDifferentPid` (2289–2318)
- `isDaemonReady` (2319–2327)
- `syncFloatingWindowsFromDaemon` (2328–2335)
- `isWindowFloating` (420–424)
- `isWindowSticky` (3652–3656)
- `updateWindowStickyState` (3657–3681)
- `buildWindowMap` (396–407)
- `getValidActiveWindowOrFail` (409–419)
- `getActiveWindow` (2615–2634)
- `ensurePreSnapGeometryStored` (359–394) — small enough that grouping with the other "pre-action" helpers is fine; alternative is `drag_snap.cpp`.

#### `plasmazoneseffect/mouse_drag.cpp`

- `slotMouseChanged` (1574–1675)
- `applyStaggeredOrImmediate` (1676–1689)

The trigger constants `TriggerModifierField` / `TriggerMouseButtonField` (lines 87–88) move to this file's anonymous namespace — they are only used by `slotMouseChanged` / trigger parsing.

#### `plasmazoneseffect/daemon_bringup.cpp`

- `slotDaemonReady` (1690–1764)
- `continueDaemonReadySetup` (1765–1874)
- `processDaemonReadyWindowState` (1875–2076)
- `slotSettingsChanged` (2077–2089)
- `loadSettingAsync` (2336–2340) — explicit instantiation may be needed; alternative is to keep this template's body in the header. **Decision:** keep it as a header-defined template (move to a `// inline template body` block in `plasmazoneseffect.h` or a private `plasmazoneseffect/load_setting_async.h`). Body is 5 lines so it stays trivial.
- `loadCachedSettings` (2341–2492)
- `anyLocalTriggerHeld` (2493–2497)
- `detectActivationAndGrab` (2498–2522)
- `connectNavigationSignals` (2523–2614)

#### `plasmazoneseffect/screens.cpp`

- `outputScreenId` (2635–2683)
- `getWindowScreenId` (2684–2696)
- `resolveEffectiveScreenId` (2697–2756)
- `fetchVirtualScreenConfig` (2757–2893)
- `fetchAllVirtualScreenConfigs` (2894–2940)
- `onVirtualScreensChanged` (2941–2952)
- `clockForOutput` (4553–4563)
- `onScreenAdded` (4564–4580)
- `onScreenRemoved` (4581–4622)

#### `plasmazoneseffect/daemon_apply.cpp`

- `emitNavigationFeedback` (2953–2965)
- `slotActivateWindowRequested` (2966–2975)
- `slotMoveSpecificWindowToZoneRequested` (2976–3046)
- `slotApplyGeometryRequested` (3047–3120)
- `slotApplyGeometriesBatch` (3121–3249)
- `slotRaiseWindowsRequested` (3250–3267)
- `slotSnapAllWindowsRequested` (3268–3404)
- `slotPendingRestoresAvailable` (3405–3473)
- `slotWindowFloatingChanged` (3474–3490)
- `slotWindowMinimizedChanged` (3491–3543)
- `slotRunningWindowsRequested` (3544–3608)

#### `plasmazoneseffect/drag_snap.cpp`

- `borderActivated` (3609–3616)
- `callResolveWindowRestore` (3617–3651)
- `callEndDrag` (3682–3861)
- `callCancelSnap` (3862–3871)
- `tryAsyncSnapCall` (3872–3914)
- `repaintSnapRegions` (3915–3931)
- `applySnapGeometry` (3932–4092)
- `slotRestoreSizeDuringDrag` (4093–4122)
- `slotSnapAssistReady` (4123–4138)
- `slotDragPolicyChanged` (4139–4234)

The `EndDragTimeoutMs` constant (line 108) moves to this file's anonymous namespace.

#### `plasmazoneseffect/borders.cpp`

- `removeWindowBorder` (4386–4415)
- `clearAllBorders` (4416–4422)
- `updateWindowBorder` (4423–4530)
- `updateAllBorders` (4531–4552)

#### `plasmazoneseffect/paint_pipeline.cpp`

- `prePaintScreen` (4623–4685)
- `postPaintScreen` (4686–4719)
- `prePaintWindow` (4720–4734)
- `paintWindow` (4735–5134)

`paintWindow` is 400 lines. Inside the hot path it reads several anonymous-namespace helpers from the shader subsystem (`kUserTextureSamplerNames`, `kUserTextureWrapKeys`, `kITextureResolutionKeys`, `kCustomParamsElementNames`, `kCustomColorsElementNames`, `shaderClockNowMs`). Those helpers are needed by `shader_transitions.cpp` too — see shared-helpers note below.

#### `plasmazoneseffect/shader_transitions.cpp`

- `evictLruTextureIfOverBound` (5135–5177)
- `warmUserTextureAsync` (5178–5296)
- `beginShaderTransition` (5297–5900) — **604 lines on its own**
- `endShaderTransition` (5901–5964)
- `tryBeginShaderForEvent` (5965–6026)
- `loadShaderProfileFromDbus` (6027–6041)
- `loadShaderRegistryFromDbus` (6042–6075)

This file ends up at ~960 lines. **It will exceed the 800-line cap** until `beginShaderTransition` is itself decomposed into helpers (out of scope here). The line-count situation is still vastly better than today's 6075. A follow-up function-level refactor of `beginShaderTransition` is tracked in the parking-lot section below.

### Shared anonymous-namespace helpers

The current top-of-file anonymous namespace (lines 81–347) holds:

| Helper | Used by |
|---|---|
| `shaderClockNowMs()` | `paint_pipeline.cpp`, `shader_transitions.cpp` |
| `injectKwinDefineAfterVersion()` | `shader_transitions.cpp` |
| `loadUserTextureImage()` | `shader_transitions.cpp` |
| `wrapStringToEnum()` | `shader_transitions.cpp` |
| `kUserTextureSamplerNames`, `kUserTextureWrapKeys`, `kITextureResolutionKeys`, `kCustomParamsElementNames`, `kCustomColorsElementNames` | `paint_pipeline.cpp`, `shader_transitions.cpp` |
| `EndDragTimeoutMs` | `drag_snap.cpp` only |
| `TriggerModifierField`, `TriggerMouseButtonField` | `mouse_drag.cpp` / settings only |

Two `.cpp` files (`paint_pipeline.cpp` + `shader_transitions.cpp`) need shared access to a subset of these. Approach:

Create **`kwin-effect/plasmazoneseffect/shader_internal.h`** as a private header (not exported, not in any install set). It declares:

- `inline qint64 shaderClockNowMs();`
- The five `kFoo` `constexpr std::array` constants (header-safe, ODR-compliant via `inline constexpr`).
- The `static_assert` length pins.
- Forward declarations of `injectKwinDefineAfterVersion` / `loadUserTextureImage` / `wrapStringToEnum` if those end up needed in more than one TU; otherwise leave them as anonymous-namespace helpers in `shader_transitions.cpp`.

Single-TU helpers (`EndDragTimeoutMs`, `TriggerModifierField`, `TriggerMouseButtonField`, `injectKwinDefineAfterVersion`, `loadUserTextureImage`, `wrapStringToEnum`) move into the anonymous namespace of the one .cpp that uses them. No header machinery for those.

### Friend / access notes

The class declaration in `plasmazoneseffect.h` already grants `friend class` access to: `AutotileHandler`, `NavigationHandler`, `ScreenChangeHandler`, `SnapAssistHandler`, `WindowAnimator`, `DragTracker`, `KWinCompositorBridge`. **Member-function definitions across multiple .cpp files need no friend declarations** — they are already members of `PlasmaZonesEffect`. The split is a TU partition, not an access change.

### Per-file headers

Each new `.cpp` only includes what it uses. The current top-of-file include block (`<algorithm>`, `<chrono>`, ~30 Qt headers, `<window.h>`, etc.) is per-method-needed; trying to hand-pick minimal includes per file is overengineering for this pass. **Decision:** every new .cpp starts with the same SPDX header + `#include "plasmazoneseffect.h"` + the union of headers needed by the methods it carries. If a header turns out unused, drop it; otherwise leave it.

## CMake change

`kwin-effect/CMakeLists.txt` adds the new sources to `plasmazones_effect_SRCS`:

```cmake
set(plasmazones_effect_SRCS
    plasmazoneseffect.cpp
    plasmazoneseffect.h
    plasmazoneseffect/lifecycle.cpp
    plasmazoneseffect/window_lifecycle.cpp
    plasmazoneseffect/window_identity.cpp
    plasmazoneseffect/window_filtering.cpp
    plasmazoneseffect/mouse_drag.cpp
    plasmazoneseffect/daemon_bringup.cpp
    plasmazoneseffect/screens.cpp
    plasmazoneseffect/daemon_apply.cpp
    plasmazoneseffect/drag_snap.cpp
    plasmazoneseffect/borders.cpp
    plasmazoneseffect/paint_pipeline.cpp
    plasmazoneseffect/shader_transitions.cpp
    plasmazoneseffect/shader_internal.h
    autotilehandler.cpp
    autotilehandler.h
    autotilehandler/signals.cpp
    autotilehandler/tiling.cpp
    # … unchanged below …
)
```

AutoMoc only needs to scan `plasmazoneseffect.h` (where `Q_OBJECT` lives). `plasmazoneseffect.cpp` keeps the `#include "plasmazoneseffect.moc"` line — that .moc is generated from the `.h` and bound to the `.cpp` by filename pairing.

## Execution order

Do this in **one commit** per logical step so reverting a step is cheap:

1. **Setup** – create `kwin-effect/plasmazoneseffect/` directory and the empty `shader_internal.h`. Update `CMakeLists.txt` to list (initially nonexistent) targets. Verify `cmake --build build` fails with "missing source" (sanity check that CMake sees the new entries). Skip if you'd rather add files first then update CMake.
2. **Lifecycle file** (lowest blast radius): create `plasmazoneseffect/lifecycle.cpp` containing only the constructor + destructor. Delete those bodies from `plasmazoneseffect.cpp`. Build + ctest. Commit `refactor(kwin-effect): extract ctor/dtor to lifecycle.cpp`.
3. **Identity, filtering, mouse, borders** (small, mostly leaf): one commit each, same pattern. Build between commits.
4. **Window lifecycle, daemon bringup, daemon apply, drag snap, screens** (medium-size): one commit each.
5. **Paint pipeline + shader transitions** (largest, also the tricky shared-helpers pair): land together in a single commit so the `shader_internal.h` header is introduced atomically with the two TUs that use it.
6. **Final verification:** full build + ctest; spot-check that `plasmazoneseffect.cpp` is now ~250 lines and only contains the .moc include + factory + effect-interface methods.

## Validation strategy

Per-step:

```bash
cmake --build build --parallel $(nproc) --target kwin_effect_plasmazones
cd build && ctest --output-on-failure
```

After all steps:

- Check for orphan symbols: every method declared in `plasmazoneseffect.h` should have exactly one definition. A missing definition surfaces as a link error (`undefined reference to PlasmaZonesEffect::foo`); a duplicate surfaces as `multiple definition`. A grep-based cross-check in `scripts/check-no-duplicate-symbols.sh` (if it doesn't exist, an ad-hoc `grep -rn "PlasmaZonesEffect::" kwin-effect/*.cpp kwin-effect/plasmazoneseffect/*.cpp` followed by uniq -c is enough).
- Run an interactive KWin session and exercise: drag a window, snap to zone, snap-assist popup, autotile flip-screen, shader transition on window open/close. **Do not skip this** — file-only refactors can still break by accidentally moving a static-storage initializer across TUs and changing initialization order.

## Out of scope (parking lot)

These violate the same 800-line cap but are not part of Phase 1:

- **`plasmazoneseffect.h` (1195 lines)** — extract `ShaderTransition`, `CachedTexture`, `CachedShader`, `WindowBorder`, `EffectVirtualScreenDef`, `CachedSnapRestore` to `plasmazoneseffect/shader_types.h` and `plasmazoneseffect/effect_types.h`. Requires moving them out of `PlasmaZonesEffect` private scope or adding nested-type forward declarations. Phase 2.
- **`autotilehandler.cpp` (981 lines)** — already has `autotilehandler/{signals,tiling}.cpp`. Add a third partition. Phase 3.
- **`beginShaderTransition` (604 lines)** — function-level decomposition (helpers for shader compile, texture resolution, slot translation, transition install). Would let `shader_transitions.cpp` drop under 800. Function-level refactor, not Phase 1.
- **`paintWindow` (400 lines)** and **`PlasmaZonesEffect::PlasmaZonesEffect()` (558 lines)** — same story; could be cut into helpers but out of scope.
- **`setupWindowConnections` (277 lines)**, **`processDaemonReadyWindowState` (202 lines)**, **`callEndDrag` (180 lines)**, **`applySnapGeometry` (161 lines)** — long single functions. Function-level refactor candidates.

## Risks

- **MOC binding to filename.** `plasmazoneseffect.cpp` must keep the `#include "plasmazoneseffect.moc"` line and the `KWIN_EFFECT_FACTORY_SUPPORTED` macro. Moving either to a different .cpp will break either the QObject metadata or the plugin factory binding. Already accounted for in the layout above.
- **Static initializer order.** None of the file-scope `Q_LOGGING_CATEGORY(lcEffect, ...)` or constexpr arrays cross-reference each other, so reshuffling them across TUs should be order-safe — but verify by `nm` if the Wayland session shows any "missing logging category" warnings on first plugin load.
- **Template instantiation.** `loadSettingAsync<Fn>` is a member template. Its body must be visible to every caller. Decision above keeps it as a header-defined template body.
- **Friend access vs nested types.** Phase 1 does not move nested types; `friend class` lists in the header stay valid. Phase 2 will need a second look.
