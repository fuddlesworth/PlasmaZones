# Library Extraction Survey

**Date:** 2026-04-18
**Scope:** Candidate inventory for further `phosphor-*` library extractions, in service of the long-term compositor / WM / shell goal.
**Status:** Strategic survey — not a migration plan. Each candidate is backed by a focused investigation (see "Investigation provenance" at the end).

---

## Context

Significant decoupling work has already landed: `phosphor-config`, `phosphor-identity`, `phosphor-layer`, `phosphor-layout-api`, `phosphor-rendering`, `phosphor-shell`, `phosphor-shortcuts`, `phosphor-tiles`, `phosphor-zones`. This document maps what remains in `src/` (and `kwin-effect/`) to potential additional libraries, ranks them by value × feasibility, and calls out what should **not** be extracted.

The core strategic insight: the daemon has **zero** `KWin::` references in `src/`. Compositor-portability is an *extraction problem*, not an architecture problem.

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
| `window_id.h` | ✅ Confirmed | `phosphor-identity` | Already shimmed |
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

### `phosphor-animation` (motion-runtime)

**Status:** Ready. Not just an easing-math library — a full motion-runtime (easing + springs + clocks + animated values + window motion + stagger orchestration + QML integration as the ambition).

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

### `phosphor-screens`

**Status:** 4/5 feasibility. ~2.0k LOC after the ScreenModeRouter carve-out.

**Scope:**
- In: `src/core/screenmanager.{h,cpp}` + `screenmanager/{panels,virtualscreens}.cpp` (963 LOC), `screen_resolver.{h,cpp}` (111 LOC), `virtualscreen.h` (330 LOC), `virtualscreenswapper.{h,cpp}` (296 LOC), `src/dbus/screenadaptor.{h,cpp}` (655 LOC).
- **Out: `screenmoderouter.{h,cpp}`** — this is snap/autotile dispatch policy (routes nav actions to `SnapEngine` vs `AutotileEngine`). Not screen management. Keep in daemon.

**Domain independence:**
- Zero KF6 headers. Qt-only + PhosphorShell layer-shell sensor windows.
- Zero snap/autotile imports in `ScreenManager`. Snap/autotile are unidirectional consumers.
- Virtual-screen model is self-contained: `VirtualScreenDef` + `VirtualScreenConfig` with validation, swap, rotate, JSON-round-trip tolerance.

**Intentional couplings (document, don't remove):**
- KDE Plasma D-Bus (`org.kde.plasmashell`) for panel geometry — necessary for Plasma compatibility; pure Qt fallback works on other desktops.
- PhosphorShell layer-shell sensors for available-geometry detection — Wayland-specific optimization.

**Settings ownership note:** Virtual-screen configs live in Settings (source of truth); `ScreenManager` caches via `refreshVirtualConfigs()`. `VirtualScreenSwapper` mutates Settings; daemon propagates via observer. Two-way sync must be wired as part of extraction.

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
4. **Interface-cast violation.** `settings.cpp:101-122` casts `ILayoutManager*` → `LayoutManager*` to access signals (interface lacks them). Fix by extending interface with signal declarations.
5. **CavaService hardwired.** Audio visualization initialized in constructor; no feature-gate. Extract to optional plugin via constructor injection.
6. **ShaderRegistry singleton.** Referenced as global in multiple TUs. Narrow via DI for testability.

**QML coupling:** `src/ui/*.qml` loaded via `resources.qrc`; all run in shared `QQmlEngine` with `ZoneShaderItem` from `src/daemon/rendering/`.

---

## Tier 3 — Housekeeping

### `layoutsourcefactory.{h,cpp}` → `phosphor-layout-api`

**Status:** Small (65 lines + `LayoutSourceBundle` struct), but high-reuse-value.

**Rationale:** Assembles the standard composite (`ZonesLayoutSource + AutotileLayoutSource → CompositeLayoutSource`) with a lifetime-order-safe destruction struct. Daemon, settings, and editor all wire this identically — moving it to the API layer removes three copies of the same footgun-avoidance pattern.

**Scope increment to `phosphor-layout-api`:**
```cpp
namespace PhosphorLayoutApi {
    struct LayoutSourceBundle { /* hand-written move-assign, reverse teardown */ };
    LayoutSourceBundle makeLayoutSourceBundle(ILayoutRegistry* registry);
}
```

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

`LayoutManager` implements five narrow phosphor-zones interfaces + daemon-specific signals + config backend + screen cycling + autotile override storage. Extraction is a net LOC increase. The existing `phosphor-layout-api` / `phosphor-zones` / `phosphor-tiles` split is already clean.

`LayoutFactory` is 72 lines — extract only if editor/settings independently consume it.

---

## Naming pattern (for future candidates)

Every clean library is named after its **domain**: `phosphor-config`, `phosphor-identity`, `phosphor-layer`, `phosphor-layout-api`, `phosphor-rendering`, `phosphor-shell`, `phosphor-shortcuts`, `phosphor-tiles`, `phosphor-zones`.

`compositor-common` is named after a **relationship** ("code shared between daemon and effect"). If a candidate can't be named after its domain, it probably isn't a library — it's a shim zone waiting to be decomposed.

Applied to new candidates: `phosphor-animation`, `phosphor-overlay`, `phosphor-screens`, `phosphor-windows` — all domain names. ✓

---

## Recommended sequence (when work begins)

1. **`phosphor-animation`** — lowest risk, highest near-term polish payoff, unblocks QML theme-binding work later.
2. **`phosphor-screens`** — clean, well-scoped, foundational for any compositor integration.
3. **Compositor-plugin SDK formalization** — the strategic endgame enabler.
4. **`phosphor-windows`** — largest surface, most coupling; last.

**Parallel cleanup** (can run independently):
- Resolve `dbus_helpers.h` logging ownership → unblocks the D-Bus triple extraction.
- Decide `debounced_action.h` (drop or park).
- Move `layoutsourcefactory.{h,cpp}` to `phosphor-layout-api`.
- Fix `overlayservice/settings.cpp:122` `LayoutManager*` cast by extending `ILayoutManager`.

**Overlay** sits outside this sequence. Its extraction depends on whether PhosphorLayer-as-public-API is acceptable for a `phosphor-overlay` library.

---

## Investigation provenance

This survey is synthesized from 8 parallel codebase investigations run 2026-04-18:

1. `phosphor-animation` inventory (file/LOC inventory, KWin-coupling split, QML surface count, config flow, ISurfaceAnimator integration).
2. `compositor-common` file-by-file audit (per-file verdicts, logging / D-Bus / metatype couplings).
3. Compositor-plugin SDK analysis (handler KWin-coupling %, D-Bus protocol stability, Wayfire feasibility, lifetime-semantics blockers).
4. `phosphor-overlay` extraction analysis (7 surface types, PhosphorLayer coupling, domain coupling score, obstacles).
5. `phosphor-windows` consolidation (three-way evaluation: absorb / split / stay).
6. `phosphor-screens` extraction (scope, ScreenModeRouter carve-out, KF6-freeness, virtual-screen ownership).
7. Shared QML components reusability audit.
8. Layout-library consolidation audit (overlap matrix across phosphor-layout-api / phosphor-tiles / phosphor-zones).
