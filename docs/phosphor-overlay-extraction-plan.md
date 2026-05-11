<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-overlay extraction plan

**Status:** sketch. Not yet started.

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

## Proposed public API

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

`SlotKey` is a strong-typed `quint32` newtype (no enum, no string) so
third-party plugins can mint their own keys without touching the
library.

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
   shouldn't ship the audio-spectrum config knobs. **Fix:** audio
   spectrum is PZ-specific shader content; keep it consumer-side, route
   it through `ContentDescriptor::animatorConfigBuilder` so the library
   never sees `m_audioProvider`.

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
   registry. For now, put them on the layout registry interface to
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

### Phase 2 - ShellHost extraction (IN PROGRESS in this branch)

Move the per-screen passive-shell mechanism into `phosphor-overlay/`.
Shipping incrementally as sub-commits on the same branch so each step
builds clean. Status of the five method moves the original plan
called for:

| Method | Status |
|---|---|
| `ensurePassiveShellFor` | DONE (2.4) - delegates to `ShellHost::ensureShell` |
| `destroyPassiveShell` | DONE (2.4) - delegates to `ShellHost::destroyShell` |
| `syncPassiveShellSurfaceState` | pending - needs slot-visibility predicates |
| `rekeyOverlayState` | pending - mixes lib + PZ content fields, callback-driven |
| `validateScreenStateInvariant` | pending - debug check on "main overlay live" predicate |

Foundation sub-commits already landed on this branch:

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

The QML side (`PassiveOverlayShell.qml`) stays where it is in the
daemon; the library accepts the qmlSource via the
consumer-registered `SurfaceFactory` callback.

Library API surface after 2.4:

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

### Phase 3 - Slot extraction

- Move per-slot show/hide animator coordination
  (`hideOsdSlotOnScreen`, `hideZoneSelectorSlotOnScreen`, etc.) into
  `Slot` / `SlotRegistry`. Each daemon `show*` method shrinks to:
  ```cpp
  void OverlayService::showLayoutOsd(PhosphorZones::Layout* layout, QString sid) {
      pushLayoutOsdContent(...);                     // PZ-specific
      m_overlay->slot(sid, kOsdSlotKey)->show();     // mechanism
  }
  ```
- `SlotRegistry::registerContent` calls move to `setupOverlay()` at
  daemon startup.

### Phase 4 - Animator config wiring

- Move `applyShaderProfilesToAnimator` / `buildOsdConfig` / friends out
  of `animation_config.cpp` and into per-content `ContentDescriptor`
  builders inside the daemon. The library exposes
  `registerConfigForRole` and the per-instance role construction; the
  daemon keeps the *shape* (curve names, durations, shader paths).

### Phase 5 - OverlayService shrink + cleanup

- The daemon's `OverlayService` should be a few hundred lines of
  PZ-content wiring. Files like `overlayservice/lifecycle.cpp` move
  into the library or get deleted. Per-screen state moves to the
  library's `ShellHost`.
- Per the project's <800-line file cap, the post-extraction
  `OverlayService` TU split should drop from 14 files to 4 or 5.

### Phase 6 - Optional: standalone-compositor seam

- Once phosphor-overlay is consumed by PZ, add a small example app at
  `libs/phosphor-overlay/examples/hello-overlay.cpp` that builds a
  ShellHost with one slot, shows it, hides it. Pins the library's
  external API surface.
- This is also the seam Phosphor-as-standalone would consume.

## Renames + new vocabulary

| PZ today | phosphor-overlay tomorrow |
|---|---|
| `OverlayService::ensurePassiveShellFor` | `ShellHost::start` |
| `OverlayService::destroyPassiveShell` | `ShellHost::stop` |
| `OverlayService::syncPassiveShellSurfaceState` | internal to `ShellHost` |
| `OverlayService::rekeyOverlayState` | `ShellHost::rekey` (internal-facing) |
| `OverlayService::hideOsdSlotOnScreen` | `Slot::hide()` |
| `OverlayService::hideZoneSelectorSlotOnScreen` | `Slot::hide()` |
| `PerScreenOverlayState` struct | `ShellHost::PerScreenState` (private impl) |
| Per-content `show*` methods | `Slot::show()` + per-content content-write |

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
- **Animator config drift.** Today the daemon registers per-content
  animator configs from `applyShaderProfilesToAnimator`. If the
  registration moves to the library's `SlotRegistry`, the daemon must
  re-fire registration on `shaderProfileTree` change. The library
  should expose a `Slot::rebuildAnimatorConfig()` for this.
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
| 2 - ShellHost extraction | IN PROGRESS (#436): 2.1-2.4 shipped, sync/rekey/validate pending |
| 3 - Slot extraction | pending |
| 4 - Animator config wiring | pending |
| 5 - OverlayService shrink + cleanup | pending |
| 6 - Optional standalone-compositor seam | pending |
