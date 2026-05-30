<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 04: Implementation Plan

This is the **execution playbook** for the gaps inventoried in `02-gap-analysis.md` and the architecture defined in `03-component-map.md`. It refines the M0-M6 milestone sketch from `02` into a slower, library-first roll-out: every new library ships with at least one runnable example before any polished UI is attempted.

## Philosophy

1. **Library-first.** Each capability lands as a focused `phosphor-*` library with a clear `IFoo` interface, NOT as a chunk of shell code. This is already how the existing `phosphor-*` tree is structured, extend the pattern.
2. **Example-first.** Each library ships with an `examples/phosphor-<name>-demo/` (and a CLI demo where it makes sense). The example is the acceptance test, if you can't drive the library from a small standalone app, the API isn't right.
3. **Ship thin slices.** Each phase ends at a tagged release. No phase depends on the next; partial completion is shippable.
4. **No premature UI polish.** The mockups in `mockups/` are the *destination*, not the starting point. We earn each surface by completing the libraries beneath it. The final shell UI work happens last, when every primitive is solid.
5. **Run on the Phosphor compositor.** Per [[project-phosphor-standalone-stack]], we are the compositor. The shell binds to `phosphor-compositor` directly. We do not target other compositors as deployment surfaces. Early Phase 1 and Phase 2 libraries are pure data sources or DBus clients and may happen to be testable under another Wayland compositor while `phosphor-compositor` matures in parallel. That is a tactical convenience, not a design goal.

## Where we are (Phase 0, what already exists)

| Asset                                                                                     | State                                                                                                                       |
|-------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| `libs/phosphor-shell` (PanelWindow / PopupWindow / FloatingWindow / ShellEngine / ShellLoader / ScreenModel / LazyLoader / PersistentProperties) | ✓ core shell primitives in place; foundation is solid                                                  |
| `libs/phosphor-config` (`ISettings`, `IConfigBackend`, JSON backend default)              | ✓ per-domain split already done (see `project_settings_page_controllers`)                                                   |
| `libs/phosphor-layer` (layer-shell helper) + `phosphor-overlay` (layer helper)            | ✓ exposed via our compositor; lockscreen / OSDs / overlays consume                                                          |
| `libs/phosphor-shaders` (`corners`, `frosted_glass`, `gradient`, `shadow`)                | ✓ baseline                                                                                                                  |
| `libs/phosphor-zones` / `phosphor-tile-engine` / `phosphor-snap-engine`                   | ✓ tiling differentiator; **none of the reference shells own this**                                                          |
| `libs/phosphor-layout-api` (`ILayoutSourceFactory`)                                       | ✓ registry pattern already exists, generalize, don't reinvent                                                              |
| Data sources (`Process`, `FileView`, `SystemClock`, `UPowerHost`, `WallpaperService`, `Toplevels`) | ✓ in place                                                                                                          |
| `examples/phosphor-shell/` (POC bar + popups, ~2.6 KLOC QML)                              | ✓ proof of concept; do **not** evolve this directly, it's the reference, not the target                                    |

## Plan overview

| Phase | Goal                                          | Duration*  | Visible at end of phase                                                       |
|-------|-----------------------------------------------|------------|-------------------------------------------------------------------------------|
| 1     | Foundation libraries (no real UI)             | 4-6 weeks  | CLI tools, kitchen-sink demos. No shell users yet.                            |
| 2     | Service libraries                             | 8-12 weeks | `phosphorctl` can drive audio/net/bt/brightness/idle/notifications/mpris/tray/polkit/clipboard. Headless. |
| 3     | UI primitives + first visible examples        | 4-6 weeks  | Pz* widgets, bar canvas, OSD/toast frameworks, ConnectedCorner, all runnable in toy windows. |
| 4     | Surface implementations                       | 12-20 weeks| Daily-drivable shell: bar, launcher, notifications, control center, lockscreen, power menu. |
| 5     | Polish + ecosystem                            | open       | Matugen template fan-out, plugin browser, dashboard, dock, color picker, screenshot, clipboard manager, settings app. |

*Single-engineer focused estimates. Phases 1 and 2 parallelize well; 4 partially.

Cross-reference: this plan implements the same gaps as `02-gap-analysis.md` (`M0`-`M6`). The relationship:
- 02's **M0 foundations** = this plan's **Phase 1**
- 02's **M1 bar parity** = Phase 3 (BarCanvas primitive) + Phase 4.1 (real bar)
- 02's **M2 launcher + notifications** = Phase 2 (notifications service) + Phase 4.2/4.3
- 02's **M3 control center + OSDs** = Phase 2 + Phase 3 + Phase 4.4
- 02's **M4 theming pipeline** = Phase 1.1 (theme lib) + Phase 5 (template fan-out)
- 02's **M5 lockscreen + polish** = Phase 4.5 + Phase 5
- 02's **M6 plugins** = Phase 5

If anything in `02` conflicts with this doc, this doc wins. It is newer and execution-oriented.

---

## Phase 1: Foundations

**Goal:** ship the four new libraries the rest of the work depends on. Each ends in an example that can be run by hand without launching the shell.

**Order (if solo):** 1.1 → 1.2 || 1.3 → 1.4 → 1.5

### 1.1: `phosphor-theme` *(shipped, 2026-05-26, PR #534)*

Token store, matugen runner, template engine.

| Deliverable                                                    | Status | Notes                                                                                                            |
|----------------------------------------------------------------|--------|------------------------------------------------------------------------------------------------------------------|
| `libs/phosphor-theme/` (C++)                                   | ✓ shipped | `IThemeService`, `PaletteStore` (QML_SINGLETON with `QFileSystemWatcher` hot-reload), `MatugenRunner` (QProcess, handles v3 + v4+ JSON shapes, always passes `--prefer`), `TemplateEngine` (`{{token[.field]}}`). |
| `qml/Phosphor/Theme/{Theme,Tokens,Motion,StateLayer}.qml`      | ✓ shipped | Singletons. Default values = canonical Phosphor palette ([[project-phosphor-default-palette]]). Theme accessors index `palette[...]` so QML bindings re-evaluate on `paletteChanged`. |
| `examples/phosphor-theme-demo/`                                | ✓ shipped | Swatch sheet with responsive GridLayout, preset switcher (`dark / light / sunset / forest`), and a wallpaper button that drives `MatugenRunner` → `applyTokens` end-to-end. Brand-gradient stops synthesised from M3 accents on each matugen run. `PresetPalettes` lives here, not in the library. |
| `examples/phosphor-theme-cli/`                                 | ✓ shipped | `set-wallpaper` / `dump` / `render-template` / `cycle <dir>` subcommands. Atomic-rename safe writes; matches the same parse path the demo's watcher uses. |
| `libs/phosphor-theme/tests/`                                   | ✓ shipped | 42 cases across `test_palettestore` (17), `test_templateengine` (11), `test_matugenrunner` (14). Covers hot-reload via in-place edit and atomic-rename, debounce coalescing and cancellation, directory-watch release, matugen v3 + v4 JSON parsing, `run(QUrl)` overload, absolute-path normalisation, edge-triggered `runningChanged`, `renderFile` round-trip, `applyTokens` normalisation, and signal-change semantics. ctest-integrated. Offscreen QPA so they run headless. |
| `libs/phosphor-theme/README.md`                                | ✓ shipped | Canonical phosphor-* library README style: responsibility, key types table, typical-use blocks, design notes (binding-tracking rule, merge semantics, matugen schema variants), dependencies. |

**Acceptance:**
- [x] Modifying the palette JSON updates the demo in <100 ms (`QFileSystemWatcher` + atomic-rename re-arm; verified)
- [x] All M3 tokens + ANSI 16 + brand-gradient extensions are exposed (33 tokens via `TokenNames` + default palette)
- [x] Matugen output round-trips through `PaletteStore` correctly (verified end-to-end against a real wallpaper after the matugen-v4+ parser landed)

**Effort:** M (estimated ~2 weeks; actual ~1 session)

### 1.2: `phosphor-popout` *(shipped, 2026-05-26, PR #535)*

Centralized popout coordinator. Single arbiter for popout lifetime, focus,
and exclusivity policy across every transient surface in the shell.

| Deliverable                                                         | Status | Notes                                                                                              |
|---------------------------------------------------------------------|--------|----------------------------------------------------------------------------------------------------|
| `libs/phosphor-popout/` (C++)                                       | ✓ shipped | `PopoutRequest` (value type), `ExclusiveMode` enum (`Cooperative` / `Modal` / `Detached`), `Anchor` enum, `IPopoutService`, `IPopoutTransport` (surface-creation seam), `PopoutController` (arbitration state machine, registered as `QML_UNCREATABLE` element). |
| `qml/Phosphor/Popout/PopoutHost.qml`                                | ✓ shipped | Transport-agnostic wrapper for popout content. Owns the open/close opacity-plus-scale animation via `phosphor-theme` Motion's emphasized M3 curve, the backdrop dim, and click-outside dismiss. Lives in `Phosphor.Popout` not `Phosphor.Helpers` so the lib's QML module is self-contained. |
| `examples/phosphor-popout-demo/`                                    | ✓ shipped | Five toggle buttons (Cooperative A, Cooperative B, Modal, Detached, Close all) plus a status bar binding to `openPopoutIds` and `modalActive`. Ships an `InAppPopoutTransport` that opens popouts as `QQuickItem`s inside the window's host item, no Wayland dependency. The layer-shell-backed transport (`LayerPopoutTransport` wrapping `phosphor-layer::SurfaceFactory`) lands when the shell binary first needs popouts; swapping is a single dependency-injection flip. |
| `libs/phosphor-popout/tests/`                                       | ✓ shipped | 30 cases in `test_popoutcontroller`. FakeTransport mocks the surface layer so the arbitration state machine is exercised as pure logic. Covers cooperative-per-scope replacement, scope independence, modal suppression and stacking, modal-same-id rejection, detached independence including detached-while-modal, empty-id open, transport refusal, toggle behavior, closeAll-clears-modal-count, dismissed-callback routing for cooperative and modal paths, the re-entrancy guard during self-teardown via a contract-violating fake transport, the destructor's callback detach, and the `modalActiveChanged` edge contract. |
| `libs/phosphor-popout/README.md`                                    | ✓ shipped | Canonical phosphor-* library README style: responsibility, key types table, typical-use blocks, arbitration rules, dependencies. |

**Acceptance:**
- [x] Only one cooperative popout open per scope (test + demo verified)
- [x] Modal popout suppresses cooperative popouts on every screen (test + demo verified)
- [x] Popouts dismiss on focus loss when configured (`dismissOnFocusLoss` flag plumbs through to the transport's click-outside MouseArea)
- [x] Open/close animations use M3 emphasized curves from `phosphor-theme` (PopoutHost.qml's Behaviors bind `Motion.emphasized`)

**Effort:** M (estimated ~2 weeks; actual ~1 session for the arbitration core + in-window transport. LayerPopoutTransport deferred until the shell binary needs it.)

### 1.3: `phosphor-registry` *(shipped, 2026-05-27, PR #538)*

Generalize `ILayoutSourceFactory` into five UI-seam registries.

| Deliverable                                                                 | Status | Notes                                                                                  |
|-----------------------------------------------------------------------------|--------|----------------------------------------------------------------------------------------|
| `libs/phosphor-registry/` (C++)                                             | ✓ shipped | Header-only `Registry<T>` template (5 instantiations: `IBarWidgetFactory`, `IControlCenterTileFactory`, `ILauncherProviderFactory`, `IOSDFactory`, `IDesktopWidgetFactory`) backed by a non-template `RegistryNotifier` QObject for `factoryRegistered` / `factoryUnregistered` signals. Common `IFactoryBase` carries `id()` / `displayName()` / `capabilities()`. `Manifest` POD mirrors plugin manifest.json; `PluginLoader` discovers + loads `.so` + manifest from a configurable plugin root via `QLibrary` + a fixed `phosphor_registry_create_factory` C entry point. `PHOSPHOR_PLUGIN_ABI_VERSION` CMake define + `static_assert` keeps the header constant locked to the build. |
| `examples/phosphor-registry-demo/`                                          | ✓ shipped | In-process demo: Repeater-driven bar with two built-in `QmlComponentBarWidgetFactory` instances (clock, color-square). `DemoController` owns the registry, registers built-ins explicitly, and exposes `factoryIds` + `createWidgetFor` to QML. |
| `examples/phosphor-registry-plugin-demo/`                                   | ✓ shipped | Same bar plus a third widget loaded from a separate `.so` (cpu-meter plugin). `PluginLoader` scans a `--plugin-root` directory; hot-reload via `phosphor-fsloader/WatchedDirectorySet` (directory add/remove only; in-place `.so` edits are out of scope because POSIX dlopen refcounts loads by path). |
| `libs/phosphor-registry/tests/`                                             | ✓ shipped | 3 test binaries / ~55 cases: `test_phosphor_registry` (template register/unregister/lookup/enumerate, signal ordering), `test_phosphor_registry_manifest` (every rejection path: malformed JSON, missing fields, abi mismatch, id-vs-dir mismatch, path-traversal id, oversize manifest, empty manifest), `test_phosphor_registry_pluginloader` (5 fake-plugin .so fixtures exercising happy path, id-mismatch, null-factory, missing-entry-point, corrupt .so, hot-reload add + remove, library-pin-before-factory-destroy). Templated `manifest.json.in` fixtures linked to the ABI version via `configure_file @ONLY`. |
| `libs/phosphor-registry/README.md`                                          | ✓ shipped | Responsibility / Key types / Typical use (C++ shell composition root + glue-layer `BarController` + Repeater-driven Bar QML) / Arbitration & lifecycle / Dependencies. Documents the dlopen-refcount limitation explicitly. |

**Acceptance:**
- [x] Registry enumerates factories; plugin loads from `~/.local/share/phosphor/plugins/<id>/` (XDG-honouring; `--plugin-root` overrides for CI / demos)
- [x] Capability declarations are present in `IFactoryBase::capabilities()` + manifest `capabilities` array (enforcement deferred to Phase 5 per plan)
- [x] Plugin hot-reload by directory add/remove verified end-to-end (rename + restart still required for in-place .so edits; dlopen-refcount limitation documented in the README)

**Effort:** M (estimated ~2 weeks; actual ~1 session for the template + plugin loader + 2 demos. Four audit cycles before merge; final state 198 / 198 tests pass, 0 outstanding findings.)

### 1.4: `phosphor-ipc` + `phosphorctl` *(shipped, 2026-05-27, PR #539)*

| Deliverable                                                       | Status        | Notes                                                                                |
|-------------------------------------------------------------------|---------------|--------------------------------------------------------------------------------------|
| `libs/phosphor-ipc/` (C++)                                        | shipped       | `IpcRouter` + `IpcTarget` (`QML_ELEMENT`, not attached property) + `IpcSchemaGenerator` (`QMetaObject` to JSON Schema) + `IpcEngine::install` engine-property bridge. NDJSON wire protocol over `QLocalServer` / `QLocalSocket` on `$XDG_RUNTIME_DIR/phosphor.sock`. Subscribe is in scope (typed signal streaming). |
| `cli/phosphorctl/` (C++ / Qt6)                                    | shipped       | Standalone binary linking Qt6::Core + Qt6::Network only (no QML, no GUI). Subcommands: `call`, `list`, `schema`, `subscribe`. Source lives in `cli/phosphorctl/` rather than `bin/phosphorctl/` because `bin/` is the project's runtime output dir; a same-named source subdir would collide with the binary's final path. |
| `examples/phosphor-ipc-demo/`                                     | shipped       | QML declares 3 IpcTargets (`greet`, `count`, `set-value`); they self-register via `IpcEngine::install`. `phosphorctl call greet.sayHello --arg name=nate` works end-to-end; the demo window mirrors the broadcast stream in a live-events panel so a single sidecar terminal exercises the full surface. |
| `libs/phosphor-ipc/tests/`                                        | shipped       | 6 test binaries (protocol parser, router invoke / register / schema, schema-generator type table, e2e socket roundtrip, subscribe / broadcast / disconnect-prune, engine install / uninstall / replace) for ~75 cases including stale-socket recovery, live-listener-not-clobbered, multi-subscriber fan-out. |

**Acceptance:**
- [x] `phosphorctl list` enumerates registered targets
- [x] `phosphorctl schema <target>` dumps JSON schema with arg types
- [x] Typed args validated server-side (`QVariant::convert`); errors are structured (`NO_SUCH_TARGET`, `NO_SUCH_FN`, `NO_SUCH_SIGNAL`, `NO_SUCH_SUBSCRIPTION`, `INVALID_ARG`, `INVOCATION_FAILED`, `MALFORMED_REQUEST`)
- [x] `phosphorctl subscribe <target>.<signal>` streams JSON events until Ctrl+C, with a clean unsubscribe handshake
- [x] Wraps (does not replace) existing D-Bus adaptors. Phase 2 service libs register both an IPC target and a D-Bus method for each callable.

**Effort:** M (~2 weeks estimated; actual ~1 session for the lib + CLI + demo + tests, plus audit cycles)

**Deferred items:** see [`phosphor-ipc-followups.md`](phosphor-ipc-followups.md) for the defense-in-depth additions (per-process MaxConnections, idle-connection timeout, SO_PEERCRED peer auth) and the fleet-wide demo cleanups (qsTr → i18n, hard-coded monospace → Tokens) that were intentionally NOT applied in PR #539.

### 1.5: `PerScreen` QML helper *(shipped, 2026-05-28, PR #540)*

A bare `Instantiator { model: PhosphorShell.screens }` would technically
satisfy "one delegate per monitor", but `ScreenModel` ships hot-plug
via `beginResetModel`/`endResetModel` rather than row-incremental
signals, so the naive binding tears down every delegate on every
monitor change, even for screens that didn't move. `PerScreen` exists
to do the work `Instantiator` cannot:

1. **Reset-aware delegate reuse.** Diff the new screen set against the
   live delegates keyed by `QScreen*` identity; reuse delegates whose
   screen survived the reset, only construct delegates for genuinely
   new screens, only destroy delegates for genuinely removed screens.
   This is what makes per-monitor wallpapers / overlays viable in
   production (a third monitor plugging in doesn't strip the wallpaper
   off the first two).
2. **Per-monitor placement plumbing.** Inject `phosphorScreen`
   (`QScreen*`), `name`, `index`, `isPrimary` into the delegate via
   required properties; consumers position their delegate via the
   screen's `geometry` rect, e.g.
   `x: phosphorScreen.geometry.x + 40` for virtual-desktop
   coordinates. (The QML-side `Window.screen` property is
   read-only `QQuickScreenInfo*`, NOT `QScreen*`, so there's no
   QML-level setter for `QQuickWindow::setScreen()`; on X11 the
   virtual-desktop x/y route is how a QML consumer anchors a
   Window to a specific monitor, and on Wayland the compositor
   decides final placement regardless.)

Without (1) and (2) the helper is sugar and the demo writes the same
plain `Instantiator` binding under a different name; with them it
absorbs the per-monitor-lifecycle boilerplate every overlay/wallpaper
consumer would otherwise duplicate.

| Deliverable                                       | Notes                                                                            |
|---------------------------------------------------|----------------------------------------------------------------------------------|
| `libs/phosphor-shell/qml/Phosphor/Shell/`         | First QML module shipped by `phosphor-shell`. URI `Phosphor.Shell`. Wired via `qt_add_qml_module(PhosphorShellQml ...)` alongside the existing core library target, mirroring the `phosphor-theme` / `phosphor-popout` split. No new top-level lib; `PerScreen` lives next to `ScreenModel`, which it depends on. |
| `libs/phosphor-shell/qml/Phosphor/Shell/PerScreen.qml` | Reset-aware Instantiator wrapper as described above. Accepts a `model` (the `ScreenModel`-shaped QAbstractItemModel; the helper does NOT default it, so the file is usable from tests and non-shell processes) and a `delegate` Component. Self-coordinates pre-shutdown delegate teardown via `Qt.application.onAboutToQuit`; hosts do not need to wire anything for clean exit. |
| `libs/phosphor-shell/tests/`                      | Test cases against a fake `QAbstractListModel` that exercises the reset semantics (add, remove, swap, reorder) and verifies delegate identity is preserved across resets for surviving rows. |
| `examples/phosphor-perscreen-demo/`               | Opens a small floating window per monitor showing the monitor's name + index + primary flag. Hot-plug a monitor → window appears. Plug it out → window goes away. Live primary swap → only the primary-pill on the affected windows updates (no full teardown, which pins the reuse path). |

**Acceptance:**
- [x] Monitor hot-plug add/remove correctly mirrors in the instantiated delegates.
- [x] Delegates for surviving screens are NOT recreated on hot-plug. The same QObject identity persists (asserted in the test).
- [x] Primary-screen swap updates the `isPrimary` property without recreating any delegate.
- [x] Delegates receive `phosphorScreen` (`QScreen*`), `name`, `index`, `isPrimary` as required properties so consumers can position their Window via `phosphorScreen.geometry` without manual lookup.

**Effort:** M (~1 week. The reset-aware diff plus the test harness for hot-plug semantics is the main lift.)

**Phase 1 gate:** All five demos run. Tag `phosphor-foundations-0.1`.

**Phase 1 progress (as of 2026-05-28):** 5 of 5 libs shipped. Phase 1 gate met.

| Lib                   | Status                                                  |
|-----------------------|---------------------------------------------------------|
| `phosphor-theme`      | ✓ shipped (PR #534)                                     |
| `phosphor-popout`     | ✓ shipped (PR #535)                                     |
| `phosphor-registry`   | ✓ shipped (PR #538)                                     |
| `phosphor-ipc`        | ✓ shipped (PR #539)                                     |
| `PerScreen` helper    | ✓ shipped (PR #540)                                     |

All five demos run. The `phosphor-foundations-0.1` tag is now unblocked. Next action.

---

## Phase 2: Service libraries

**Goal:** in-process C++ services for every desktop integration, each with a CLI demo that proves the contract. **No shell UI yet**, just data, exposed via QML facades + `phosphorctl`.

### Naming, layout, and the dissolved umbrella

(Phase 2.0 has shipped: the umbrella is gone. The history below records the design rationale for future reference.)

The original `phosphor-services` library was an umbrella whose CMakeLists explicitly anticipated `Notifications`, `MPRIS`, `UPower`, `NetworkManager`, and `logind` as "future siblings" *inside* it. **This plan deviated from that**: the umbrella was dissolved into one lib per tenant. The per-domain libs (both the 2.0 extractions and the 2.1-2.10 additions) all carry a `phosphor-service-*` prefix. The prefix is a group-tag: anything `phosphor-service-*` is a shell-domain integration that surfaces OS state via D-Bus, sysfs, or native APIs and lets the shell drive system actions, distinct from foundation libs like `phosphor-dbus`, `phosphor-layer`, or `phosphor-rendering` that are infrastructure consumed across tiers. The split makes `ls libs/` self-documenting: foundation libs in one visual group, service libs in another.

The shared "-common" already exists: **`phosphor-dbus`** is the service-agnostic DBus plumbing (`client.cpp`, error/cancel handling, custom-type marshalling). Every new service lib in this phase consumes `phosphor-dbus` instead of duplicating boilerplate.

Each service follows the same shape:
- `libs/phosphor-service-<domain>/` (C++), focused, one concern, depends on `phosphor-dbus` if it talks DBus
- QML singleton `<Domain>.qml` (e.g. `Network`, `Bluetooth`); the user-facing surface keeps the short name
- `examples/phosphor-service-<domain>-cli/`, small binary that reads + controls the service
- `phosphorctl` namespace (e.g. `phosphorctl call network.scan`); same short name. The prefix is for the lib directory only.

### 2.0: Extract existing `phosphor-services` tenants

The umbrella already houses four tenants. Before adding new ones, extract these to follow the pattern (small, mostly-mechanical moves; update consumers in the same PRs):

| Source                                             | New home                                 | Status | Notes                                                                          |
|----------------------------------------------------|------------------------------------------|--------|--------------------------------------------------------------------------------|
| `phosphor-services/src/statusnotifier/`            | `phosphor-service-sni/`                  | ✓ shipped | StatusNotifierItem host + watcher + dbusmenu. Namespace `PhosphorServiceSni::`, QML module `Phosphor.Service.Sni 1.0`. Consumes `phosphor-service-icontheme` for icon resolution + image-provider plumbing. The forced-include of `dbustypes.h` + `UNITY_BUILD OFF` invariants carried over per the original CMakeLists rationale. |
| `phosphor-services/src/mpris/`                     | `phosphor-service-mpris/`                | ✓ shipped | MPRIS2 client + player aggregation. Namespace `PhosphorServiceMpris::`, QML module `Phosphor.Service.Mpris 1.0`. Used by `examples/phosphor-shell/Mpris{Widget,Content,PlayerState}.qml`; consumers updated in the same PR. |
| `phosphor-services/src/upower/`                    | `phosphor-service-upower/`               | ✓ shipped | Battery/power-supply readouts. Namespace `PhosphorServiceUPower::`, QML module `Phosphor.Service.UPower 1.0`. Used by `examples/phosphor-shell/`; consumers updated in the same PR. |
| `phosphor-services/src/icontheme/` + `iconimageprovider.*` | `phosphor-service-icontheme/`    | ✓ shipped | Icon-theme resolution + Qt image provider. Namespace `PhosphorServiceIconTheme::`, QML module `Phosphor.Service.IconTheme 1.0`. Image-provider URL host renamed `phosphor-services` → `phosphor-service-icontheme`; SNI publisher routes through `imageProviderUrlHost()` to keep the rename a link-failure rather than a silent miss. Folded with rendering deferred to a later judgement; kept standalone for now to match the per-domain pattern. |
| `libs/phosphor-services/` umbrella                 | **deleted**                              | ✓ shipped | Removed in the same commit as the SNI extraction. No backwards-compat shim, per `feedback_no_legacy_shims`. |

**Effort:** S-M total. Mechanical except for any cross-tenant helper code that needs to move into `phosphor-dbus` or a new tiny `phosphor-service-icontheme`. Per CLAUDE.md, every call site updates in the same PR.

**Approach taken:** one commit per tenant, in order of independence (upower → mpris → icontheme → sni). The umbrella deletion folded into the SNI commit because the umbrella has no remaining source files at that point; keeping it as an empty CMake target would have been a backwards-compat shim, which the project forbids.

**Result:** Phase 2.0 complete. `ls libs/` now shows four `phosphor-service-*` siblings (icontheme / mpris / sni / upower) and the umbrella is gone.

### 2.1-2.10, New service libraries

Naming notes:
- The full PipeWire mixer goes into `phosphor-service-pipewire`. (`phosphor-audio` is unrelated; that's the cava-spectrum lib for audio-reactive shaders; the prefix makes the distinction self-evident in `libs/`.)
- `phosphor-shortcuts` already exists with a `kglobalaccel/portal/dbus` backend pattern, use the same pattern for any service with multiple possible backends. (It pre-dates the `phosphor-service-*` group and isn't renamed for backwards-compat reasons; new service libs adopt the prefix.)

| #    | Library                              | Backend                                                                              | CLI demo capabilities                                          | Effort |
|------|--------------------------------------|--------------------------------------------------------------------------------------|----------------------------------------------------------------|--------|
| 2.1  | `phosphor-service-pipewire`          | PipeWire (libpipewire); reads/writes the WirePlumber-populated default metadata global via pw_metadata | list sinks/sources, set volume, set default, mute              | L      |
| 2.2  | `phosphor-service-network`           | DBus `org.freedesktop.NetworkManager` (via `phosphor-dbus`); libnm optional          | list connections, scan wifi, connect to AP                     | L      |
| 2.3  | `phosphor-service-bluetooth`         | DBus `org.bluez` (via `phosphor-dbus`)                                               | list adapters/devices, scan, pair, connect                     | M      |
| 2.4  | `phosphor-service-brightness`        | `/sys/class/backlight` + `org.freedesktop.login1.Session.SetBrightness`              | get/set brightness, list devices                               | S      |
| 2.5  | `phosphor-service-notifications`     | server side of `org.freedesktop.Notifications` (via `phosphor-dbus`)                 | runs as standalone daemon for demo; logs received notifications | L      |
| 2.6  | `phosphor-service-polkit`            | polkit-qt6 binding                                                                   | runs as standalone agent demo; prints auth requests, accepts a hardcoded password | M |
| 2.7  | `phosphor-service-idle`              | `idle-inhibit-unstable-v1` client + timer state machine                              | inhibit toggle, timeout config, fired-at log                   | S      |
| 2.8  | `phosphor-service-clipboard`         | `wlr-data-control` + on-disk history (cliphist-style)                                | watch / list history / copy nth entry                          | M      |
| 2.9  | `phosphor-service-lock`              | PAM (`pam_authenticate`) + coordination with ext-session-lock-v1                     | CLI demo authenticates a user and prints success/failure       | M      |
| 2.10 | `phosphor-service-session`           | DBus `org.freedesktop.login1` (logind, via `phosphor-dbus`)                          | lock, suspend, hibernate, reboot, shutdown, read available capabilities first | S |

(Existing `phosphor-service-mpris`, `phosphor-service-sni`, `phosphor-service-upower` from 2.0 cover what would have been service rows 11-13 in the prior plan.)

**Total effort:** L-XL (8-12 weeks solo, including 2.0 extractions). Parallelizes well across 2-3 engineers, `phosphor-dbus` is the only shared dep, and it's already there.

**Phase 2 gate:** each library has a passing CLI demo; `phosphor-services` umbrella is deleted; `phosphorctl call <ns>.<fn>` works for every service. Tag `phosphor-integrations-0.1`.

### 2.1: `phosphor-service-pipewire` post-mortem *(shipped)*

This section is the post-implementation record of the shape that landed on `feat/phase-2.1-pipewire-service`. The library shipped end-to-end (milestones 1-10), so the milestone list below documents what is in the tree, not a forward pickup plan. Where the shipped surface diverges from the original sketch in `02-gap-analysis.md`, this section is authoritative.

**Milestones**

1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-pipewire/` directory with the same shape Phase 2.0 established: `CMakeLists.txt`, `PhosphorServicePipeWireConfig.cmake.in`, `include/PhosphorServicePipeWire/`, `src/`, `tests/`. Imperative QML registration via `src/qmlregistration.cpp` (`qmlRegisterUncreatableType` for `PipeWireConnection` / `PwNode` / `PwNodeModel`, `qmlRegisterType` for `PwSinkModel` / `PwSourceModel` / `PwStreamModel`, `qmlRegisterSingletonType` for `PipeWireHost`), idempotency-guarded with `std::call_once`. This matches the sibling `phosphor-service-mpris` / `-upower` / `-sni` / `-icontheme` pattern: the lib ships no `.qml` files and installs nothing under `${KDE_INSTALL_QMLDIR}`; only the C++ shared library is installed and the QML module is process-global once `registerQmlTypes()` is called. The shell composition root (`src/shell/main.cpp`) calls `PhosphorServicePipeWire::registerQmlTypes()` alongside the four 2.0 sibling services so the QML module is process-global before any consumer mounts a `Phosphor.Service.PipeWire` import.
2. **PipeWire core-connect lifecycle (M, ~3 days).** `PipeWireConnection` owns the `pw_main_loop` + `pw_context` + `pw_core`. Off-thread loop on a `QThread` (PipeWire's loop is not Qt-event-loop friendly), with a `QObject` bridge that posts events to the GUI thread via queued signals. Surface `pw_core` errors via the `error(QString)` signal + `connected → false` flip; the lib does not auto-reconnect (the shell drives backoff via `PipeWireConnection::connectToDaemon()` / `PipeWireHost::reconnect()`). No public surface yet, just the lifecycle and the cross-thread plumbing.
3. **Object registry: nodes (M-L, ~5 days).** The registry walk is folded into `PipeWireConnection::Private` rather than a standalone `PipeWireRegistryObserver` class. The Private holds the `pw_registry` listener and materialises one typed Qt object family: `PwNode` (parented to the connection, vended to consumers via `nodeAdded` / `nodeRemoved` signals). Property updates route through queued signals from the loop thread to the GUI thread. Filtering happens consumer-side via `PwNodeModel::mediaClasses` (`Audio/Sink`, `Audio/Source`, `Stream/Output/Audio`, `Stream/Input/Audio`); the connection surfaces every audio-class node and lets the models decide who they show. Ports, links, and devices are deliberately out of scope for 2.1 (the mixer use case is node-only); if a later phase needs them they land as sibling typed objects without changing the existing surface.
4. **Sink/source/stream models (M, ~4 days).** `PwSinkModel`, `PwSourceModel`, `PwStreamModel` (per-app streams) as `QAbstractListModel` over the connection's filtered view. Role list pinned to: `node` (the `PwNode*` itself), `id`, `name`, `nick`, `description`, `mediaClass`, `channelCount`, `volumes` (per-channel linear amplitudes), `muted`, plus `Qt::DisplayRole` (`display`) that falls back through nick → description → name. Default-node membership is read from `PipeWireHost.defaultSinkName` / `defaultSourceName` rather than carried as a per-row role, so bindings stay reactive without the model re-emitting `dataChanged` on every default swap. Role enum integer values + role-name strings pinned via smoke tests (the Phase 2.0 sni-shell pattern).
5. **Volume + mute write path (M, ~3 days).** `PwNode::setVolume(qreal)`, `setVolumes(QList<qreal>)`, `setMuted(bool)`. PipeWire writes go through `pw_node_set_param(SPA_PARAM_Props)` with a `SPA_PROP_channelVolumes` array; round-trip through the registry signal echo so the model reflects clamped values. Per-channel volume + Linear vs Cubic mapping decision (see Unknowns).
6. **Default-sink / default-source switching (S, ~1-2 days).** Through `pw_metadata` against the WirePlumber-populated `default` metadata global. Read `default.audio.sink` / `default.audio.source`, write `default.configured.audio.sink` / `default.configured.audio.source`. The write silently no-ops when no WirePlumber-managed metadata is bound (so the lib loads inert without WirePlumber).
7. **QML registration + singleton facade (S, ~1 day).** `Phosphor.Service.PipeWire 1.0` exposes the three concrete models (`PwSinkModel`, `PwSourceModel`, `PwStreamModel`), the uncreatable types (`PipeWireConnection`, `PwNode`, `PwNodeModel`), and the `PipeWireHost` host singleton (`connection`, `connected`, `daemonAvailable`, `defaultSinkName`, `defaultSourceName`). The defaults expose the WirePlumber-tracked node names (not opaque ids) so QML can compare them directly against `model.name` without a registry lookup. Idempotent registration via `std::call_once` to match siblings.
8. **CLI demo: `examples/phosphor-service-pipewire-cli/` (M, ~3 days).** Standalone `QCoreApplication`. Subcommands (the `<target>` argument resolves a node by name OR by numeric id, so callers can paste either column from `list`): `list sinks`, `list sources`, `list streams`, `default` (prints the WirePlumber default sink + source names), `set-volume <target> <pct>`, `set-default-sink <target>`, `set-default-source <target>`, `mute <target>`, `unmute <target>`. Default-sink and default-source switching are split into distinct subcommands so the action's domain is visible at the CLI without inferring it from the node's media class. Drives the lib directly, no IPC, same pattern as `examples/phosphor-theme-cli/`.
9. **Tests: smoke + role pinning (S, ~1 day).** No real PipeWire on CI; the lifecycle path is exercised inline in `tests/test_smoke.cpp`: 15 cases pinning the public contract (lifecycle, role names + integers, convenience filters, host forwarding, WirePlumber metadata, write-API safety, post-disconnect teardown); see `tests/test_smoke.cpp` for the catalogue. The daemon-dependent subset uses `QSKIP` when no PipeWire daemon is present (effectively running as smoke on CI and as integration on dev hosts). A dedicated PipeWire-loop fixture lands only if a later milestone needs integration-style coverage; the smoke harness is sufficient for the public contract today.
10. **README + Status section (S, half a day).** Match the Phase-2.0 sibling convention: SPDX → one-line summary → Responsibility → Key types → Typical use → Design notes → Dependencies → Status. Status leads with `Phase 2.1: shipped.` once the gate passes.

**Total effort:** L (≈ 3-4 wks solo). Most of the wall-clock is in milestones 2-3-5; the rest is mechanical given the Phase-2.0 templates.

**Dependencies**

- `libpipewire-0.3` (ABI 1.0+ minimum per U1 resolution; tracks Arch / Fedora 38+ / Ubuntu 24.04).
- WirePlumber 0.5 runtime (the lib reads/writes the `default` metadata global that WirePlumber populates; with no libwireplumber link the lib loads inert without WirePlumber being present, surfacing empty defaultSinkName/defaultSourceName).
- `phosphor-dbus`: not used. PipeWire surface is native, not D-Bus. (Brightness fallback for the same shell area lives in 2.4 and DOES use D-Bus.)
- Qt6 ≥ 6.6 Core / Qml. No QtGui dependency (mixer state is non-visual; QML consumers handle rendering).
- Build dep on `pkgconf` for `libpipewire-0.3` discovery, scoped to `libs/phosphor-service-pipewire/CMakeLists.txt`. No top-level wiring needed because the BUILD_PHOSPHOR_SHELL gate keeps the subdir from compiling on hosts without the dev package.

**Risks**

- **PipeWire loop threading.** `pw_main_loop` is not a Qt event loop; mixing it with Qt's invokeMethod / queued signals has historically been a footgun. Mitigation: own a dedicated `QThread` with the loop, post into Qt via `QMetaObject::invokeMethod(..., Qt::QueuedConnection, ...)`. Keep the cross-thread API surface narrow (the host singleton's slots), do NOT expose raw PipeWire objects to QML.
- **WirePlumber availability variance.** Some distros (musl-based, embedded) don't ship wireplumber. The lib always talks raw `pw_metadata` against the `default` global; with no WirePlumber-managed metadata bound, reads surface empty `defaultSinkName` / `defaultSourceName` and writes silently no-op. The lib loads inert; the shell sees the empty defaults and degrades gracefully.
- **Volume cubic-vs-linear.** PipeWire's `SPA_PROP_channelVolumes` is linear amplitude; user-facing percentages typically want cubic (matches pavucontrol / GNOME Sound). Decide on the storage shape (we expose linear internally) and provide a `Phosphor.Service.PipeWire.Mixer` QML helper to convert.
- **Daemon restart correctness.** PipeWire daemon restarts during user sessions (driver hotplug, kernel module reload). The host must drop and re-acquire the registry without leaking the model rows or zombie streams. Mirror Phase-2.0 SNI's `watcher promoted` pattern: announce the rebuild via a signal, model resets, no zombie items.
- **CI without real PipeWire.** Smoke tests must construct the host with `daemonAvailable=false` and not crash. Same as the SNI `modelWithoutHostIsEmpty` shape.
- **API churn between PipeWire 0.3 / 1.0 / WirePlumber 0.4 / 0.5.** Pin minimum versions in CMake and document the upgrade path (see Unknowns).

**Unknowns to resolve before / during implementation**

| # | Question | Resolution |
|---|----------|------------|
| U1 | Minimum supported PipeWire version: 0.3.60 (Debian stable baseline) or 1.0 (modern distros, cleaner WP metadata API)? | **Resolved: 1.0+ minimum.** Tracks Arch / Fedora 38+ / Ubuntu 24.04. One code path; the WirePlumber-populated `default` metadata global accessed via `pw_metadata` is the only supported surface for default-node switching. |
| U2 | Linear vs cubic vs perceptual (M3 power-curve) volume on the public QML surface. | **Resolved: linear amplitude on the public surface.** Matches PipeWire's `SPA_PROP_channelVolumes` storage. A `Phosphor.Service.PipeWire.Mixer.Curve` QML helper converts cubic / perceptual at the UI layer; round-trips through the lib stay lossless. |
| U3 | One process or two: keep PipeWire owned by the same `QGuiApplication` as the shell, or run a tiny `phosphor-pipewire-daemon` and talk via `phosphor-ipc`? | **Resolved: single-process.** PipeWire owned by the same `QGuiApplication` that runs the shell, matching the Phase-2.0 sni / mpris / upower pattern. The cross-thread plumbing is one `QThread` for `pw_main_loop` with queued-signal events to the GUI thread; no IPC contract to maintain. |
| U4 | Scope of stream metadata: do we surface PipeWire's per-stream tags (`application.name`, `application.icon-name`, `media.role`) or stop at the WirePlumber-curated set? | **Resolved: WirePlumber-curated set on the named roles, full `properties` hash for advanced bindings.** `PwNode::name`, `nick`, `description`, `mediaClass`, `channelCount`, `volumes`, `muted` cover the common mixer use; `properties()` returns the full `node.*` / `application.*` / `media.*` hash for callers (icon lookup, privacy categorisation) without bloating the model role list. |
| U5 | Privacy-indicator hook: should `phosphor-service-pipewire` surface a `recordingActiveStreams` signal in Phase 2.1, or defer to a separate `Phosphor.PrivacyIndicator` consumer in Phase 3+? | **Resolved: deferred to Phase 3+.** Out of scope here; the privacy consumer can derive the same view from `PwSourceModel` + the model's per-row stream filter. |
| U6 | CLI demo argv format: positional (`list sinks`) or subcommand-with-flags (`list --kind sink`)? Tradeoffs in the row: positional matches Phase-2.0's idiomatic feel; flags compose better with future filters. | **Resolved: positional.** Matches the row in the 2.1 table and the Phase-2.0 idiomatic feel; future filters can land as additional positional words (`list sinks --json` style) without breaking existing usage. |

**Out of scope for 2.1.** The mixer **UI** (slider strip, per-app expand, OSDs) is Phase 3 / 4 territory and lives in `examples/phosphor-shell/` or a dedicated `phosphor-mixer` demo. The shell's hotkey-driven volume actions wire through `phosphorctl call mixer.setVolume ...` once a Phase-3 `IpcTarget` lands on top of this lib.

---

### 2.3: `phosphor-service-bluetooth` *(planned)*

Forward plan for `feat/phase-2.3-bluetooth-service`, in the shape of the §2.1 narrative. Backend is pure D-Bus `org.bluez` on the **system bus** via `phosphor-dbus`, with no `libbluetooth` / QtConnectivity dependency (keeps the wire contract direct; unlike 2.2 there is no optional native backend). Namespace `PhosphorServiceBluetooth`, export macro `PHOSPHORSERVICEBLUETOOTH_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Bluetooth 1.0`.

Two structural firsts versus 2.2:

- BlueZ is **ObjectManager-rooted** (`/` → `GetManagedObjects` returning `a{oa{sa{sv}}}`, plus `InterfacesAdded` / `InterfacesRemoved`), not the per-interface add/remove signals NetworkManager exposes.
- This is the first service lib to act as a **D-Bus server** — it *exports* an `org.bluez.Agent1` object that bluez calls back into during pairing. (2.5 notifications is the only other server-side lib; the `qt6_add_dbus_adaptor` Agent1 pattern set here is the precedent.)

**Design decisions taken up front** (see Unknowns for the rest):

- **ObjectManager support lands in `phosphor-dbus`** (foundation tier), not in the bluetooth lib, so 2.5 (notifications) and 2.10 (session/logind) reuse it rather than re-implementing the `a{oa{sa{sv}}}` walk.
- **Full `org.bluez.Agent1`** — every callback implemented (not a Just-Works-only or minimal agent), with interactive callbacks using delayed D-Bus replies driven by a consumer (CLI now, pairing dialog in Phase 3/4).

**Milestones**

0. **`phosphor-dbus` ObjectManager observer (M, ~3-4 days).** *Foundation prerequisite — lands in `phosphor-dbus`, not the bluetooth lib.* New service-agnostic `PhosphorDBus::ObjectManager`: binds `(connection, service, rootPath)`, issues `GetManagedObjects` async, subscribes to `InterfacesAdded` / `InterfacesRemoved`, and emits raw-map signals `interfacesAdded(QString path, QMap<QString, QVariantMap>)` / `interfacesRemoved(QString path, QStringList)` plus a `ready()` once the initial walk lands. Owns the `a{oa{sa{sv}}}` demarshalling decision once (hand-iterated `QDBusArgument`, as `NetworkConnection` did for `a{sa{sv}}`). Its own smoke test pins the demarshalling + add/remove contract against a synthetic reply. Kept deliberately un-typed so each consumer materialises its own typed objects.
1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-bluetooth/` mirroring the 2.2 shape (`CMakeLists.txt`, `PhosphorServiceBluetoothConfig.cmake.in`, `include/PhosphorServiceBluetooth/`, `src/`, `tests/`, `examples/phosphor-service-bluetooth-cli/`). Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the six existing services. `BUILD_PHOSPHOR_SHELL`-gated.
2. **`BluetoothHost` facade + ObjectManager wiring (M, ~3 days).** Central facade (like `NetworkHost`) built on the milestone-0 observer pointed at `org.bluez` `/`. Materialises typed `BluetoothAdapter` (per `Adapter1`) and `BluetoothDevice` (per `Device1`), parented to the host, vended via `adapterAdded` / `adapterRemoved` + `deviceAdded` / `deviceRemoved` (+ count signals). Inert when bluez is absent. Handles adapter removal cascading to its child devices.
3. **`BluetoothAdapter` + `BluetoothAdapterModel` (M, ~2-3 days).** Properties Address / Name / Alias / Powered / Discoverable / Pairable / Discovering (`Q_PROPERTY`, emit-on-change `setField` pattern). Writes: `setPowered` / `setDiscoverable` via `Properties.Set`; `startDiscovery()` / `stopDiscovery()` (with an optional `SetDiscoveryFilter`). `QAbstractListModel` over the host's adapters.
4. **`BluetoothDevice` + `BluetoothDeviceModel` (M, ~3-4 days).** Properties Address / Name / Alias / Icon / Class / Appearance / Paired / Trusted / Blocked / Connected / RSSI / UUIDs / adapter-path. Writes: `connect()` / `disconnect()` (fire-and-forget `Connect` / `Disconnect`), `setTrusted` / `setBlocked`. `QAbstractListModel` with an adapter-path filter (mirrors `AccessPointModel`'s device-scoped subscription). Derived display name: Alias → Name → Address.
5. **`Agent1` + `AgentManager1` registration — full agent (L, ~5-6 days).** The load-bearing milestone. `org.bluez.Agent1` via `qt6_add_dbus_adaptor` from a checked-in `org.bluez.Agent1.xml`; export on a fixed path, `RegisterAgent(path, "KeyboardDisplay")` + best-effort `RequestDefaultAgent`. Implement every callback: `Release`, `RequestPinCode` → `s`, `DisplayPinCode`, `RequestPasskey` → `u`, `DisplayPasskey`, `RequestConfirmation`, `RequestAuthorization`, `AuthorizeService`, `Cancel`. Interactive callbacks use **delayed D-Bus replies**: the agent emits a Qt request signal (e.g. `confirmationRequested(device, passkey, requestId)`) and a consumer calls `respondConfirmation(id, accept)` / `respondPasskey(...)` / `reject(id)`, which sends the held reply or `org.bluez.Error.Rejected` / `Canceled`. Single in-flight request with an id + timeout; `Cancel` withdraws it. `BluetoothDevice::pair()` → `Device1.Pair()`; agent callbacks fire during the handshake.
6. **QML registration + facade (S, ~1 day).** `BluetoothHost` as `qmlRegisterType` (instantiable, **not** a singleton — follows 2.2's `NetworkHost` and the no-singletons direction); both models `qmlRegisterType`; `BluetoothAdapter` / `BluetoothDevice` `qmlRegisterUncreatableType`. Agent request signals surfaced on the host so a Phase-3 pairing dialog can bind them. Idempotent via `std::call_once`.
7. **CLI demo: `examples/phosphor-service-bluetooth-cli/` (M, ~3 days).** phosphorctl-style, system bus, sysexits codes (0 ok / 64 usage / 1 runtime). Subcommands covering the gate: `status`, `list-adapters`, `list-devices [adapter]`, `power <on|off>`, `scan [secs]`, `pair <address>`, `connect <address>`, `disconnect <address>`, `trust` / `untrust`, `remove <address>`. The `pair` flow makes the CLI the interactive responder: prints the passkey and reads y/n for `RequestConfirmation`, prompts for PIN / passkey on the request callbacks.
8. **Tests: smoke + role pinning (S-M, ~1-2 days).** No live bluez on CI. Pin: inert host construction (empty models, no crash), role names + enum integers for both models, adapter / device property surfaces, agent-registration safety with the bus absent, and the agent callback → signal → delayed-reply wiring exercised with a synthetic `QDBusMessage` (deterministic, no daemon). `QSKIP` the daemon-dependent paths. (The milestone-0 ObjectManager smoke test lives in `phosphor-dbus`.)
9. **README + Status (S, half a day).** Match the Phase-2.0 sibling convention; Status leads with `Phase 2.3: shipped.` on gate pass; cross-reference §2.1 for the shared milestone narrative and note the `phosphor-dbus` ObjectManager addition.

**Total effort:** M-L (≈ 3 weeks solo). The table's original **M** assumed a lighter agent; full `Agent1` + the reusable ObjectManager push milestones 0 and 5, which hold most of the wall-clock. Milestones 1, 6, 8, 9 are mechanical given the 2.1 / 2.2 templates.

**Dependencies**

- BlueZ ≥ 5.x runtime (`org.bluez` system service). No build-time `libbluetooth`.
- `phosphor-dbus` (now including the milestone-0 ObjectManager observer). Qt6 ≥ 6.6 Core / Qml / DBus; `qt6_add_dbus_adaptor` for the Agent1 XML. No QtGui dependency (state is non-visual).
- System-bus access; pairing needs the agent registered (default-agent acquisition is best-effort).

**Risks**

- **Default-agent conflict.** Only one default agent at a time; gnome-bluetooth / `bluetoothctl` may already hold it. Mitigation: always register the agent; `RequestDefaultAgent` is best-effort with a logged warning (pairing still works as a non-default agent).
- **Delayed-reply correctness.** Each interactive callback must reply exactly once (a value, or `Rejected` / `Canceled`); a dropped or double reply hangs bluez's pairing state machine or trips its timeout. Mitigation: single in-flight request + id + timeout + explicit `Cancel` handling.
- **ObjectManager foundation API churn.** The observer will be consumed by 2.5 / 2.10; get the signal surface right once (raw maps, conservative; per-service typed materialisation).
- **`a{oa{sa{sv}}}` demarshalling.** Deeply nested; decide metatype-registration vs hand-iteration once, inside the observer.
- **System-bus policy.** bluez D-Bus policy may gate pairing / agent operations to specific users / groups; the CLI surfaces `AccessDenied` cleanly.
- **CI without bluez.** Inert construction, empty models, no crash (same shape as 2.2 and the SNI `modelWithoutHostIsEmpty` pattern).

**Unknowns to resolve before / during implementation**

| # | Question | Resolution |
|---|----------|------------|
| U1 | ObjectManager observer surface: typed callbacks or raw maps? | **Resolved: raw-map signals.** `interfacesAdded(QString path, QMap<QString, QVariantMap>)` / `interfacesRemoved(QString path, QStringList)`; each service materialises its own typed objects. Keeps the foundation lib service-agnostic. |
| U2 | Agent capability string? | **Resolved: `KeyboardDisplay`.** Most capable; exercises the full callback set the full-agent decision calls for; bluez selects the right flow per device. |
| U3 | Agent path + single vs multiple in-flight requests? | **Resolved: single agent, fixed path, one in-flight request.** bluez pairs one device per agent at a time. |
| U4 | Discovery filter or plain `StartDiscovery`? | **Resolved: plain `StartDiscovery` for the gate**, with an optional `SetDiscoveryFilter({Transport: auto})`; a transport argument on `scan` can land later. |
| U5 | Device list scope? | **Resolved: all `Device1` objects** bluez reports (paired + discovered); expose Paired / Connected / RSSI so consumers filter (RSSI is absent on cached, out-of-range devices). |
| U6 | Surface `Battery1` / `MediaControl1` interfaces? | **Resolved: out of scope for 2.3** (device-level pair / connect only); land as sibling typed objects later, exactly as ports / links were deferred in 2.1. |

**Out of scope for 2.3.** GATT / profiles (BLE characteristic access), `Battery1` / `MediaControl1` / `MediaPlayer1`, OBEX file transfer, and the pairing **UI** (Phase 3 / 4 — the agent only surfaces the request signals a dialog will bind). Multi-adapter selection UI is likewise deferred.

---

## Phase 3: UI primitives + first visible examples

**Goal:** end-user-visible building blocks that we'll glue together in Phase 4. Each is a runnable demo, not a real shell surface yet.

### 3.1: `Pz*` atom library

| Deliverable                                                                  | Notes                                                                            |
|------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| `qml/Phosphor/Widgets/` (or `libs/phosphor-shell-widgets/qml/`)              | `PzButton`, `PzSlider`, `PzTextField`, `PzCard`, `PzRipple`, `PzPill`, `ElevationShadow`, `StateLayer`, `Motion`-aware transitions |
| `examples/phosphor-widgets-kitchen-sink/`                                    | One window listing every atom in every state (default / hover / pressed / focused / disabled). Theme-switch button at top. |

**Acceptance:** every atom respects the theme tokens; states use M3 state-layer opacities; ripple uses Motion timing.

**Effort:** M (~2 weeks)

### 3.2: `ConnectedCorner` / `ConnectedShape` / `BarCanvas`

The connected-corner geometry primitive, central to the visual identity.

| Deliverable                                                                          | Notes                                                                                   |
|--------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| `qml/Phosphor/Widgets/{ConnectedShape,ConnectedCorner,BarCanvas}.qml`               | QML `Shape` + JS geometry (`ConnectorGeometry.js`). Sockets are a binding; geometry recomputes on socket change with `Behavior on …` Motion easing. |
| `examples/phosphor-bar-canvas-demo/`                                                 | A standalone bar with one button that opens a popout. The popout grows out of the bar with the inverted-corner join, exactly the animation from `mockups/control-center.svg`. |

**Acceptance:** opening/closing the popout morphs the shared Shape; one popout per bar; uses `PopoutService` from Phase 1.2.

**Effort:** L (~3 weeks, the geometry math is the bulk)

### 3.3: OSD framework + first OSDs

| Deliverable                                                  | Notes                                                                                       |
|--------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| `qml/Phosphor/OSD/OSDHost.qml`                               | Single layer-shell surface per screen. Owns timers / debounce / dedupe.                     |
| Built-in OSDs (`VolumeOSD`, `BrightnessOSD`, `MicOSD`, `CapsLockOSD`) | Registered via `IOSDFactory`.                                                       |
| `examples/phosphor-osd-demo/`                                | Standalone window that displays the OSD layer overlay; `phosphorctl call osd.show volume 62` triggers it. |

**Acceptance:** repeated triggers restart the timer; multi-screen routing works; theme-tinted; uses Motion tokens for fade.

**Effort:** M (~2 weeks)

### 3.4: Toast framework

| Deliverable                                                  | Notes                                                                                            |
|--------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| `qml/Phosphor/Notifications/ToastHost.qml` + `Toast.qml`     | Layer-shell surface per screen, stacks toasts top-right. Slide-in from edge, auto-dismiss, hover-to-pause. |
| `examples/phosphor-toast-demo/`                              | Send a notification via `notify-send` and a toast appears. Uses `phosphor-service-notifications` from Phase 2.5. |

**Acceptance:** toasts queue, dismiss correctly; rich text + image support; the per-app-rules consumption seam exists and applies any rules supplied by the rules service. The rules editor and its persistence layer belong to Phase 4.3 (Notification center) and wire into this seam without changes to ToastHost.

**Effort:** M (~2 weeks)

**Phase 3 gate:** All four examples runnable. The connected-corner bar demo is the headline, visually identical to the mockup. Tag `phosphor-ui-primitives-0.1`.

---

## Phase 4: Surface implementations

**Goal:** the actual shell. Each surface is a discrete release.

Order is rough, each surface is independent enough to slip. Recommended sequence below.

### 4.1: Bar (M1 from gap-analysis)

| Deliverable                                                  | Effort                                                                              |
|--------------------------------------------------------------|-------------------------------------------------------------------------------------|
| `qml/Phosphor/Bar/{BarHost,Slot}.qml`                        | M                                                                                   |
| `qml/Phosphor/Bar/Widgets/*`, 10 widgets: Clock, Workspaces, FocusedApp, SystemMetrics, Network, Bluetooth, Audio, Battery, Tray, Media, ControlCenterButton, NotificationButton, PowerButton, Spacer | L |
| Migrate `examples/phosphor-shell/TopPanel.qml` users to new bar | S                                                                                |

Visible win: bar feels alive and distinct.

### 4.2: Launcher (M2)

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Launcher/Launcher.qml` (Spotlight skin first; Connected + Standalone later) | L       |
| Providers: `AppsProvider` (.desktop), `CalculatorProvider`, `WindowsProvider` (foreign-toplevel from `phosphor-compositor`), `CommandProvider`, `EmojiProvider`, `ClipboardProvider` (Phase 2.10) | L |
| Fuzzy match, port `fzf` scoring to C++                                                   | S       |

### 4.3: Notification center (M2)

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Notifications/NotificationCenter.qml` (popout + history + DND + rule editor) | M       |
| Rich text (`markdown2html` port)                                                           | S       |
| Per-app rules editor                                                                       | S       |

### 4.4: Control center (M3)

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/ControlCenter/ControlCenter.qml` + `Tile.qml` + `DetailPanel.qml`           | M       |
| Tile catalog: Network / Bluetooth / Audio / Brightness / NightMode / DarkMode / Airplane / Idle / PowerProfile / Wallpaper (each ~1-2 days, depend on Phase 2 services) | L |
| Card components (Calendar, Weather, SystemMonitor, Media, Shortcuts), reusable in Dashboard | M |

### 4.5: Lockscreen (M5)

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Lock/{LockSurface,LockClock,LockAuthField,LockMediaCard}.qml`               | L       |
| Wired to `phosphor-service-lock` (Phase 2.9) + ext-session-lock-v1 from compositor        | M       |

### 4.6: Power menu

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Power/PowerMenu.qml` (replaces today's `MenuContent.qml` stub)              | S       |
| Wired to `phosphor-service-session` (Phase 2.10)                                           | S       |

### 4.7: Wallpaper picker UI

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Wallpaper/WallpaperPicker.qml` (gallery + per-monitor + cycling schedule)   | M       |
| Wallpaper transition shaders (port 6 from Noctalia or write 2-3 of our own)               | S       |

### 4.8: Theme browser

| Deliverable                                                                                | Effort  |
|--------------------------------------------------------------------------------------------|---------|
| `qml/Phosphor/Themes/ThemeBrowser.qml` (stock themes + custom JSON + matugen)             | M       |
| `qml/Phosphor/Themes/ColorPickerModal.qml`                                                | S       |

**Phase 4 gate:** Phosphor is daily-drivable for one engineer (me, you). Tag `phosphor-shell-0.1`. Replace `examples/phosphor-shell/` with the production shell.

**Total Phase 4 effort:** 12-20 weeks depending on parallelism.

---

## Phase 5: Polish + ecosystem

Lower priority, deferred until 4 ships. Open scope.

| Area                                                  | Effort | Why deferred                                                                          |
|-------------------------------------------------------|--------|---------------------------------------------------------------------------------------|
| Matugen template fan-out (~30 templates)              | M      | Phosphor's theme already retints from matugen; templates extend reach to terminals/editors/external apps. Templates are mostly mustache files, batchable. |
| Plugin browser UI + sandboxing                        | L      | Plugin ABI ships in Phase 1.3 unsandboxed; sandboxing needs capability runtime first   |
| Settings app (separate process)                       | L      | Built on the existing settings-window pattern; not in shell scope                      |
| Dashboard (DankDash-style multi-tab)                  | M      | Optional second surface, see `03-component-map.md` open questions                     |
| Dock                                                  | M      | Optional                                                                               |
| Color picker (Hyprpicker-style)                       | S      | Tool                                                                                   |
| Screenshot tool                                       | M      | Tool, region/window/screen via screencopy                                             |
| Clipboard manager UI                                  | M      | Service exists Phase 2.10; UI is the polish                                            |
| Keybinds cheatsheet                                   | S      | Reads from compositor shortcut registry                                                |
| Notepad widget                                        | S      |                                                                                        |
| Process list                                          | M      |                                                                                        |
| Cava visualizer widget                                | S      |                                                                                        |
| Weather widget                                        | S      |                                                                                        |
| Greeter                                               | L      | Separate process; reuses lockscreen primitives, see open questions in 03              |

---

## What we explicitly DON'T do during this period

(Anti-goals, kept here so scope creep is obvious.)

- **Don't port end-4's novelty widgets** (AI chat, OCR translator, anime sidebar), out of shell scope; let them be plugins later.
- **Don't ship a CUPS printer manager**, standalone tool, not shell concern.
- **Don't reimplement matugen**, call the external binary like DMS does.
- **Don't fork Qt or QtQuick.** We're not building on Quickshell or any other external shell framework. We are the framework, in `phosphor-*` libs. Engine-level fixes go through Qt upstream or land as a workaround inside the relevant `phosphor-*` lib. Noctalia's `noctalia-qs` Quickshell fork is the cautionary tale. It's a tale about a *different* project's stack. Our analogous risk is forking Qt itself, which we likewise refuse.
- **Don't build a marketplace** for plugins. Local discovery lands in Phase 5. Remote registry is deferred indefinitely. The supply-chain risk is documented in `01-feature-inventory.md` anti-patterns.
- **Don't add backwards-compat shims** for the bar's TopPanel POC, replace cleanly when 4.1 lands (per `feedback_no_legacy_shims`).
- **Don't introduce ad-hoc settings migrations**, only the one migration function per real schema bump (per CLAUDE.md "No Ad-Hoc Backwards Compatibility").
- **Don't optimize before measuring.** No premature animation tuning, no premature shader compilation pipeline, no premature plugin sandboxing.

---

## Keeping this plan honest

- Every phase ships a tagged release. The release is the gate, not subjective "done."
- Each `examples/*` stays green forever. Build them in CI from day one.
- When a Phase 4 surface lands, delete the relevant row from `02-gap-analysis.md` (or mark it `(shipped)`).
- When a Phase 1 lib changes its public API, update `03-component-map.md` in the same PR. Don't let the design doc rot.
- The mockups in `mockups/` are reference, not contract. If the lived design deviates, update them or note the deviation in the README, don't pretend they're current if they're not.
- Re-read this plan every ~4 weeks. If a phase is taking 2× the estimate, ask whether the *phase* is wrong (not just the estimate).

---

## TL;DR

- **Phase 1** (4-6 wks): theme, popout, registry, ipc, perscreen, all with example apps. **No shell users yet.**
- **Phase 2** (8-12 wks): 10 new service libs (plus 4 extracted from the `phosphor-services` umbrella in 2.0), each driven by a CLI demo via `phosphorctl`. **Still no shell UI.**
- **Phase 3** (4-6 wks): Pz* atoms, BarCanvas, OSD, Toast, toy windows demonstrating each. **First visible Phosphor pixels.**
- **Phase 4** (12-20 wks): the real shell, bar, launcher, notifications, control center, lockscreen, power menu, wallpaper, theme browser. **Daily-drivable.**
- **Phase 5** (open): matugen fan-out, plugin browser, dashboard, all the smaller surfaces.

Earn each surface by completing its libraries first. Each library earns its place by being driveable from a 100-line example. Each example earns its place by being a runnable acceptance test, not a slideshow.
