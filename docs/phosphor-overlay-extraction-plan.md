<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-overlay extraction plan

**Status:** Phases 0a-5 shipped in PR #436; Phase 6 deferred. See the
status table at the bottom for the per-phase breakdown.

## Why

`src/daemon/overlayservice/` is the largest single subsystem in
PlasmaZones (around 5,900 lines across 14 TUs, 84 `OverlayService::`
methods) and almost all of it is domain-agnostic mechanism:

- per-screen layer-shell shell hosts that group multiple slot-Items
  onto a single `wl_surface`,
- show/hide animator coordination for those slots,
- per-role animator config registration and longest-prefix lookup,
- screen hot-plug, identifier drift, and rekey plumbing,
- shader warming / preview surfaces,
- D-Bus driven slot lifecycle (visible / hidden / dismiss).

The PZ-specific bits (zones, snap-assist, layout-picker, OSDs) are
*content* riding on this mechanism. Lifting the mechanism into a
`phosphor-overlay` library:

1. Lets any Phosphor shell (PZ today, Phosphor-as-standalone tomorrow,
   third-party plugins per the plugin-based-compositor direction) build
   overlays without re-implementing the shell-host / slot / animator
   coordination.
2. Shrinks `OverlayService` to the PZ-content wiring it actually owns.
3. Sets up the symmetric provider-library pattern the project memory
   notes call for (`I<Domain>Registry` + concrete, DI by default,
   no singletons).

## Library framing (from project memory)

- **`phosphor-overlay` is a helper library for building overlays with
  PhosphorLayer**, not an insulation layer over it
  (`project_phosphor_overlay_scope.md`). `PhosphorLayer::*` and
  `PhosphorSurfaces::SurfaceManager` are accepted public dependencies.
- The library API must be stable enough for third-party plugins
  (`project_plugin_based_compositor.md`). Constructor-injected
  instances over process-global singletons.

## Scope: what's in vs what's out

| Concept | Owner | Lives in |
|---|---|---|
| Per-screen layer-shell shell host (one wl_surface, many slots) | overlay | `phosphor-overlay` |
| Named slot (a `QQuickItem` inside the shell, animator-driven show/hide) | overlay | `phosphor-overlay` |
| Per-slot animator config registration (longest-prefix scope lookup) | overlay | `phosphor-overlay` |
| Slot show/hide controller + completion callbacks | overlay | `phosphor-overlay` |
| Screen hot-plug, identifier-drift, rekey plumbing for overlays | overlay | `phosphor-overlay` |
| Vulkan keep-alive surface | already in `phosphor-surfaces` | unchanged |
| Layer-shell role / anchors / keyboard | `phosphor-layer` | unchanged |
| UI patterns (Hud, Modal, etc.) | `phosphor-shell-patterns` | unchanged |
| `PzRoles::*` (axis-3 app roles) | PZ daemon | unchanged |
| Zone overlay content (zone rects, drag preview) | PZ daemon | unchanged |
| Snap-assist content (window picker) | PZ daemon | unchanged |
| Layout-picker content | PZ daemon | unchanged |
| OSD content (LayoutOsd / NavigationOsd) | PZ daemon | unchanged |
| Shader preview content | PZ daemon | unchanged |
| `IAudioSpectrumProvider`, zone domain, shader registry | PZ daemon | unchanged |

The library knows about *surfaces and slots*. It does not know about
zones, layouts, or audio spectrums. PZ keeps the content vocabulary.

## Original API sketch (NOT shipped: see Phase 2 for the final API)

This block is the pre-implementation sketch from the plan's early
draft. The actual shipped surface is documented in the Phase 2 status
entry below; it uses `ensureShell` / `destroyShell` / `syncSurfaceState`
/ `rekey` / `hideSlot` / `registerConfigForRole` with a plain
`SlotEntry { item, role }` struct (no `Slot` class, no `SlotRegistry`,
no `ContentDescriptor`). The sketch is preserved here as a record of
the design exploration that motivated the simpler shipped API.

```cpp
namespace PhosphorOverlay {

// One per screen. Owns the layer-shell wl_surface and the QQuickItem
// tree that hosts every per-content slot for this screen.
class ShellHost {
public:
    ShellHost(PhosphorSurfaces::SurfaceManager& sm,
              PhosphorScreens::ScreenManager& screens,
              QQuickWindow* /* injected if pre-existing */ = nullptr);
    ~ShellHost();

    // The wl_surface scope prefix governs this host's identity. By
    // default it derives from PhosphorShellPatterns::Hud, but consumers
    // can pass a custom Role via the config struct.
    void start(const QString& screenId, const Config& cfg);
    void stop(const QString& screenId);

    // Per-content slot registration. The slot QQuickItem must be a
    // child of the shell's root and tagged via QML object name; the
    // consumer registers it here so phosphor-overlay can drive show /
    // hide / animator config against a known handle.
    Slot* registerSlot(const QString& screenId, SlotKey key, QQuickItem* item);
    Slot* slot(const QString& screenId, SlotKey key) const;

    QQuickWindow* window(const QString& screenId) const;
    PhosphorLayer::Surface* surface(const QString& screenId) const;

Q_SIGNALS:
    void slotShown(const QString& screenId, SlotKey key);
    void slotHidden(const QString& screenId, SlotKey key);
    void shellLost(const QString& screenId, const QString& reason);
};

// A handle to one slot inside one shell. Drives show/hide via the
// injected animator using the role-override path so the slot's animator
// config (registered against PzRoles::Osd, ::SnapAssist, etc.) is
// resolved by scope-prefix match.
class Slot {
public:
    void show(std::function<void()> onShown = {});
    void hide(std::function<void()> onHidden = {});

    bool isLogicallyShown() const;
    QQuickItem* item() const;
    PhosphorLayer::Role role() const;
};

// Registry of (SlotKey → ContentDescriptor) the consumer fills before
// start(). ContentDescriptor carries the role override, the optional
// QML property writer, and the animator config builder. Replaces the
// god-method per-slot show/hide pattern in OverlayService.
class SlotRegistry {
public:
    void registerContent(SlotKey key, ContentDescriptor desc);
};

// Consumer-side hook: phosphor-overlay calls this when it needs to
// build the animator config for a slot. The consumer constructs a
// SurfaceAnimator::Config from its own settings + the supplied
// ShaderProfileTree (or other config source).
struct ContentDescriptor {
    PhosphorLayer::Role role;                       // axis-3
    std::function<PhosphorAnimationLayer::SurfaceAnimator::Config(
        const PhosphorAnimation::ShaderProfileTree&)> animatorConfigBuilder;
    QString qmlObjectName;                          // resolved at start()
};

} // namespace PhosphorOverlay
```

(Sketch note: `SlotKey` was proposed as a strong-typed `quint32`
newtype. As shipped, the lib uses plain `QString` keys: consumers
define their own slot vocabulary via function-local-static accessors,
e.g. PZ's `PzSlotKeys::Osd()` / `SnapAssist()` / etc. in
`src/daemon/overlayservice/pz_slot_keys.h`. Third-party plugins mint
their own keys the same way without touching the library.)

## Dependencies (declared public)

- `PhosphorLayer::PhosphorLayer` (Role, Surface, Factory)
- `PhosphorSurfaces::PhosphorSurfaces` (SurfaceManager, scope generation, keep-alive)
- `PhosphorShellPatterns::PhosphorShellPatterns` (Hud / Modal / Floating recipes)
- `PhosphorAnimation::PhosphorAnimation` + `PhosphorAnimationLayer` (SurfaceAnimator, profile tree)
- `PhosphorScreens::Manager` (screen topology, virtual screens)
- Qt 6.6 Core / Gui / Quick / Qml

## Blockers that must clear first

Originally captured by the project-memory note
(`project_phosphor_overlay_scope.md`), with corrections from reading
current code:

1. **`PhosphorZones::IZoneLayoutRegistry` is missing the layout
   signals and most of the query surface OverlayService needs.** The
   memory note framed this as "a `dynamic_cast` from `ILayoutRegistry*`
   to concrete." The actual code is worse: `OverlayService::m_layoutManager`
   is already typed `LayoutRegistry*` (concrete) and the cast was a
   no-op self-cast. So the architectural problem is that the daemon's
   biggest single subsystem hard-binds to the concrete `LayoutRegistry`
   for both signals and query methods, while the project memory's
   plugin-architecture direction wants every consumer talking to an
   interface.

   **Fix is two-phase** (see Phase 0a + 0b below).

2. **Hardwired `IAudioSpectrumProvider` flow** (`overlayservice/settings.cpp:301-312`,
   `shader.cpp:182-183`). Already an interface, but its lifecycle is
   driven from inside `OverlayService`. A generic overlay library
   shouldn't ship the audio-spectrum config knobs. **As shipped:** audio
   spectrum stays consumer-side via the daemon's per-role
   `buildOsdConfig` / `buildZoneSelectorConfig` / etc. helpers in
   `animation_config.cpp`. The lib never sees `m_audioProvider`.

3. **`ShaderRegistry` singleton** (per memory note). Already resolved:
   `daemon.h:520-525` owns a per-daemon `ShaderRegistry` and injects it.

4. **Per-instance `QQmlEngine`**. Confirm `PhosphorSurfaces::SurfaceManager`'s
   shared-engine path covers every overlay surface phosphor-overlay
   creates. The PassiveShell already shares; verify SnapAssist / Picker /
   ShaderPreview do too.

5. **Vulkan keep-alive surface**. Already in phosphor-surfaces;
   phosphor-overlay does NOT recreate this.

## Migration phases

Each phase is its own PR. Build clean + tests green at every commit.

### Phase 0a - Plan + signal lift (DONE in this branch)

- Land this doc.
- Lift the four catalog / active-layout signals (`layoutAdded`,
  `layoutRemoved`, `activeLayoutChanged`, `layoutAssigned`) onto
  `PhosphorZones::IZoneLayoutRegistry` so consumers can target the
  interface for the lifecycle subset.
- Move the `Q_PROPERTY(Layout* activeLayout READ activeLayout NOTIFY
  activeLayoutChanged)` to the interface alongside its NOTIFY signal
  (moc 6.11's same-class NOTIFY-resolution rule needs both in the
  same class scope).
- Delete the dead `dynamic_cast<LayoutRegistry*>(m_layoutManager)`
  self-cast pair in `OverlayService::setLayoutManager`. Connect via
  `IZoneLayoutRegistry::*` signal pointers directly.
- Verify blocker #3 (ShaderRegistry DI) is already resolved
  (confirmed: `daemon.h:520-525`).

### Phase 0b - Interface widening for layout queries (DONE in this branch)

`OverlayService` consumes these `LayoutRegistry`-concrete methods today:

- `layoutForScreen(screenId, vd, activity)` - cascade-resolved layout
- `assignmentIdForScreen(screenId, vd, activity)` - cascade-resolved assignment id
- `defaultLayout()` - fallback layout
- `layoutById(uuid)` - layout lookup by UUID
- `resolveLayoutForScreen(screenId)` - convenience over `layoutForScreen` + current VD/activity
- `currentActivity()` - currently-active KActivities id
- `currentVirtualDesktop()` - currently-active virtual desktop id

These are all concrete-only on `LayoutRegistry` today. Port them up to
`IZoneLayoutRegistry`:

1. Add the seven virtual methods to the interface. The first five
   belong cleanly there (layout-catalog queries). The last two
   (`currentActivity` / `currentVirtualDesktop`) are session state
   that `LayoutRegistry` happens to hold; flag whether they should live
   on a separate `ISessionContext` interface or stay on the layout
   registry. As shipped, they live on the layout registry interface to
   keep the change scope-bounded.
2. Update `LayoutRegistry` to declare each method `override`.
3. Update `OverlayService::m_layoutManager` to `IZoneLayoutRegistry*`
   and `setLayoutManager(IZoneLayoutRegistry*)`. The daemon caller
   (`daemon.cpp:694`) keeps passing `m_layoutManager.get()` (concrete
   `LayoutRegistry*` upcasts implicitly to the interface).
4. Re-run the audit pattern from #436 to check no other call sites are
   accidentally relying on concrete-only `LayoutRegistry` methods via
   the OverlayService bridge.

The audit picked up one transitive widening: the
`PhosphorZones::LayoutUtils::buildUnifiedLayoutList` helper (consumed by
`OverlayService::buildLayoutsList` / `visibleLayoutCount`) took a
concrete `LayoutRegistry*` even though it only called interface methods
(`layouts()`, `activeLayout()`). Widened to `IZoneLayoutRegistry*` so
`OverlayService` can pass its (now-interface) pointer through. Other
daemon callers (`zoneselectorcontroller`, `daemon`, `layoutadaptor`,
`unifiedlayoutcontroller`) pass concrete `LayoutRegistry*` and upcast
implicitly - no churn at call sites.

Result: `OverlayService` only sees `IZoneLayoutRegistry`. The PZ
daemon is now interface-driven for its biggest subsystem, matching the
plugin-architecture direction. The bridge from `OverlayService` into
the future `phosphor-overlay` library is symmetrical: both sides talk
to interfaces.

### Phase 1 - New library scaffolding (DONE in this branch)

- `libs/phosphor-overlay/`: CMakeLists, Config.cmake.in, README, empty
  public headers, `add_library SHARED` linking layer + surfaces +
  shell-patterns + animation + screens.
- Top-level `CMakeLists.txt` add_subdirectory after `phosphor-surfaces`
  (and after `phosphor-animation` since SurfaceAnimator is a dep).
- `tests/test_overlay_smoke.cpp` that links the lib and exercises an
  empty ShellHost ctor/dtor. Pins the build.

Shipped a minimal `PhosphorOverlay::ShellHost` (QObject, default
ctor/dtor) and a smoke test that pins both the construct/destruct path
and the QObject parent semantics. The Phase 2 mechanism move fills in
the real ctor signature (`SurfaceManager&`, `ScreenManager&`, etc.)
without churn at the library boundary - consumers Phase 2 lands on
re-call against the same `PhosphorOverlay::` namespace.

### Phase 2 - ShellHost extraction (DONE in this branch)

Per-screen passive-shell mechanism extracted into `phosphor-overlay/`.
Shipped as eight sub-commits on the same branch (each builds clean,
tests green). Status of the five method moves the original plan called
for:

| Method | Status |
|---|---|
| `ensurePassiveShellFor` | DONE (2.4) - delegates to `ShellHost::ensureShell` |
| `destroyPassiveShell` | DONE (2.4) - delegates to `ShellHost::destroyShell` |
| `syncPassiveShellSurfaceState` | DONE (2.5) - delegates to `ShellHost::syncSurfaceState` with PZ-computed visibility booleans |
| `rekeyOverlayState` | DONE (2.7) - delegates to `ShellHost::rekey` with PZ guards + post-rekey re-anchor |
| `validateScreenStateInvariant` | STAYS in daemon - debug check on PZ-content "main overlay live" sentinel (`overlayPhysScreen != nullptr`); iteration domain is the daemon's `m_screenStates`, not the lib's shell state map, so moving it would change semantics without adding value |

Sub-commits landed on this branch:

- **2.1**: `ShellState` struct + real `ShellHost` state container
  (stateFor / removeState / screenIds / failure flag accessors). Smoke
  test grew from 2 to 7 cases.
- **2.2**: Sticky creation-failure spam-guard moves from
  `OverlayService::m_notificationCreationFailed` to ShellHost.
- **2.3**: Shell mechanism fields (`shellSurface`, `shellWindow`,
  `physScreen`) migrate from `PerScreenOverlayState` into the embedded
  `ShellState shell` member. ~90 access sites across 7 TUs renamed via
  sed.
- **2.4**: `ensurePassiveShellFor` and `destroyPassiveShell` shrink to
  shims that delegate to `ShellHost::ensureShell` /
  `ShellHost::destroyShell`. The PZ-specific bits (PassiveShell role +
  qmlSource + warmed-surface pipeline, 5 slot QML object-name lookups,
  6 QML signal wires, RHI prime, PZ-content sentinel teardown) move
  into `wirePassiveShellSlots` / `unwirePassiveShellSlots` helpers and
  register as callbacks on the ShellHost in the OverlayService ctor.
- **2.5**: `syncPassiveShellSurfaceState` delegates to
  `ShellHost::syncSurfaceState(screenId, anyVisible, anyInputGrabbing)`.
  PZ computes the two booleans from its parallel slot pointers; the
  lib owns the surface-mapped + input-region toggle mechanism.
- **2.6**: Latent-bug fix from 2.4 design. `PerScreenOverlayState::shell`
  was an embedded `ShellState` value, but `ensureShell` populated only
  the lib's parallel map: the embedded field stayed default-initialized.
  Switched to single source of truth: lib stores
  `QHash<QString, ShellState*>` (raw owning pointers, stable across
  rehashes); PZ state carries a borrowed pointer wired by the shim.
  `.shell.X` -> `.shell->X` mass-renamed across the daemon.
- **2.7**: `rekeyOverlayState` delegates to `ShellHost::rekey(oldKey, newKey)`,
  which does the lib-side map-key swap without touching the heap
  ShellState (borrowed pointer survives). PZ shim keeps the flavor-flip
  guard, clobber check, daemon-state move, geom-watcher reinstall, and
  re-anchor-the-surface step. Smoke test grew by 3 rekey edge-case
  pins.

The QML side (`PassiveOverlayShell.qml`) stays where it is in the
daemon; the library accepts the qmlSource via the
consumer-registered `SurfaceFactory` callback.

Library API surface at end of Phase 2:

```cpp
class ShellHost : public QObject {
public:
    using SurfaceFactory      = std::function<PhosphorLayer::Surface*(const QString&, QScreen*)>;
    using PostCreateCallback  = std::function<void(const QString&, ShellState&)>;
    using PreDestroyCallback  = std::function<void(const QString&)>;

    void setSurfaceFactory(SurfaceFactory);
    void setPostCreateCallback(PostCreateCallback);
    void setPreDestroyCallback(PreDestroyCallback);

    ShellState* ensureShell(const QString& screenId, QScreen* physScreen);
    void destroyShell(const QString& screenId);
    void syncSurfaceState(const QString& screenId, bool anyVisible, bool anyInputGrabbing);
    bool rekey(const QString& oldKey, const QString& newKey);

    ShellState& stateFor(const QString&);
    const ShellState* stateFor(const QString&) const;
    bool hasState(const QString&) const;
    void removeState(const QString&);
    QStringList screenIds() const;

    void markFailure(const QString&);
    void clearFailure(const QString&);
    bool hasFailure(const QString&) const;
    QStringList failureScreenIds() const;
};
```

### Phase 3 - Slot extraction (DONE in this branch)

Sub-commits:

- **3.1**: Slot pointer storage migrates from 5 named fields on
  `PerScreenOverlayState` (`passiveShellOsdSlot`, …) into the lib's
  `ShellState::slots` map keyed by daemon-defined slot key strings
  (`pz_slot_keys.h` with `PzSlotKeys::Osd()` / `SnapAssist()` / etc.
  function-local-static accessors). `PerScreenOverlayState` exposes
  the same 5 names as inline accessor methods (`osdSlot()` etc.) that
  resolve through the borrowed `ShellState*`. ~95 read sites mass-renamed.
- **3.2**: `SlotEntry` struct ({item, role}) added to the lib;
  `ShellState::slots` becomes `QHash<QString, SlotEntry>`. Roles are
  pinned at slot-wire time so the lib can drive the animator. ShellHost
  gains `setSurfaceAnimator(SurfaceAnimator*)` and
  `hideSlot(screenId, slotKey, completion)`. PZ wires the 5 slots' roles
  in `wirePassiveShellSlots`. `hideZoneSelectorSlotOnScreen` shrinks
  to a thin shim.
- **3.3**: Every direct `m_surfaceAnimator->beginHide` call in the
  overlay subsystem (6 total) routes through `ShellHost::hideSlot`.
  The lib is now the SurfaceAnimator client for slot hides; the daemon
  retains content-side concerns (resetCursorState QML invocations,
  completion callbacks).

The "show side" of slot lifecycle (`beginShow` calls in
`showZoneSelector` / `showSnapAssist` / etc.) stays in the daemon -
each show path intermixes PZ content writes (mode toggles, loader
re-instantiation, content-property pushes) with the
animator-trigger boilerplate. The show side did not migrate in this
PR: its content/mechanism interleaving makes a clean lift
significantly larger than the hide-side conversion landed in Phase 3.

### Phase 4 - Animator config wiring (DONE in this branch)

The library is now the sole SurfaceAnimator client for the overlay
subsystem. The daemon's `applyShaderProfilesToAnimator` routes its 4
`registerConfigForRole` calls through `ShellHost::registerConfigForRole`,
and the per-instance scope-prefix construction policy
(`makePerInstanceRole`) moves to a `PhosphorOverlay::` free function so
every consumer gets the same longest-prefix-lookup guarantee.

The `build*Config` helpers (`buildOsdConfig`, `buildLayoutPickerConfig`,
`buildZoneSelectorConfig`, `buildSnapAssistConfig`) stay in
`animation_config.cpp`: they own the SHAPE of each config (curve
names, durations, shader paths) which is PZ-content. The plan-doc
phrased this as "per-content ContentDescriptor builders inside the
daemon"; in practice the existing build*Config functions are already
the right shape, no rename needed.

Library additions:

```cpp
namespace PhosphorOverlay {

[[nodiscard]] PhosphorLayer::Role makePerInstanceRole(const PhosphorLayer::Role& base,
                                                     QStringView screenId, quint64 generation);

class ShellHost : public QObject {
public:
    void registerConfigForRole(const PhosphorLayer::Role& role,
                               PhosphorAnimationLayer::SurfaceAnimator::Config config);
    // ...
};

} // namespace PhosphorOverlay
```

`PzRoles::makePerInstanceRole` shrinks to a delegating wrapper so every
existing call site continues to compile under the PzRoles:: namespace.

### Phase 5 - OverlayService shrink + cleanup (DONE in this branch)

Three sub-commits:

- **5.1**: Delete the three redundant cleanup helpers
  (`releaseSurfacesInState`, `cleanupAllScreenStates`,
  `cleanupVirtualScreenStates`) that duplicated `ShellHost::destroyShell`
  semantics. The two callers (dtor + physical-screen-removed handler)
  switch to two-pass (collect keys → destroy + erase) loops calling
  `m_shellHost->destroyShell` per screen. Net: `overlayservice.cpp`
  drops 28 lines.

- **5.2**: Split `overlayservice.cpp` into three:
  - `overlayservice.cpp` (kept the ctor + dtor + small handlers): now
    795 lines, under the 800-line cap.
  - `overlayservice/priming.cpp` (new, 182 lines):
    `primeSurfaceRenderPipeline` + `cancelSurfacePrime`: the
    RHI/Vulkan pipeline-warmup helpers.
  - `overlayservice/screens.cpp` (new, 220 lines): screen-lifecycle
    methods (`setupForScreen`, `removeScreen`, `assertWindowOnScreen`,
    `handleScreenAdded`, `destroyAllWindowsForPhysicalScreen`,
    `handleScreenRemoved`).

- **5.3**: Extract the 8 daemon-to-ShellHost bridge methods from
  `osd.cpp` (where they accumulated during Phase 2-4) into a new
  `overlayservice/shellhost_bridge.cpp` (274 lines). `osd.cpp` drops
  from 889 to 691 lines, under the cap. OSD-specific show / dismiss
  paths stay in `osd.cpp`.

Final TU sizes after Phase 5 (and pass-4/5 audit fixes):

| File | Lines |
|---|---|
| `overlayservice.cpp` | 795 |
| `overlayservice/osd.cpp` | 691 |
| `overlayservice/overlay.cpp` | 893 |
| `overlayservice/selector.cpp` | 748 |
| `overlayservice/snapassist.cpp` | 578 |
| `overlayservice/shader.cpp` | 557 |
| `overlayservice/overlay_data.cpp` | 379 |
| `overlayservice/animation_config.cpp` | 330 |
| `overlayservice/settings.cpp` | 326 |
| `overlayservice/lifecycle.cpp` | 313 |
| `overlayservice/shellhost_bridge.cpp` | 274 |
| `overlayservice/screens.cpp` | 220 |
| `overlayservice/priming.cpp` | 182 |
| `overlayservice/selector_update.cpp` | 231 |
| `overlayservice.h` | 1046 |

`overlay.cpp` at 893 sits 93 lines over the 800-line cap: further
fragmentation (e.g. lifting `rekeyOverlayState` and
`validateScreenStateInvariant` into a separate TU) would hurt
readability for a marginal win. The header at 1046 is dominated by
the still-large `OverlayService` class declaration; meaningful
header shrinkage requires breaking up the class itself, which is
beyond Phase 5's scope.

The plan-doc's other Phase 5 goals do NOT apply in practice:
  - "Files like `overlayservice/lifecycle.cpp` move into the library or
    get deleted." `lifecycle.cpp` is 311 lines of PZ-content
    initialization (show / hide / setupZoneOverlay): it's content,
    not mechanism, and stays in the daemon.
  - "Per-screen state moves to the library's ShellHost." Shell
    mechanism state already lives in `ShellState` (Phase 2.3-2.6);
    PZ-content state (`overlayPhysScreen`, `overlayGeometry`,
    `labelsTextureHash`, `zoneSelectorPhysScreen`,
    `zoneSelectorGeometry`) stays on the daemon's
    `PerScreenOverlayState` because it's PZ semantics, not lib
    semantics.
  `OverlayService` TU split should drop from 14 files to 4 or 5.

### Phase 6 - Optional: standalone-compositor seam

- Once phosphor-overlay is consumed by PZ, add a small example app at
  `libs/phosphor-overlay/examples/hello-overlay.cpp` that builds a
  ShellHost with one slot, shows it, hides it. Pins the library's
  external API surface.
- This is also the seam Phosphor-as-standalone would consume.

## Renames + new vocabulary (original sketch: actual mapping below)

The first column shows the pre-extraction PZ names. The second column
was the sketched target API; the third shows what actually shipped.

| PZ daemon (pre-extraction) | Sketched target | Shipped |
|---|---|---|
| `OverlayService::ensurePassiveShellFor` | `ShellHost::start` | `ShellHost::ensureShell` (+ daemon shim of the same name) |
| `OverlayService::destroyPassiveShell` | `ShellHost::stop` | `ShellHost::destroyShell` (+ daemon shim of the same name) |
| `OverlayService::syncPassiveShellSurfaceState` | internal to `ShellHost` | `ShellHost::syncSurfaceState(screenId, anyVisible, anyInputGrabbing)`; daemon shim computes the booleans |
| `OverlayService::rekeyOverlayState` | `ShellHost::rekey` (internal-facing) | `ShellHost::rekey` (public) |
| `OverlayService::hideOsdSlotOnScreen` | `Slot::hide()` | `ShellHost::hideSlot(screenId, slotKey, completion)` |
| `OverlayService::hideZoneSelectorSlotOnScreen` | `Slot::hide()` | `ShellHost::hideSlot(screenId, slotKey, completion)` |
| `PerScreenOverlayState` struct | `ShellHost::PerScreenState` (private impl) | `PhosphorOverlay::ShellState` (public lib struct); PZ-content fields stay on daemon's `PerScreenOverlayState` with a borrowed `ShellState*` |
| Per-content `show*` methods | `Slot::show()` + per-content content-write | Per-content `show*` methods stay in daemon (mixed content writes + animator triggers) |

## Risks

- **OverlayService is the daemon's input-region authority.** Every
  slot's visible/invisible state contributes to whether the shell's
  `wl_surface` accepts pointer input on that frame. The library must
  expose this aggregation so the consumer can override (e.g. PZ wants
  modal slots to grab pointer; OSD slots to stay click-through).
  Mistake here would silently break drag-snap.
- **Per-instance role construction.** `PzRoles::makePerInstanceRole`
  appends `-{screenId}-{generation}` to the base role's scope prefix.
  The library must support this (or the consumer-side equivalent) or
  the SurfaceAnimator's longest-prefix lookup misses and animations
  fall back to library defaults. Pin with a regression test
  (the existing `test_overlay_scope_prefixes.cpp` is the model).
- **QML resource pathing.** Today `PassiveOverlayShell.qml` is shipped
  inside the daemon's QML resources. If phosphor-overlay accepts a
  `QUrl` for the shell QML source, the daemon must keep shipping
  `PassiveOverlayShell.qml` AND the library must not implicitly require
  any specific QML object names. Negotiation point: a
  `OverlayContract.qml` interface header that consumers' QML files
  conform to (`osdSlotItem`, `snapAssistSlotItem`, etc. as published
  object names).
- **Animator config drift.** Resolved as shipped: the daemon's
  `applyShaderProfilesToAnimator` routes through
  `ShellHost::registerConfigForRole` (Phase 4) and re-fires from the
  `shaderProfileTreeChanged` signal handler in `settings.cpp`, so
  registration stays current across live tree edits without a
  separate library-side rebuild hook.
- **OverlayService is the IOverlayService implementation.** The
  daemon's interface contract continues to point at `OverlayService`
  during the migration. No interface changes here; the daemon's
  externally-visible D-Bus surface is unchanged.

## Open questions

1. **Does phosphor-overlay ship QML or just accept QUrl?**
   Recommendation: accept QUrl. The library does not own the visual
   identity of any shell; consumers (PZ, Phosphor) ship their own
   `OverlayShell.qml` conforming to the published object-name contract.
2. **Does phosphor-overlay take a hard dep on phosphor-animation-layer
   for SurfaceAnimator, or expose an `ISurfaceAnimator` interface?**
   Recommendation: hard dep. SurfaceAnimator is already the established
   contract; abstracting it just to avoid the dep is the kind of glue
   the overlay-scope memory note explicitly rejects.
3. **Where do `OverlayContentParams` (`LayoutOsdContentParams`, etc.)
   live?** Recommendation: stay in the PZ daemon. The library is
   content-agnostic; these are PZ-shape DTOs that consumers populate
   via `Slot::item()->setProperty()` calls.
4. **Does `ShellHost` enforce one-shell-per-screen?**
   Recommendation: yes. Hard invariant. A consumer that wants multiple
   independent surfaces per screen builds a second `ShellHost` keyed by
   a different scope-prefix family. The library does not silently
   collapse them.
5. **Test scope.** Library-level tests should mock `SurfaceManager` and
   pin the show/hide/animator-config-resolution paths. Integration with
   real animators / shaders stays in PZ's `tests/`.

## Status

| Phase | Status |
|---|---|
| 0a - Plan + signal lift | DONE (#436) |
| 0b - Interface widening for layout queries | DONE (#436) |
| 1 - New library scaffolding | DONE (#436) |
| 2 - ShellHost extraction | DONE (#436): 8 sub-commits, all 4 movable methods landed; validate stays in daemon |
| 3 - Slot extraction | DONE (#436): 3 sub-commits, slot storage + hide path lifted; show-side stays in daemon |
| 4 - Animator config wiring | DONE (#436): registerConfigForRole + makePerInstanceRole on lib; daemon routes through ShellHost |
| 5 - OverlayService shrink + cleanup | DONE (#436): redundant helpers removed; overlayservice.cpp + osd.cpp under 800-line cap |
| 6 - Optional standalone-compositor seam | DEFERRED: lib API surface is pinned by the smoke test and the in-tree PZ consumer; an example app would be a "nice to have" without a current driver |
