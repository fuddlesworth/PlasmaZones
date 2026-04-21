# Library Extraction Survey

**Date:** 2026-04-18 (original); revised 2026-04-20 to fold in PR #343 + #345 outcomes; revised 2026-04-21 to add `phosphor-audio` extraction.
**Scope:** Candidate inventory for further `phosphor-*` library extractions, in service of the long-term compositor / WM / shell goal.
**Status:** Strategic survey — not a migration plan. Each candidate is backed by a focused investigation (see "Investigation provenance" at the end).

---

## Context

Significant decoupling work has already landed: `phosphor-animation`, `phosphor-audio`, `phosphor-config`, `phosphor-identity`, `phosphor-layer`, `phosphor-layout-api`, `phosphor-rendering`, `phosphor-screens`, `phosphor-shell`, `phosphor-shortcuts`, `phosphor-tiles`, `phosphor-zones`. This document maps what remains in `src/` (and `kwin-effect/`) to potential additional libraries, ranks them by value × feasibility, and calls out what should **not** be extracted.

The core strategic insight: the daemon has **zero** `KWin::` references in `src/`. Compositor-portability is an *extraction problem*, not an architecture problem.

---

## Progress since original survey (2026-04-18 → 2026-04-20)

### Libraries extracted

- **`phosphor-animation`** — `easingcurve`, `animation_math`, `stagger_timer` moved in from `compositor-common/`; `Profile` / `ProfileTree` / `CurveRegistry` / `Spring` added. Motion-runtime scope realized.
- **`phosphor-screens`** — `screenmanager`, `virtualscreen`, `virtualscreenswapper`, `plasmapanelsource` moved in. Daemon keeps `ScreenModeRouter` (correctly out of scope per original survey). Sensor-authoritative + D-Bus-ratio availability reconciliation landed in `manager.cpp::calculateAvailableGeometry` (PR #345).
- **`phosphor-zones`** grew the concrete **`LayoutRegistry`** (previously `LayoutManager` in `src/core/`) plus `AssignmentEntry` (PR #345 commit `d6f74fc7`). Reverses the original Tier 4 "don't extract LayoutManager" verdict — see the note below that entry for what changed.
- **`phosphor-audio`** — `CavaService` extracted from `src/daemon/` into `libs/phosphor-audio/` as `CavaSpectrumProvider` behind a new `IAudioSpectrumProvider` abstract interface. Motivated by the compositor/WM/shell endgame: audio spectrum data is a system-level service that panel widgets, desktop effects, lock screen animations, and third-party plugins should all be able to consume. LGPL-licensed for third-party linkability. `Audio::MinBars` / `Audio::MaxBars` constants moved from `src/core/constants.h` to `PhosphorAudio::Defaults`. Both daemon (`OverlayService`) and editor (`EditorController`) now link `PhosphorAudio::PhosphorAudio` instead of compiling `cavaservice.cpp` directly. Resolves overlay extraction obstacle #5 ("CavaService hardwired"): overlay can now take `IAudioSpectrumProvider*` via constructor injection. See `docs/phosphor-audio-api-design.md` for the full interface design.

### Process-global singletons eliminated (PR #343 + #345)

Every lib-level singleton except the two documented as structurally-process-global (`BakeCache`, `LayerShellIntegration`) is now per-process DI, owned by the composition root:

| Singleton | Home lib | Replacement shape | PR |
|---|---|---|---|
| `AlgorithmRegistry::instance()` | `phosphor-tiles` | `unique_ptr` on Daemon, threaded through every consumer | #343 |
| `ShaderRegistry::instance()` | `src/core` (PZ subclass over `PhosphorShell::ShaderRegistry`) | `unique_ptr` on Daemon, injected into `OverlayService` ctor + `SettingsAdaptor` + `ShaderAdaptor` | #345 |
| `ScriptedAlgorithmWatchdog::instance()` | `phosphor-tiles` | `shared_ptr` per loader, shared with every `ScriptedAlgorithm` it constructs (required — registry uses `deleteLater`, algo dtor can outlive loader) | #345 |
| `CurveRegistry::instance()` | `phosphor-animation` | Per-process value; `Profile::fromJson` / `ProfileTree::fromJson` take `const CurveRegistry&` | #345 |

**Implication for future extractions:** any new library MUST follow the same shape — a concrete registry/service type the composition root owns, not a `static instance()` accessor. Plugin-based compositor topologies require this (plugins can't share process-global state safely).

### Interface collapse in `phosphor-zones` (PR #345 `d6f74fc7`)

`ILayoutManager`, `ILayoutAssignments`, `IQuickLayouts`, `IBuiltInLayouts`, `ILayoutPersistence` were deleted. The lib now exports `IZoneLayoutRegistry` (narrow provider contract) + concrete `LayoutRegistry` — mirrors `ITileAlgorithmRegistry` + `AlgorithmRegistry` in `phosphor-tiles`. This resolves the "interface-cast violation" obstacle that was listed under Tier 2 overlay.

---

## Cross-cutting finding: `compositor-common/` is a god-object, not a library

`src/compositor-common/` is a shim zone, not a coherent domain. Its own `CMakeLists.txt` comment confesses the mix: *"easing curves, animation math, D-Bus helpers, JSON parsing, trigger parsing, and other portable code."* Five unrelated domains glued together by the accident of "both the daemon and the KWin effect need this."

Naming smell test: every clean `libs/phosphor-*` is named after its *domain* (config, identity, layer, layout-api, rendering, shell, shortcuts, tiles, zones). Only `compositor-common` is named after a *relationship*. If you can't name a library after its domain, it isn't one.

### Decomposition of `src/compositor-common/` (audit verdicts)

| File | Verdict | Target | Notes |
|---|---|---|---|
| `easingcurve.{h,cpp}` | ✅ Confirmed | `phosphor-animation` | Pure math, zero non-Qt deps |
| `animation_math.{h,cpp}` | ✅ Confirmed | `phosphor-animation` | Pure composition math |
| `stagger_timer.{h,cpp}` | ✅ Confirmed | `phosphor-animation` | QTimer-based, compositor-agnostic |
| `debounced_action.h` | ⚠️ Unused | drop or park | No live consumers; MOC stub only |
| `snap_assist_filter.{h,cpp}` | ✅ Confirmed | `phosphor-tiles` | Filters candidates via `ICompositorBridge`; *not* snap geometry |
| `trigger_parser.{h,cpp}` | ✅ Confirmed | `phosphor-shortcuts` | Hardcoded enum values 0-10 documented, not a blocker |
| `screen_id.{h,cpp}` | ✅ Confirmed | `phosphor-identity` | Already shimmed |
| `window_id.h` | ✅ Confirmed | `phosphor-identity` | Extracted; consumers `#include <PhosphorIdentity/WindowId.h>` directly |
| `dbus_types.{h,cpp}` | ✅ Confirmed | `phosphor-shell` / new `phosphor-ipc` | Keep triple together |
| `dbus_constants.h` | ✅ Confirmed | same as `dbus_types` | |
| `dbus_helpers.h` | 🔗 **Blocked** | same as `dbus_types` | Uses `lcCompositorCommon`; resolve logging ownership first |
| `compositor_bridge.h` | ✅ Confirmed | compositor-plugin SDK | Pure interface |
| `autotile_state.h` | ✅ Confirmed | compositor-plugin SDK or `phosphor-tiles` | POD + inline helpers |
| `floating_cache.h` | ✅ Confirmed | compositor-plugin SDK | Thin wrapper, no KWin API |
| `geometry_helpers.h` | ✅ Confirmed | `phosphor-core` / geometry lib | One inline function |
| `logging.{h,cpp}` | ⚠️ Cross-cutting | move with `dbus_helpers` or keep | Only `dbus_helpers` uses `lcCompositorCommon` |

**13 of 15 files extract cleanly.** The one real blocker is logging-category ownership for `dbus_helpers`; the one cleanup is deciding `debounced_action.h`'s fate.

### Files that must stay together

- `dbus_types` + `dbus_constants` + `dbus_helpers` — form the D-Bus contract layer.
- `animation_math` + `easingcurve` — the former depends on `EasingCurve`.

---

## Tier 1 — Ready, high value

### `phosphor-animation` (motion-runtime) — ✅ EXTRACTED

**Status:** Landed. The three compositor-common files (`easingcurve`, `animation_math`, `stagger_timer`) moved in; `Profile`, `ProfileTree`, `Spring`, and `CurveRegistry` were added. `CurveRegistry` is per-process DI (PR #345) — `Profile::fromJson` / `ProfileTree::fromJson` take `const CurveRegistry&`, no process-global singleton. Wire-format accepts both bare `"x1,y1,x2,y2"` and prefixed `"bezier:..."` cubic-bezier forms, case-insensitively. The motion-runtime ambitions (window motion bridge, `IMotionClock`, QML value types) remain future work; the original notes below are retained for that next phase.

---

#### Original Tier 1 analysis (now historical):

**File inventory:**
- Moves from `compositor-common/`: `easingcurve.{h,cpp}`, `animation_math.{h,cpp}`, `stagger_timer.{h,cpp}`, (`debounced_action.h` optional).
- Splits from `kwin-effect/windowanimator.{h,cpp}`:
  - ~95 portable lines: state (`QHash<KEY, WindowAnimation>`), config properties, `advanceAnimations(presentTime)` progression → library.
  - ~114 KWin-wrapper lines: `EffectWindow*` keying, `addRepaintFull()`, `WindowPaintData` mutation, `expandedGeometry()`, `isDeleted()` lifecycle → stays in `kwin-effect/` as a thin adapter.

**QML surface:** 383 animation directives across `src/settings/qml/`, `src/editor/qml/`, `src/ui/`. All use standard `Qt.easing.*` — no migration churn needed to ship the C++ runtime. Future QML-side unification (shared theme curves, `PhosphorMotion` attached type) is orthogonal work that the library *enables*.

**Config wiring:** `AnimationConfig` flows config file → `Settings` (via `PhosphorConfig`) → D-Bus adaptor (string-serialized) → QML `EasingSettings.qml`. Both C++ and QML parse the string format; duplicate parsers, documented. Phosphor-animation ships the canonical C++ parser; PlasmaZones keeps UI bindings.

**ISurfaceAnimator:** `libs/phosphor-layer/` already defines `ISurfaceAnimator` + `NoOpSurfaceAnimator`. `phosphor-animation` should ship a concrete `PhosphorAnimation::SurfaceAnimator` as a separate (optional) CMake target so `phosphor-animation` core doesn't depend on `phosphor-layer`.

**Non-Qt dependencies:** zero. Only `std::chrono`, `std::optional`, `std::function`.

**Obstacles:** none blocking. Minor footnote: `debounced_action.h` has no active consumers (parked for future Wayfire integration).

**Motion-runtime scope the *full* library should eventually cover** (beyond current code):
- Springs (mass / stiffness / damping / velocity) alongside easing.
- `IMotionClock` abstraction so QML side and compositor side share one clock model.
- `AnimatedValue<T>` primitive for QPointF / QSizeF / QRectF / QColor / QTransform.
- Retarget-mid-flight with velocity preservation (critical for snap-during-animation).
- `MotionGroup` for sequence / parallel / stagger composition.
- QML value types (`PhosphorEasing`, `PhosphorSpring`, `PhosphorMotion`) so `Behavior { }` bindings inherit user-configured curves.

**Recommended first step when work begins:** lift-and-shift the three compositor-common animation files as-is with forwarding shims. The thick-runtime additions layer on after.

---

### Compositor-plugin SDK formalization

**Status:** 4/5 feasibility. The single most strategic extraction for the compositor/WM/shell endgame.

**What already exists:**
- `ICompositorBridge` interface (`src/compositor-common/compositor_bridge.h`) — 23 methods covering lookup, identity, properties, filtering, window actions, D-Bus utilities.
- `KWinCompositorBridge` (`kwin-effect/kwin_compositor_bridge.{h,cpp}`) — thin type-erasing adapter, `void*` window handles, zero shared logic with the interface.
- `PlasmaZonesEffect` (~4,295 LOC) — owns window-state machine + frame-geometry shadow + D-Bus orchestration.
- Daemon is **entirely compositor-agnostic**: 0 `KWin::` references in `src/`. All 242 `KWin::*` references live in `kwin-effect/`.
- D-Bus protocol uses POD + versioned structs (`WindowGeometryEntry`, `TileRequestEntry`, `WindowOpenedEntry`, `EmptyZoneEntry`, `SnapAssistCandidate`). Versioning: `ApiVersion` in `registerBridge`.

**Handler portability scores (% KWin API in .cpp):**

| Handler | KWin-coupled | Portability |
|---|---|---|
| `snapassisthandler` | 0% | Perfect |
| `navigationhandler` | 0% | Perfect |
| `dragtracker` | ~3% | Excellent |
| `screenchangehandler` | ~5% | Good |
| `autotilehandler` | ~6% | Good |
| `windowanimator` | ~7% | Good (math shared via compositor-common) |

**Top 3 blockers:**

1. **Window-handle lifetime semantics** (MEDIUM). KWin uses `QPointer<EffectWindow>` + destruction signals. Wayfire uses ref-counted `view_interface_t`. `ICompositorBridge` passes bare `void*`. Each plugin must cache and validate handles.
2. **Animation attachment model differs** (LOW). KWin: `paintWindow()` + `WindowPaintData`. Wayfire: `view_2d_transformer_t` on scene node. Shared easing/timing, separate attachment.
3. **Screen ID stability across hotplug** (LOW). EDID serial can change on some monitors (Sony, older Samsung). Already mitigated in KWin with disambiguation counter; Wayfire follows the same pattern.

**Estimated Wayfire plugin size:** ~5,600 LOC (vs KWin's 7,316), simpler drag/animation APIs on Wayfire's side.

**What's in the SDK vs what stays per-compositor:**

| SDK material | Per-compositor adapter |
|---|---|
| `ICompositorBridge` interface | `KWinCompositorBridge` concrete |
| D-Bus contract (`*.xml`, POD types) | Effect lifecycle + paint pipeline |
| Easing / animation math | KWin `WindowAnimator::applyTransform` |
| Autotile state helpers (POD) | Keyboard grab + input routing |
| Floating cache (window lookup) | Per-frame paint-data mutation |
| Screen ID generation (EDID) | Event signal plumbing |
| Snap-assist candidate builder | Window-state debounce / shadow |

**See also:** `docs/wayfire-plugin-plan.md` — complementary implementation planning, validated by this audit.

---

### `phosphor-screens` — ✅ EXTRACTED

**Status:** Landed. `ScreenManager`, `VirtualScreen`, `VirtualScreenSwapper`, `PlasmaPanelSource`, `ScreenIdentity` all live in `libs/phosphor-screens/`. `ScreenModeRouter` correctly kept in daemon (snap/autotile dispatch policy, not screen management) as predicted.

**Follow-up work in PR #345:**
- `calculateAvailableGeometry` rewritten to treat the layer-shell sensor as authoritative for *reserved total* and D-Bus panel offsets as authoritative only for *which edge* — fixes a Plasma 6 bug where `floating=true` on reserving panels collapsed offsets to zero and produced zones anchored under the top panel.
- Plasma panel JS coerces numerics to int (fractional-scaling Wayland outputs were producing float coordinates the regex silently dropped). Regex accepts negative screen origins for multi-monitor arrangements.
- All `toInt()` parses on the panel wire format are now `&ok`-guarded so out-of-range values skip the panel rather than coercing to 0 and attaching to a (0,0)-origin screen.

**Settings ownership note (still applies):** Virtual-screen configs live in Settings (source of truth); `ScreenManager` caches via `refreshVirtualConfigs()`. `VirtualScreenSwapper` mutates Settings; daemon propagates via observer. The two-way sync was wired as part of extraction via `SettingsConfigStore` → `IConfigStore`.

---

### `phosphor-windows`

**Status:** Extract as new library. **Do not** absorb into `phosphor-identity`; **do not** leave entirely in daemon.

**Rationale:**
- `phosphor-identity` is a **wire-format codec** (177 LOC, INTERFACE library, Qt6::Core only). Absorbing `WindowTrackingService` (3.9k LOC state machine with LayoutManager / Settings / VirtualDesktopManager / ScreenManager / WindowRegistry deps) collapses tiers and breaks effect-linkability.
- Keeping in daemon fails because: SnapEngine calls `commitSnap()` / `resolveUnfloatGeometry()` directly (post-Phase-5C); unit tests for FIFO consumption queues and multi-instance state can't run in-daemon-only; autotile coordination needs WTS as neutral state authority.

**Scope (phosphor-windows public API):**
```
namespace PhosphorWindows {
    class WindowTracker;           // assign/unassign/query zone assignments
    class SessionRestoreManager;   // FIFO per-appId consumption queues
    class WindowStateManager;      // pre-tile geometry + floating state + pre-float memory
    class WindowSnapOrchestrator;  // SnapIntent, commitSnap, unfloat semantics
}
```

**What stays in daemon:**
- `WindowTrackingAdaptor` D-Bus facade + KConfig I/O (`PersistenceWorker`, debounced writes, dirty-mask).
- Screen shadow (cursor / active window from effect).
- SnapEngine relay signals.

**Size:** ~3,899 LOC in WTS proper + ~10 files in `windowtrackingadaptor/` subfolder.

---

## Tier 2 — Feasible with known tradeoffs

### `phosphor-overlay`

**Status:** Coherent library; coupling score 3/5; extraction is non-trivial.

**Scope:** 7 surface types owned by `OverlayService` — zone overlay, zone selector, layout OSD, navigation OSD, snap assist, layout picker, shader preview — plus the Vulkan keep-alive surface.

**Adjacent but separate:** `WindowThumbnailService` is a pure async D-Bus screenshot client (KWin `ScreenShot2`). Not a surface manager. Belongs in the same package, not the same class.

**Clean domain separation:**
- **Zero** references to `ZoneManager`, `SnapEngine`, `AutotileEngine` in `overlayservice/`.
- Receives `PhosphorZones::Layout*` / `Zone*` as read-only data.

**Obstacles:**

1. **PhosphorLayer becomes a transitive public dep.** All 7 surfaces use `PhosphorLayer::Surface` / `SurfaceFactory` / `Role` / `Anchors` / `ITransportHandle` directly. No insulation layer. Acceptable for a shell-adjacent library; document as public.
2. **Per-instance QQmlEngine.** OverlayService owns `unique_ptr<QQmlEngine>` with shared context properties. Refactor-intensive to share across instances.
3. **Vulkan keep-alive surface.** Persistent 1×1 background window prevents Qt from tearing down Vulkan globals. Must outlive all user overlays; complicates library lifecycle contract.
4. ~~**Interface-cast violation.**~~ ✅ Resolved in PR #345 (`d6f74fc7`). `ILayoutManager` et al. were deleted; `phosphor-zones` exports `IZoneLayoutRegistry` + concrete `LayoutRegistry` directly, and OverlayService now borrows the concrete type — no downcast needed.
5. ~~**CavaService hardwired.**~~ ✅ Resolved. `CavaService` extracted to `phosphor-audio` as `CavaSpectrumProvider` behind `IAudioSpectrumProvider`. Overlay can now take the interface via constructor injection.
6. ~~**ShaderRegistry singleton.**~~ ✅ Resolved in PR #345 (`dd126553`). Per-daemon `unique_ptr` threaded through `OverlayService` / `SettingsAdaptor` / `ShaderAdaptor` constructors; `detach()` wired in `Daemon::stop()` for clean teardown of the three raw-Qt-parented adaptors that borrow it.

**QML coupling:** `src/ui/*.qml` loaded via `resources.qrc`; all run in shared `QQmlEngine` with `ZoneShaderItem` from `src/daemon/rendering/`.

---

## Tier 3 — Housekeeping

### `layoutsourcefactory.{h,cpp}` → `phosphor-layout-api` — ✅ EXTRACTED

**Status:** Done. `LayoutSourceBundle.h` lives in `libs/phosphor-layout-api/include/PhosphorLayoutApi/`, and `ILayoutSourceFactory` + the factory-registry pattern moved to the same package. Daemon / editor / settings now call `buildStandardLayoutSourceBundle` once rather than open-coding the composite.

### `compositor-common/` dissolution

Covered in the cross-cutting section above. Only real blocker is `dbus_helpers.h` logging-category ownership.

---

## Tier 4 — Don't extract

### `phosphor-shell-qml`

**Verdict:** Not worth the package overhead.

- Only `PopupFrame.qml` (generic rounded container) and `ZonePreview.qml` (model-agnostic, accepts any zones array) are truly reusable.
- `LayoutCard.qml` depends on the specific layout data shape.
- `CategoryBadge.qml` hardcodes `LayoutCategory` enum values (0=Manual, 1=Autotile).
- `AspectRatioBadge.qml` hardcodes PlasmaZones aspect class strings.
- `ZoneShaderRenderer.qml` wraps `ZoneShaderItem` — inseparable from daemon's shader rendering pipeline (`phosphor-rendering::ShaderEffect` subclass, audio spectrum sampling, wallpaper texture, multi-pass buffer rendering).

A two-component library is not worth its own QML module URI. Keep `org.plasmazones.common` as-is.

### `ScreenModeRouter`

Not screen management — it's snap/autotile dispatch policy. Stays in daemon.

### `LayoutManager` / `LayoutFactory`

> **⚠️ Verdict reversed in PR #345 (commits `44169c36`, `fd2a658f`, `d6f74fc7`).**
>
> `LayoutManager` was moved into `phosphor-zones` and renamed to `LayoutRegistry`. The "five narrow interfaces" concern was resolved by deleting four of them (`ILayoutAssignments`, `IQuickLayouts`, `IBuiltInLayouts`, `ILayoutPersistence`, `ILayoutManager`) and keeping only `IZoneLayoutRegistry` — mirrors `phosphor-tiles`' `ITileAlgorithmRegistry` + `AlgorithmRegistry` shape. PZ-specific config-schema knowledge (previously reached via `ConfigDefaults::*`) is gone: the lib now takes a `unique_ptr<PhosphorConfig::IBackend>` + XDG layout-subdirectory string in its constructor, and exposes `setDefaultLayoutIdProvider(std::function<QString()>)` for the default-layout lookup callback. Wire-format constants (`"Assignment:"`, `"QuickLayouts"`) are lib-private — they ARE the serialization format, inherited for free by any future plugin compositor that links the lib. Third-party consumers construct it with whatever schema strings + backend they own.
>
> `LayoutFactory` stays in `src/core/` — still daemon-internal.

---

#### Original Tier 4 rationale (now historical):

`LayoutManager` implements five narrow phosphor-zones interfaces + daemon-specific signals + config backend + screen cycling + autotile override storage. Extraction is a net LOC increase. The existing `phosphor-layout-api` / `phosphor-zones` / `phosphor-tiles` split is already clean.

`LayoutFactory` is 72 lines — extract only if editor/settings independently consume it.

---

## Naming pattern (for future candidates)

Every clean library is named after its **domain**: `phosphor-config`, `phosphor-identity`, `phosphor-layer`, `phosphor-layout-api`, `phosphor-rendering`, `phosphor-shell`, `phosphor-shortcuts`, `phosphor-tiles`, `phosphor-zones`.

`compositor-common` is named after a **relationship** ("code shared between daemon and effect"). If a candidate can't be named after its domain, it probably isn't a library — it's a shim zone waiting to be decomposed.

Applied to new candidates: `phosphor-animation`, `phosphor-audio`, `phosphor-overlay`, `phosphor-screens`, `phosphor-windows` — all domain names. ✓

---

## Recommended sequence (when work begins)

1. ~~**`phosphor-animation`**~~ ✅ Done (PR #345 finished the singleton-kill; the motion-runtime thick-scope work — `IMotionClock`, `AnimatedValue<T>`, QML value types — is still future work).
2. ~~**`phosphor-screens`**~~ ✅ Done (extracted; availability calculation hardened in PR #345).
3. **Compositor-plugin SDK formalization** — the strategic endgame enabler. Now unblocked: every lib-level singleton that would have blocked per-plugin ownership is gone.
4. **`phosphor-windows`** — largest surface, most coupling; last.

**Parallel cleanup** (can run independently):
- Resolve `dbus_helpers.h` logging ownership → unblocks the D-Bus triple extraction.
- Decide `debounced_action.h` (drop or park).
- ~~Move `layoutsourcefactory.{h,cpp}` to `phosphor-layout-api`.~~ ✅ Done (`LayoutSourceBundle` + `ILayoutSourceFactory` live in `phosphor-layout-api`).
- ~~Fix `overlayservice/settings.cpp:122` `LayoutManager*` cast by extending `ILayoutManager`.~~ ✅ Resolved in PR #345 (`ILayoutManager` deleted; OverlayService uses concrete `PhosphorZones::LayoutRegistry` directly).

**Overlay** sits outside this sequence. Its extraction depends on whether PhosphorLayer-as-public-API is acceptable for a `phosphor-overlay` library. Three of its six obstacles (interface-cast violation, ShaderRegistry singleton, CavaService hardwired) are now resolved — remaining three still apply.

---

## Investigation provenance

This survey is synthesized from 8 parallel codebase investigations run 2026-04-18, plus post-hoc extraction work:

1. `phosphor-animation` inventory (file/LOC inventory, KWin-coupling split, QML surface count, config flow, ISurfaceAnimator integration).
2. `compositor-common` file-by-file audit (per-file verdicts, logging / D-Bus / metatype couplings).
3. Compositor-plugin SDK analysis (handler KWin-coupling %, D-Bus protocol stability, Wayfire feasibility, lifetime-semantics blockers).
4. `phosphor-overlay` extraction analysis (7 surface types, PhosphorLayer coupling, domain coupling score, obstacles).
5. `phosphor-windows` consolidation (three-way evaluation: absorb / split / stay).
6. `phosphor-screens` extraction (scope, ScreenModeRouter carve-out, KF6-freeness, virtual-screen ownership).
7. Shared QML components reusability audit.
8. Layout-library consolidation audit (overlap matrix across phosphor-layout-api / phosphor-tiles / phosphor-zones).
9. `phosphor-audio` extraction (2026-04-21): `CavaService` → `IAudioSpectrumProvider` + `CavaSpectrumProvider`. Motivated by shell-ecosystem reuse — audio spectrum is a system service, not an app feature.
