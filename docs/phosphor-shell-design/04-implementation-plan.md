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

### 2.3: `phosphor-service-bluetooth` *(shipped)*

Shipped on `feat/phase-2.3-bluetooth-service` (milestones 0-9), in the shape of the §2.1 narrative. Backend is pure D-Bus `org.bluez` on the **system bus** via `phosphor-dbus`, with no `libbluetooth` / QtConnectivity dependency (keeps the wire contract direct; unlike 2.2 there is no optional native backend). Namespace `PhosphorServiceBluetooth`, export macro `PHOSPHORSERVICEBLUETOOTH_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Bluetooth 1.0`.

Two structural firsts versus 2.2:

- BlueZ is **ObjectManager-rooted** (`/` → `GetManagedObjects` returning `a{oa{sa{sv}}}`, plus `InterfacesAdded` / `InterfacesRemoved`), not the per-interface add/remove signals NetworkManager exposes.
- This is the first service lib to act as a **D-Bus server**: it *exports* an `org.bluez.Agent1` object that bluez calls back into during pairing. (2.5 notifications is the only other server-side lib.) The agent is implemented directly on the `QObject` (`Q_CLASSINFO` interface + `ExportAllSlots` + `QDBusContext`), NOT via a generated `qt6_add_dbus_adaptor` adaptor: an interactive agent needs `setDelayedReply`, which only applies when the agent object is itself QtDBus's dispatch target; a generated `QDBusAbstractAdaptor` would intercept dispatch and break it. That direct-registration pattern is the precedent for 2.5.

**Design decisions taken up front** (see Unknowns for the rest):

- **ObjectManager support lands in `phosphor-dbus`** (foundation tier), not in the bluetooth lib, so 2.5 (notifications) and 2.10 (session/logind) reuse it rather than re-implementing the `a{oa{sa{sv}}}` walk.
- **Full `org.bluez.Agent1`**: every callback implemented (not a Just-Works-only or minimal agent), with interactive callbacks using delayed D-Bus replies driven by a consumer (CLI now, pairing dialog in Phase 3/4).

**Milestones**

0. **`phosphor-dbus` ObjectManager observer (M, ~3-4 days).** *Foundation prerequisite: lands in `phosphor-dbus`, not the bluetooth lib.* New service-agnostic `PhosphorDBus::ObjectManager`: binds `(connection, service, rootPath)`, issues `GetManagedObjects` async, subscribes to `InterfacesAdded` / `InterfacesRemoved`, and emits raw-map signals `interfacesAdded(QString path, QMap<QString, QVariantMap>)` / `interfacesRemoved(QString path, QStringList)` plus a `ready()` once the initial walk lands. Owns the `a{oa{sa{sv}}}` demarshalling decision once (hand-iterated `QDBusArgument`, as `NetworkConnection` did for `a{sa{sv}}`). Its own smoke test pins the demarshalling + add/remove contract against a synthetic reply. Kept deliberately un-typed so each consumer materialises its own typed objects.
1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-bluetooth/` mirroring the 2.2 shape (`CMakeLists.txt`, `PhosphorServiceBluetoothConfig.cmake.in`, `include/PhosphorServiceBluetooth/`, `src/`, `tests/`, `examples/phosphor-service-bluetooth-cli/`). Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the six existing services. `BUILD_PHOSPHOR_SHELL`-gated.
2. **`BluetoothHost` facade + ObjectManager wiring (M, ~3 days).** Central facade (like `NetworkHost`) built on the milestone-0 observer pointed at `org.bluez` `/`. Materialises typed `BluetoothAdapter` (per `Adapter1`) and `BluetoothDevice` (per `Device1`), parented to the host, vended via `adapterAdded` / `adapterRemoved` + `deviceAdded` / `deviceRemoved` (+ count signals). Inert when bluez is absent. Handles adapter removal cascading to its child devices.
3. **`BluetoothAdapter` + `BluetoothAdapterModel` (M, ~2-3 days).** Properties Address / Name / Alias / Powered / Discoverable / Pairable / Discovering (`Q_PROPERTY`, emit-on-change `setField` pattern). Writes: `setPowered` / `setDiscoverable` via `Properties.Set`; `startDiscovery()` / `stopDiscovery()` (with an optional `SetDiscoveryFilter`). `QAbstractListModel` over the host's adapters.
4. **`BluetoothDevice` + `BluetoothDeviceModel` (M, ~3-4 days).** Properties Address / Name / Alias / Icon / Paired / Trusted / Blocked / Connected / RSSI / UUIDs / adapter-path. (Class / Appearance are not surfaced in this phase.) Writes: `connectDevice()` / `disconnectDevice()` (fire-and-forget `Device1.Connect` / `Disconnect`, named to avoid shadowing `QObject::connect`), `setTrusted` / `setBlocked`. `QAbstractListModel` with an adapter-path filter (mirrors `AccessPointModel`'s device-scoped subscription). Derived display name: Alias → Name → Address.
5. **`Agent1` + `AgentManager1` registration, full agent (L, ~5-6 days).** The load-bearing milestone. `org.bluez.Agent1` implemented directly on a `QObject` (`Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")` + `ExportAllSlots` + `QDBusContext`, not a generated adaptor; see the structural-firsts note above); export on a fixed path, `RegisterAgent(path, "KeyboardDisplay")` + best-effort `RequestDefaultAgent`. Implement every callback: `Release`, `RequestPinCode` → `s`, `DisplayPinCode`, `RequestPasskey` → `u`, `DisplayPasskey`, `RequestConfirmation`, `RequestAuthorization`, `AuthorizeService`, `Cancel`. Interactive callbacks use **delayed D-Bus replies**: the agent emits a Qt request signal (e.g. `confirmationRequested(device, passkey, requestId)`) and a consumer calls `respondConfirmation(id, accept)` / `respondPasskey(...)` / `rejectRequest(id)`, which sends the held reply or `org.bluez.Error.Rejected` / `Canceled`. Requests are keyed by an id (BlueZ pairs one device at a time, so effectively single in-flight); `Cancel` answers every in-flight request with `Canceled` and BlueZ owns the pairing timeout. `BluetoothDevice::pair()` calls `Device1.Pair()`; agent callbacks fire during the handshake.
6. **QML registration + facade (S, ~1 day).** `BluetoothHost` as `qmlRegisterType` (instantiable, **not** a singleton, following 2.2's `NetworkHost` and the no-singletons direction); both models `qmlRegisterType`; `BluetoothAdapter` / `BluetoothDevice` `qmlRegisterUncreatableType`. Agent request signals surfaced on the host so a Phase-3 pairing dialog can bind them. Idempotent via `std::call_once`.
7. **CLI demo: `examples/phosphor-service-bluetooth-cli/` (M, ~3 days).** phosphorctl-style, system bus, sysexits codes (0 ok / 64 usage / 1 runtime). Subcommands covering the gate: `status`, `list-adapters`, `list-devices [adapter]`, `power <on|off>`, `scan [secs]`, `pair <address>`, `connect <address>`, `disconnect <address>`, `trust` / `untrust`, `remove <address>`. The `pair` flow makes the CLI the interactive responder: prints the passkey and reads y/n for `RequestConfirmation`, prompts for PIN / passkey on the request callbacks.
8. **Tests: smoke + role pinning (S-M, ~1-2 days).** No live bluez on CI. Pin: inert host construction (empty models, no crash), role names + enum integers for both models, adapter / device property surfaces, agent-registration safety with the bus absent, and the agent callback → signal → delayed-reply wiring exercised with a synthetic `QDBusMessage` (deterministic, no daemon). `QSKIP` the daemon-dependent paths. (The milestone-0 ObjectManager smoke test lives in `phosphor-dbus`.)
9. **README + Status (S, half a day).** Match the Phase-2.0 sibling convention; Status leads with `Phase 2.3: shipped.` on gate pass; cross-reference §2.1 for the shared milestone narrative and note the `phosphor-dbus` ObjectManager addition.

**Total effort:** M-L (≈ 3 weeks solo). The table's original **M** assumed a lighter agent; full `Agent1` + the reusable ObjectManager push milestones 0 and 5, which hold most of the wall-clock. Milestones 1, 6, 8, 9 are mechanical given the 2.1 / 2.2 templates.

**Dependencies**

- BlueZ ≥ 5.x runtime (`org.bluez` system service). No build-time `libbluetooth`.
- `phosphor-dbus` (now including the milestone-0 ObjectManager observer). Qt6 ≥ 6.6 Core / Qml / DBus. No QtGui dependency (state is non-visual).
- System-bus access; pairing needs the agent registered (default-agent acquisition is best-effort).

**Risks**

- **Default-agent conflict.** Only one default agent at a time; gnome-bluetooth / `bluetoothctl` may already hold it. Mitigation: always register the agent; `RequestDefaultAgent` is best-effort with a logged warning (pairing still works as a non-default agent).
- **Delayed-reply correctness.** Each interactive callback must reply exactly once (a value, or `Rejected` / `Canceled`); a dropped or double reply hangs bluez's pairing state machine or trips its timeout. Mitigation: requests keyed by id, replied exactly once, with explicit `Cancel` handling; BlueZ owns the pairing timeout.
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

**Out of scope for 2.3.** GATT / profiles (BLE characteristic access), `Battery1` / `MediaControl1` / `MediaPlayer1`, OBEX file transfer, and the pairing **UI** (Phase 3 / 4: the agent only surfaces the request signals a dialog will bind). Multi-adapter selection UI is likewise deferred.

---

### 2.4: `phosphor-service-brightness` *(shipped)*

Shipped on `feat/phase-2.4-brightness-service` (milestones 1-7), in the shape of the §2.1 narrative. A change of shape from 2.2 / 2.3: the read path is **sysfs** (`/sys/class/backlight` + `/sys/class/leds`), not D-Bus, and the privileged write path is logind's `org.freedesktop.login1.Session.SetBrightness(subsystem, name, value)` (writing the sysfs `brightness` attribute directly needs root / udev rules, whereas logind permits the active session to set brightness). Namespace `PhosphorServiceBrightness`, export macro `PHOSPHORSERVICEBRIGHTNESS_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Brightness 1.0`. Smallest service lib so far (effort S); no `ObjectManager` involvement (sysfs has no ObjectManager root).

**Decisions taken up front** (see Unknowns for the rest):

- **Scope = display backlight + keyboard backlight only.** Enumerate `/sys/class/backlight/*` (Display) and `/sys/class/leds/*` filtered to entries whose function segment (after the last `:` in `device:color:function`) is `kbd_backlight` (Keyboard). Every other `/sys/class/leds` entry (capslock / numlock indicators, power / charge / wifi / mute LEDs, RGB notification LEDs, trigger-driven status LEDs) is NOT a user-facing brightness control and is out of scope. A general LED / indicator surface, if ever wanted, is its own concern, not this lib.
- **Writes go through logind, not sysfs.** `SetBrightness` is called with the matching subsystem string (`"backlight"` for display, `"leds"` for the keyboard backlight) so the lib needs no root and no udev rules. With no logind session bound, the write silently no-ops (the lib stays read-only inert rather than attempting a sysfs write it has no permission for).
- **DI for testability.** The host takes an injectable sysfs root (default `/sys`) and an injectable `(QDBusConnection, service)` for logind, so the whole read + write surface is testable against a fake sysfs tree in a temp dir with no root and no real backlight.

**Milestones**

1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-brightness/` mirroring the 2.3 shape (`CMakeLists.txt`, `PhosphorServiceBrightnessConfig.cmake.in`, `include/`, `src/`, `tests/`, `examples/phosphor-service-brightness-cli/`). Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the seven existing services. `BUILD_PHOSPHOR_SHELL`-gated. PUBLIC Qt6 Core / Qml; PUBLIC Qt6 DBus only if the host's logind types surface in a public header (else PRIVATE); `PhosphorDBus` PRIVATE.
2. **`BrightnessDevice` + `BrightnessHost` (M, ~2-3 days).** `BrightnessDevice`: `name`, `kind` (Display / Keyboard), `brightness`, `maxBrightness`, `percentage` (derived), read from the sysfs `brightness` / `max_brightness` attributes; live external changes (another app, hardware keys) tracked via a `QFileSystemWatcher` (or inotify) on the `brightness` attribute. Write: `setBrightness(int)` / `setPercentage(qreal)` routed through logind `SetBrightness(subsystem, name, value)` fire-and-forget; the cached value moves on the next sysfs read triggered by the watcher (no optimistic write). `BrightnessHost`: enumerates `/sys/class/backlight/*` + `/sys/class/leds/*::kbd_backlight` under the injectable sysfs root, vends `BrightnessDevice`s, inert when the root is absent.
3. **`BrightnessDeviceModel` (S, ~1 day).** `QAbstractListModel` over the host's devices (the host-backed model pattern from 2.2 / 2.3). Roles: `device`, `id`, `name`, `kind`, `brightness`, `maxBrightness`, `percentage`.
4. **QML registration (S, ~half day).** `Phosphor.Service.Brightness 1.0`: `BrightnessHost` + `BrightnessDeviceModel` instantiable (`BrightnessHost` a plain type, not a singleton), `BrightnessDevice` uncreatable. `std::call_once`.
5. **CLI demo `examples/phosphor-service-brightness-cli/` (S, ~1 day).** phosphorctl-style, sysexits codes (0 / 64 / 1). Subcommands covering the gate: `list` (devices with kind + brightness / max / percentage), `get <id>`, `set <id> <value>` (raw), `set <id> <pct>%` (percentage). Drives the lib directly.
6. **Tests: smoke + role pinning (S, ~1 day).** A fake sysfs tree in a `QTemporaryDir` (crafted `brightness` / `max_brightness` files for a synthetic backlight + a `*::kbd_backlight` led) makes enumeration, percentage math, kind classification, the `kbd_backlight` filter (and the exclusion of non-keyboard leds), live `QFileSystemWatcher` updates, and model role names deterministic with no root / no daemon. The logind write path is exercised over a session-bus loopback (a fake `org.freedesktop.login1.Session` recording `SetBrightness` calls for both the `backlight` and `leds` subsystems), `QSKIP`'d when no session bus.
7. **README + Status (S, ~half day).** Sibling convention; "Phase 2.4: shipped" on gate pass.

**Total effort:** S (≈ 1 week solo). Milestone 2 holds most of the wall-clock (the sysfs reader + watcher + logind write).

**Dependencies**

- Linux sysfs (`/sys/class/backlight`, `/sys/class/leds`). A running `org.freedesktop.login1` (logind) on the system bus for the write path (the lib loads read-only inert without it).
- `phosphor-dbus` (private link; the logind `SetBrightness` call). Qt6 ≥ 6.6 Core / Qml / DBus. No QtGui.

**Risks**

- **No write permission without logind.** Direct sysfs writes need root / udev rules; the design routes writes through logind exactly to avoid that. With no session, writes no-op (documented), not crash.
- **`actual_brightness` vs `brightness`.** Some drivers lag `brightness` (the request) behind `actual_brightness` (the hardware readback). Read `brightness` for the set-point and expose `actual_brightness` if it diverges; the watcher fires on `brightness` changes.
- **`max_brightness` of zero / missing attributes.** Guard the percentage math against divide-by-zero and absent files (a malformed sysfs entry surfaces as a 0-max device rather than a crash).
- **Watcher churn.** Hardware brightness keys can fire rapid sysfs updates; coalesce (the model emits `dataChanged` per device, not a reset).
- **CI without backlight.** The fake-sysfs-tree fixture makes the read path fully deterministic; the logind write path `QSKIP`s without a session bus.

**Unknowns to resolve before / during implementation**

| # | Question | Recommended resolution |
|---|----------|------------------------|
| U1 | LED scope: all `/sys/class/leds` or keyboard backlight only? | **Resolved: display backlight + `kbd_backlight` leds only.** Filter `/sys/class/leds` to function == `kbd_backlight`; all other leds (indicators, RGB, triggers) are out of scope. |
| U2 | Write path: sysfs directly or logind? | **Resolved: logind `Session.SetBrightness(subsystem, name, value)`.** No root / udev needed; read-only inert with no session. |
| U3 | Percentage curve: linear or perceptual? | **Resolved: linear on the public surface** (`percentage = brightness / maxBrightness`). A perceptual / log curve, if wanted, is a UI-layer helper, matching the 2.1 cubic-volume resolution. |
| U4 | Surface `actual_brightness` separately from `brightness`? | **Resolved: expose `brightness` as the primary value; surface `actual_brightness` only if it diverges** (most drivers keep them equal). Keep the model role list small. |

**Out of scope for 2.4.** General `/sys/class/leds` control (indicators, RGB, triggers), ambient-light auto-brightness, and the brightness **OSD / slider UI** (Phase 3 / 4). (External-display DDC/CI brightness was initially deferred but is now folded in as the 2.4 extension below.)

### 2.4 extension: external-display brightness (DDC/CI) *(shipped)*

The sysfs + logind backend only covers internal panels and keyboard backlights; a desktop with external monitors has no `/sys/class/backlight` entries, so it surfaces zero devices. External-display brightness rides over **DDC/CI on I2C** (VCP feature `0x10`), which logind does not mediate. This extension adds that as a second source inside the same library, decided as follows:

- **libddcutil backend.** Link `libddcutil` (ddcutil 2.x) rather than reimplementing DDC/CI on raw `/dev/i2c-*` or shelling out to the `ddcutil` binary: it owns the I2C transactions, the DDC/CI framing + checksums, retries, timing, and per-monitor quirks. The dependency is **compile-time optional** (detected via pkg-config `ddcutil`); without it the library still builds and simply surfaces no external displays.
- **One unified surface.** External monitors are surfaced as `BrightnessDevice` with a new `kind` of `ExternalDisplay`, alongside the sysfs Display / Keyboard devices, so the shell still binds a single `BrightnessHost` + `BrightnessDeviceModel`.
- **Off-thread I2C.** libddcutil calls are blocking and slow (~50ms+ per op, with retries), so enumeration and get/set run on a dedicated worker `QThread`; results post back to the GUI thread via queued signals. External-display brightness has no change notification, so it is refreshed by polling rather than a watcher.
- **Permissions.** DDC/CI writes need `/dev/i2c-*` access (the `i2c` group + a udev rule); unlike the logind path there is no privileged mediator. The lib degrades to read-only (or no displays) when I2C is inaccessible.

**Milestones**

8. **CMake optional libddcutil detection + `ExternalDisplay` kind (S, ~1 day).** pkg-config `ddcutil` → `PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL`; link when present. Add the `ExternalDisplay` enumerator to `BrightnessDevice::Kind`. The host's internal logind-session gate already excludes it (DDC writes don't use logind).
9. **`DdcController` worker + DDC device path (M, ~3-4 days).** A `DdcController` QObject on a worker `QThread` wrapping libddcutil: enumerate displays, read / set VCP `0x10`, emit per-display results via queued signals. `BrightnessHost` (when `PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL`) creates `ExternalDisplay` `BrightnessDevice`s from the enumeration; the device's `setBrightness` routes to the controller (not logind) and its cached value updates from the controller's read signal. All additive: the sysfs path is untouched.
10. **Tests + CLI + docs (S-M, ~1-2 days).** A deterministic unit test constructs an `ExternalDisplay` `BrightnessDevice` with a recording setter lambda and asserts the route/clamp/read-back/emit path with no I2C or libddcutil; the live `DdcController` + host wiring is hardware-bound, so it is validated manually through the CLI against real monitors rather than in CI. CLI `list` / `get` / `set` work by id across all kinds (the stable first `list` column; `list` shows the `external` kind). README + this section updated to shipped.

**Out of scope (still).** DDC/CI features beyond luminance (contrast, input source, power), and the brightness OSD/UI.

---

### 2.5: `phosphor-service-notifications` *(shipped)*

> Note: milestone 2 (NotificationServer facade + name acquisition + the static
> `GetServerInformation` / `GetCapabilities`) shipped inside the milestone-1
> commit. Name acquisition lives in the same constructor as the adaptor wiring,
> so a milestone-1 server with nothing registered could not be meaningfully
> smoke-tested; the two were built and committed together. The commit series
> therefore runs 1 → 3 → 4.


The server side of `org.freedesktop.Notifications` (Desktop Notifications Spec 1.3) on the **session bus**, via `phosphor-dbus`. Namespace `PhosphorServiceNotifications`, export macro `PHOSPHORSERVICENOTIFICATIONS_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Notifications 1.0`. Table effort **L**: this is one of the larger Phase 2 libs because it owns notification lifecycle (id allocation, expiry, replace, close-reason bookkeeping) and decodes the full hint set, not just a read/write passthrough.

Three structural firsts versus the prior service libs:

- **First name-owning server.** 2.3 (bluetooth) exported an `Agent1` *object* that bluez calls back into, but bluez still owns the service. 2.5 acquires the well-known name `org.freedesktop.Notifications` and *is* the authoritative server for the interface. This is a name-acquisition concern the earlier libs never had: exactly one process may own the name, so a running `dunst` / `mako` / Plasma notification daemon is a hard conflict, not a degraded backend.
- **Generated adaptor, not direct `ExportAllSlots`.** 2.3's agent had to be a direct `QObject` + `ExportAllSlots` because its interactive callbacks need `setDelayedReply`. The Notifications methods (`Notify`, `CloseNotification`, `GetCapabilities`, `GetServerInformation`) all reply **synchronously** (`Notify` allocates an id and returns it immediately), so there is no delayed-reply constraint, and per CLAUDE.md's D-Bus rule ("XML interface files → `qt6_add_dbus_adaptor()`") the server uses a generated adaptor from a checked-in `org.freedesktop.Notifications.xml`. The adaptor forwards to a plain `NotificationServer` QObject.
- **First lib to link Qt::Gui.** Every prior service lib deliberately avoided `Qt::Gui` (state was non-visual). The `image-data` / `icon_data` hint (`(iiibiiay)`: width, height, rowstride, has-alpha, bits-per-sample, channels, pixel bytes) decodes to a `QImage`, which needs `Qt::Gui`. Scoped to image decode only: `QImage`, no widgets, no rendering.

**Decisions taken up front** (see Unknowns for the rest):

- **DI for testability.** `NotificationServer` takes an injectable `(QDBusConnection, serviceName)` defaulting to `sessionBus()` + `org.freedesktop.Notifications`, mirroring 2.3 / 2.4. Tests drive it over a private peer-to-peer `QDBusServer` (or an injected loopback connection) so the full ingest path runs with no real session daemon and no name conflict.
- **Name-conflict policy: inert, with opt-in replace.** `registerService` is attempted once. On failure (another daemon owns the name) the server stays inert (`nameAcquired == false`), logs the conflict, and surfaces an empty model rather than fighting for the name. `ReplaceExisting` is NOT the default; the CLI demo exposes a `--replace` flag that opts into `QDBusConnectionInterface::registerService(..., ReplaceExistingService, DontAllowReplacement)` so taking over from another daemon is an explicit, observable choice (and, once taken, the name is held firmly until exit). This matches the sibling "inert when the backend is unavailable" shape, with conflict standing in for absence.
- **Server owns the full lifecycle.** Id allocation (monotonic, non-zero), `replaces_id` reuse (a Notify with `replaces_id != 0` updates the existing record in place rather than allocating), per-notification expiry timers (the spec's `expire_timeout`: `-1` server-default, `0` never-expire, `>0` explicit ms), and `NotificationClosed` reason codes (`1` expired, `2` dismissed-by-user, `3` closed by `CloseNotification`, `4` undefined) all live in the server, not the consumer.
- **Decode, don't render.** Hints are decoded to typed fields (`urgency` 0/1/2, `category`, `desktop-entry`, `resident`, `transient`, `suppress-sound`, `value`) and `image-data` to a `QImage`; the body's optional markup is stored **raw**. Toast/center rendering, markup-to-HTML, and per-app rules are Phase 3.4 / 4.3, not here.

**Milestones**

1. **Skeleton + CMake plumbing + adaptor (S, ~1 day).** `libs/phosphor-service-notifications/` mirroring the 2.4 shape (`CMakeLists.txt`, `PhosphorServiceNotificationsConfig.cmake.in`, `include/PhosphorServiceNotifications/`, `src/`, `tests/`, `examples/phosphor-service-notifications-cli/`). Check in `src/org.freedesktop.Notifications.xml` and wire `qt6_add_dbus_adaptor` to generate the adaptor against the `NotificationServer` skeleton. Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the eight existing services. `BUILD_PHOSPHOR_SHELL`-gated. PUBLIC Qt6 Core / Qml / DBus / **Gui** (the `QImage` hint surfaces in a public header); `PhosphorDBus` PRIVATE for the hint `QDBusArgument` demarshalling.
2. **`NotificationServer` facade + name acquisition (M, ~2-3 days).** Build the server on the injectable `(connection, service)`. Register the object at `/org/freedesktop/Notifications`, attempt `registerService`, expose `nameAcquired`. Implement the two static methods now: `GetServerInformation` → (`"Phosphor"`, `"phosphor-works"`, version, `"1.3"`) and `GetCapabilities` → the advertised set (`"body"`, `"actions"`, `"icon-static"`, `"persistence"`). `"body-markup"` is deliberately NOT advertised until a renderer exists (advertising it would surface raw markup); see U4. Inert + empty when the name is taken.
3. **`Notify` ingestion + `Notification` object (M, ~3-4 days).** Parse the eight `Notify` args, allocate an id (or honour `replaces_id`), decode hints into typed fields, store the record. The load-bearing piece is the `image-data` / `icon_data` / `image-path` resolution: the `(iiibiiay)` struct demarshals via `QDBusArgument` (the notifications analog of 2.3's nested `a{oa{sa{sv}}}` walk) into a `QImage`, with `image-path` / `app_icon` falling back through icon-theme lookup (reuse `phosphor-service-icontheme`). `Notification` is a parented `QObject` with `Q_PROPERTY`s for every decoded field, vended via `notificationAdded` / `notificationClosed`.
4. **Expiry + close lifecycle (M, ~2-3 days).** Per-notification expiry timer honouring `expire_timeout` semantics. `CloseNotification(id)` plus user-dismiss and replace paths, each emitting `NotificationClosed(id, reason)` with the correct reason code. The consumer-driven side: `invokeAction(id, key)` emits `ActionInvoked(id, key)` and, for Wayland activation, `ActivationToken(id, token)`; these are `Q_INVOKABLE` (CLI now, toast in Phase 3.4), never exported on the bus, the same isolation 2.3 used to keep `respond*` off the `Agent1` interface.
5. **`NotificationModel` (S-M, ~1-2 days).** `QAbstractListModel` over live notifications (host-backed model pattern from 2.2 / 2.3 / 2.4). Roles: `notification`, `id`, `appName`, `appIcon`, `summary`, `body`, `actions`, `urgency`, `category`, `desktopEntry`, `image`, `resident`, `transient`, `expireTimeout`, `timestamp`. Insert / remove on add / close; `dataChanged` on `replaces_id` update (no full reset). Role enum integers + names pinned by smoke test (the 2.0 sni-shell pattern).
6. **QML registration + facade (S, ~1 day).** `Phosphor.Service.Notifications 1.0`: `NotificationServer` + `NotificationModel` instantiable (plain types, NOT singletons, per the no-singletons direction); `Notification` `qmlRegisterUncreatableType`. Action-invoke + activation-token surfaced on the server so a Phase-3 toast can bind them. Idempotent via `std::call_once`.
7. **CLI demo: `examples/phosphor-service-notifications-cli/` (M, ~3 days).** Runs as the actual daemon: acquires the name (or `--replace` to take over), then logs every incoming `Notify` with decoded fields. Subcommands while running / against the live model: `watch` (default: stream incoming notifications), `list`, `close <id>`, `invoke <id> <action>`. `notify-send "hi" "body" -u critical` from a second terminal exercises the full path end to end. sysexits codes (0 / 64 / 1), same shape as the sibling CLIs.
8. **Tests: smoke + role pinning (S-M, ~1-2 days).** No live session daemon on CI. A private peer-to-peer `QDBusServer` (or injected loopback connection) drives `Notify` / `CloseNotification` via synthetic `QDBusMessage`s and asserts: id allocation monotonic + non-zero, `replaces_id` updates in place, expiry fires `NotificationClosed` with reason `1`, explicit close gives reason `3`, `GetCapabilities` / `GetServerInformation` content, `image-data` decode from a crafted `(iiibiiay)` struct, urgency / category / desktop-entry hint decode, model role names + integers, inert construction when the name is unavailable (empty model, no crash, the `modelWithoutHostIsEmpty` shape). `QSKIP` the real-session-bus path.
9. **README + Status (S, ~half day).** Sibling convention (SPDX → summary → Responsibility → Key types → Typical use → Design notes → Dependencies → Status). Status leads with `Phase 2.5: shipped.` on gate pass; cross-reference §2.1 for the shared milestone narrative and §2.3 for the server-object precedent.

**Total effort:** L (≈ 3-4 weeks solo). Milestones 3 and 4 hold most of the wall-clock (hint decode + lifecycle); 1, 6, 8, 9 are mechanical given the 2.3 / 2.4 templates.

**Dependencies**

- A free `org.freedesktop.Notifications` name on the session bus (no other notification daemon running, or `--replace`).
- `phosphor-dbus` (private link; `QDBusArgument` hint demarshalling). `phosphor-service-icontheme` (private link; `app_icon` / `image-path` resolution). Qt6 ≥ 6.6 Core / Qml / DBus / **Gui** (`QImage` only).
- No external native dependency; the entire surface is D-Bus + Qt.

**Risks**

- **Name conflict.** The common case on a real desktop is that something already owns the name. The lib must stay inert (empty model, `nameAcquired == false`), never crash or busy-loop; `--replace` is the deliberate opt-in. Documented, not silent.
- **`image-data` decode correctness.** The `(iiibiiay)` struct is easy to get wrong: rowstride vs width, premultiplied alpha, bits-per-sample, channel count. Decode once in the lib, pin it with a crafted-struct test, expose the result as a `QImage` so no consumer re-derives it.
- **Expiry timer churn.** A burst of notifications creates many short timers; coalesce where possible and ensure the model emits per-row `dataChanged` / removal, not a reset, on each expiry.
- **Qt::Gui creep.** Linking `Qt::Gui` is a first for a service lib; keep it scoped to `QImage` decode. No `QPixmap`, no widgets, no rendering, those belong to Phase 3.4.
- **Markup in body.** The spec allows a limited HTML subset in `body`. Store it raw but do NOT advertise `body-markup` until a renderer exists: advertising it tells apps to send markup that 2.5 would surface as raw tags. Rendering / sanitising is Phase 4.3, not a 2.5 concern.
- **CI without a session bus.** The private-`QDBusServer` fixture makes ingest deterministic; the real-session path `QSKIP`s, same shape as the sibling daemon-dependent tests.

**Unknowns to resolve before / during implementation**

| # | Question | Recommended resolution |
|---|----------|------------------------|
| U1 | Generated adaptor or direct `ExportAllSlots`? | **Resolved: generated `qt6_add_dbus_adaptor` from a checked-in XML.** All four methods reply synchronously, so there is no `setDelayedReply` reason to hand-roll dispatch, and CLAUDE.md mandates XML → adaptor for this case. (2.3's direct export was driven solely by delayed replies.) |
| U2 | Name-conflict policy when another daemon owns the name? | **Resolved: inert by default, opt-in `--replace`.** No forced `ReplaceExisting`; the lib surfaces `nameAcquired == false` and an empty model, the CLI flag makes takeover explicit. |
| U3 | `image-data`: decode to `QImage` in the lib, or pass raw bytes to the UI? | **Resolved: decode in the lib to a `QImage` role.** Every consumer needs it and the decode is spec-fiddly; decode once. The raw hint stays available via a `properties()`-style accessor for advanced bindings, mirroring 2.1's U4 resolution. |
| U4 | `body` markup: render in the lib or store raw? | **Resolved: store raw, do NOT advertise `body-markup` until a renderer exists.** Advertising it without rendering would surface raw markup to users; the capability lands with the Phase 4.3 notification center (`markdown2html` port). The server never renders. |
| U5 | History / persistence across restart? | **Resolved: in-memory only for 2.5.** On-disk history + DND + per-app rules are the Phase 4.3 notification center, which consumes this model. |
| U6 | Is linking `Qt::Gui` acceptable for a service lib? | **Resolved: yes, scoped to `QImage` decode.** No widgets, no rendering. The alternative (raw bytes only) pushes the spec-fiddly decode onto every consumer, which is worse. |

**Out of scope for 2.5.** The toast surface and notification center UI (Phase 3.4 / 4.3), per-app rules + their persistence (Phase 4.3), DND scheduling, actual audio playback for `suppress-sound` (the hint is decoded and surfaced; honouring it is a consumer concern), and markup rendering. The server stores and lifecycles notifications; it draws nothing.

---

### 2.6: `phosphor-service-polkit` *(shipped)*

> Note: milestone 2 (the `Listener` subclass + registration lifecycle) shipped
> inside the milestone-1 commit; the registration plumbing is inseparable from
> the skeleton. The commit series runs 1 → 3 → 4 → 5 → 6 → 7 → 8.


A PolicyKit **authentication agent** via the `polkit-qt6` binding. Namespace `PhosphorServicePolkit`, export macro `PHOSPHORSERVICEPOLKIT_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Polkit 1.0`. Table effort **M**. Unlike the data-source service libs (2.1-2.4), and like 2.5, this one is a callback target: `polkitd` calls into us when an app requests a privileged action, and we drive the PAM conversation that authenticates the user.

Two structural notes versus the prior service libs:

- **Single active request, no model.** polkit serialises authentication: the agent handles one request at a time. So the facade exposes a single *active* `AuthRequest` plus signals, not a `QAbstractListModel` (the notifications-style list shape does not apply). The closest precedent is 2.3's `Agent1` (a callback object with interactive, deferred responses keyed to a request), not 2.5's server-with-a-model.
- **Security-sensitive.** This library handles authentication. It surfaces the request and a `respond(password)` path but never stores, logs, or echoes the password; the password flows straight into `Agent::Session::setResponse`. The plan-table's "accepts a hardcoded password" is strictly a CLI-demo convenience, fenced behind an explicit flag and never in the library.

The `polkit-qt6` API we build on:
- `PolkitQt1::Agent::Listener` (abstract): subclass it, override `initiateAuthentication(actionId, message, iconName, Details, cookie, Identity::List, AsyncResult*)`, `initiateAuthenticationFinish()`, `cancelAuthentication()`; register with `registerListener(subject, objectPath)` for the session `Subject`. The `AsyncResult*` MUST be completed when the flow ends.
- `PolkitQt1::Agent::Session`: drives one PAM conversation. `initiate()` starts it; the `request(prompt, echo)` signal carries each PAM prompt; `setResponse(password)` answers; `completed(gainedAuthorization)` / `showError` / `showInfo` report progress; `cancel()` aborts. Constructed with the chosen `Identity`, the `cookie`, and the `AsyncResult`.

**Decisions taken up front** (see Unknowns for the rest):

- **DI for testability.** The agent takes an injectable `Subject` (default: the current session) and an injectable object path, so registration can be exercised without owning the real session, and the request/response state machine can be driven without a live `polkitd`.
- **Inert on registration failure.** Exactly one agent serves a session; if the desktop's agent (KDE / GNOME / `polkit-gnome`) already holds it, `registerListener` fails and the agent surfaces `registered() == false` and stays inert, mirroring 2.5's name-conflict-is-inert shape. Taking over (stopping the desktop agent) is the operator's choice, surfaced in the CLI demo, not forced.
- **Surface the request, never the secret.** `AuthRequest` carries actionId / message / iconName / details / the chosen identity / the current PAM prompt + echo flag. `respond(password)` and `cancel()` are `Q_INVOKABLE` for a UI; the password is passed straight through and never retained.
- **Link Core + Agent, not Gui.** `PolkitQt6-1::Core` + `::Agent` only; the `::Gui` action-button widgets are a different (Phase 3/4 UI) concern. No Qt Gui type is used, and `PolkitQt6-1::Agent` does not pull `Qt6::Gui` transitively, so Gui is not a dependency.

**Milestones**

1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-polkit/` mirroring the 2.5 shape (`CMakeLists.txt`, `PhosphorServicePolkitConfig.cmake.in`, `include/PhosphorServicePolkit/`, `src/`, `tests/`, `examples/phosphor-service-polkit-cli/`). `find_package(PolkitQt6-1 REQUIRED)`; link `PolkitQt6-1::Core` + `PolkitQt6-1::Agent`. Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the nine existing services. `BUILD_PHOSPHOR_SHELL`-gated.
2. **`PolkitAgent` listener + registration (M, ~2-3 days).** Subclass `Listener`. Build on an injectable `(Subject, objectPath)`; `registerListener` for the session subject, expose `registered`. Implement the three overrides as a skeleton (store the `AsyncResult` + `cookie`, complete immediately or reject) so the lib links and registers; inert + `registered() == false` when another agent owns the session.
3. **`initiateAuthentication` ingestion + typed `AuthRequest` (M, ~2-3 days).** Decode the callback into an `AuthRequest` (actionId, message, iconName, `Details` → `QVariantMap`, cookie, identities → typed list), store the pending `AsyncResult` keyed by cookie (the 2.3 keyed-request precedent), and surface the active request via signals. Identity selection (default: first identity; multi-identity selection is a UI affordance).
4. **`Session` / PAM conversation (M, ~2-3 days).** Wrap `Agent::Session`: `initiate()` on accept; relay `request(prompt, echo)` as a Qt signal; `respond(password)` → `setResponse`; `completed(gained)` → complete the `AsyncResult` + emit result + clear the active request; `cancel()` and `showError` / `showInfo` paths. `cancelAuthentication()` from polkit tears down the in-flight session.
5. **QML facade (S, ~1 day).** `Phosphor.Service.Polkit 1.0`: the agent host instantiable (plain type, not a singleton), `AuthRequest` `qmlRegisterUncreatableType`. `respond` / `cancel` `Q_INVOKABLE`, never exported on any bus (the 2.3 / 2.5 isolation pattern).
6. **CLI demo `examples/phosphor-service-polkit-cli/` (M, ~3 days).** Runs as the standalone agent: registers, logs each incoming request (action, message, identities, PAM prompt), and with an explicit `--password <pw>` (demo only, loudly documented) auto-answers. `pkexec true` (or any polkit action) from another terminal exercises the full path end to end; `--replace`-style guidance when the desktop agent owns the session.
7. **Tests: smoke (S-M, ~1-2 days).** No live `polkitd` on CI. Pin what is deterministic: inert construction (no crash when registration fails / no daemon), the `Details` → `QVariantMap` decode, `AuthRequest` field surfaces and identity selection, and the session state machine where it can be driven without PAM (`request` → `respond` → `completed` wiring via a fake/injected session seam). `QSKIP` the daemon-dependent registration + real-PAM paths.
8. **README + Status (S, ~half day).** Sibling convention; "Phase 2.6: shipped" on gate pass; cross-reference §2.3 for the agent-callback precedent and §2.5 for the inert-on-conflict shape.

**Total effort:** M (≈ 2-3 weeks). Milestones 3-4 (the request decode + the PAM session relay) hold the wall-clock; 1, 5, 7, 8 are mechanical given the 2.5 templates.

**Dependencies**

- `polkit-qt6` ≥ the distro's PolkitQt6-1 (Core + Agent), discovered via `find_package(PolkitQt6-1)`. A running `polkitd` on the system bus for the live path (the lib loads inert without it).
- Qt6 ≥ 6.6 Core / Qml. No `phosphor-dbus` (polkit-qt owns its D-Bus).

**Risks**

- **Agent-conflict.** A desktop session almost always already has an agent (KDE / GNOME). Registration then fails; the lib must stay inert, never crash or busy-loop, and the demo documents stopping the desktop agent to test. The inert path is the common case on a real desktop.
- **AsyncResult lifetime.** Every `initiateAuthentication` MUST eventually complete its `AsyncResult` (success, error, or cancel) or polkit's auth dialog hangs; a dropped or double completion corrupts the flow. Key it by cookie, complete exactly once, with explicit `cancelAuthentication` handling (the 2.3 delayed-reply discipline).
- **Password handling.** The secret must never be retained, logged, or echoed; it flows straight into `setResponse`. The `request(prompt, echo)` echo flag is honoured by the UI, not the lib. Audit this surface specifically.
- **PAM in tests.** Real authentication needs PAM + a daemon, untestable on CI. Keep the testable seam (request decode, state machine) injectable; `QSKIP` the rest, validated manually through the CLI against a real `pkexec`.

**Unknowns to resolve before / during implementation**

| # | Question | Lean |
|---|----------|------|
| U1 | `Subject` for registration: `UnixSessionSubject` from the session id, or from the process pid? | Lean session-id (the agent serves the whole session); injectable so tests pass a synthetic subject. Confirm against a polkit-qt example. |
| U2 | Object path for the exported agent. | A Phosphor-namespaced path (e.g. `/org/phosphor/PolicyKit1/AuthenticationAgent`), constant + injectable. |
| U3 | Multi-identity handling: pick the first, or surface the list for selection? | Surface the list on `AuthRequest`; default-select the first; the Phase-3/4 dialog can offer a chooser. |
| U4 | Also expose the **client** side (`Authority::checkAuthorization`) for the shell to gate its own privileged actions, or agent-only? | Agent-only for 2.6 (the table's scope); a thin `Authority` query wrapper can land later if a consumer needs it. |
| U5 | Registration-failure policy when a desktop agent owns the session: inert, or offer takeover? | Inert by default (cannot force-replace a polkit agent the way a bus name allows); the CLI documents stopping the desktop agent. |

**Out of scope for 2.6.** The authentication **dialog UI** (Phase 3/4: the agent only surfaces the request + prompt a dialog binds), client-side `checkAuthorization` gating (U4), temporary-authorization management, and any credential storage. The agent drives the PAM conversation; it draws nothing and remembers nothing.

### 2.7: `phosphor-service-idle` *(shipped)*

A Wayland idle-management service: it watches the session for inactivity through a configurable multi-stage timeout policy, and it inhibits idle on request. Namespace `PhosphorServiceIdle`, export macro `PHOSPHORSERVICEIDLE_EXPORT`, LGPL-2.1-or-later, QML URI `Phosphor.Service.Idle 1.0`. Table effort **S**.

First **Wayland-client** service lib. Unlike 2.1 (libpipewire), 2.2 / 2.3 / 2.5 (D-Bus), 2.4 (sysfs + logind), and 2.6 (polkit-qt), this one is a pure Wayland client: it consumes `ext-idle-notify-v1` (idle notification) and `zwp-idle-inhibit-v1` (idle inhibition) globals from our own compositor. The closest code precedents are the foundation libs `phosphor-wayland` and `phosphor-layer`, not the prior service libs.

> Backend note: the plan table (row 2.7) names only `idle-inhibit-unstable-v1`, but that protocol covers only the *inhibit* half (and is surface-bound). The *timeout / fired-at* half (the demo's "timeout config, fired-at log") is `ext-idle-notify-v1`. The service binds both.

**What already exists (foundation tier).** `phosphor-wayland` already ships the raw protocol clients this service composes, so 2.7 does **not** re-vendor protocols or re-write `wl_registry` binding:

- `PhosphorWayland::IdleNotifier`: an `ext-idle-notify-v1` consumer with `setTimeout(ms)`, `isIdle()`, and `idled()` / `resumed()` signals. One notifier is one timeout.
- `PhosphorWayland::IdleInhibitor`: a `zwp-idle-inhibit-v1` client bound to a `QWindow` surface; inhibits while that surface is visible.
- Both protocol XMLs (`ext-idle-notify-v1.xml`, `zwp-idle-inhibit-v1.xml`) are vendored under `libs/phosphor-wayland/protocols/` and code-generated there.
- Both are currently registered for QML directly by the shell (`Phosphor.Shell.IdleNotifier` / `IdleInhibitor` in `shellengine.cpp`).

So the service lib is the **policy layer** on top of those single-purpose primitives, not a new protocol binding.

**What the service adds:**

- A **multi-stage idle policy**: several escalating stages (e.g. dim, then lock-hint, then display-off), each backed by an `IdleNotifier` at its own timeout, driven by one coherent state machine that tracks the current stage, advances on each `idled()`, and resets to active on the first `resumed()`. The foundation `IdleNotifier` is single-timeout; the service composes N of them into a monotonic ladder.
- **Programmatic, surface-less inhibition** with reference-counted cookies, so callers ("a video is playing", "a long copy is running") can inhibit idle without owning a visible surface, and inhibition lifts only when the last cookie is released. (See U2 for how surface-less inhibit maps onto the surface-bound `zwp-idle-inhibit-v1`.)
- A QML / CLI facade (`IdleService`) exposing the live state, the configured stages, an inhibit toggle, and a fired-at log.

**Decisions taken up front** (see Unknowns for the rest):

- **Compose the foundation primitives, do not fork them.** Link `PhosphorWayland`; build the policy on `IdleNotifier` / `IdleInhibitor`. No protocol XML in this lib.
- **Single instantiable host, no model.** Like 2.6 (single active request), the service is one `IdleService` host object with the current `state`, the `stages` it monitors, and signals; not a `QAbstractListModel`.
- **DI for testability.** The state machine is driven by an injectable idle-source seam (an interface the real `IdleNotifier` adapter implements and a fake satisfies), so the stage-ladder logic is unit-tested with simulated idle / resume edges and no live compositor.
- **Inert without the globals.** If the compositor advertises neither global (`IdleNotifier::isSupported()` / `IdleInhibitor::isSupported()` false), the host constructs, reports unsupported, and stays inert (the 2.x no-crash-on-missing-backend shape).

**Milestones**

1. **Skeleton + CMake plumbing (S, ~1 day).** `libs/phosphor-service-idle/` mirroring the 2.6 shape (`CMakeLists.txt`, `PhosphorServiceIdleConfig.cmake.in`, `include/PhosphorServiceIdle/`, `src/`, `tests/`, `examples/phosphor-service-idle-cli/`). Link `PhosphorWayland` (PRIVATE) + Qt6 Core / Qml / Gui (Gui for `QWindow` on the inhibitor path). Imperative `qmlregistration.cpp`, `std::call_once`-guarded, called from `src/shell/main.cpp` alongside the ten existing services. `BUILD_PHOSPHOR_SHELL`-gated.
2. **Idle-source seam + stage state machine (S-M, ~1-2 days).** Define `IIdleSource` (timeout setter + `idled` / `resumed` signals) implemented by an adapter over `PhosphorWayland::IdleNotifier`. `IdleStateMachine` owns an ordered list of stages, arms one source per stage, advances the current stage on `idled()`, and resets to active on `resumed()`. Pure logic, fully testable on a fake source.
3. **Inhibition aggregation (S, ~1 day).** Ref-counted `inhibit()` / `release(cookie)` surface; while any cookie is held, idle monitoring is paused (or an inhibitor is armed, per U2). `inhibited` property + change signal.
4. **QML facade + `IdleService` host (S, ~1 day).** `Phosphor.Service.Idle 1.0`: `IdleService` instantiable (plain type, not a singleton); `state` (active / idle-stage-N), `stages` config, `inhibited`, `inhibit()` / `release()` `Q_INVOKABLE`, `idled(stage)` / `resumed()` signals. Resolve coexistence with the shell's existing direct `Phosphor.Shell.IdleNotifier` / `IdleInhibitor` regs (U4).
5. **CLI demo `examples/phosphor-service-idle-cli/` (S, ~1 day).** Configures one or more timeouts from flags, logs each stage fire with a wall-clock timestamp (the "fired-at log"), and offers an inhibit toggle (e.g. a `--inhibit-for <s>` flag or stdin). Run it, stop touching input, watch stages fire; move the mouse, watch it resume.
6. **Tests: smoke + state machine (S, ~1 day).** Unit-test the stage ladder on the fake source (single stage; multi-stage monotonic advance; resume-from-any-stage reset; reconfigure while idle), the inhibit ref-count (nested inhibit / release; release-unknown-cookie no-op), and inert construction when unsupported. `QSKIP` the live-compositor path.
7. **README + Status (S, ~half day).** Sibling convention; "Phase 2.7: shipped" on gate pass; cross-reference `phosphor-wayland` for the underlying primitives and §2.6 for the single-host (no-model) shape.

**Total effort:** S (≈ 1 week). Most of the protocol cost was already paid in `phosphor-wayland`; the new work is the state machine (milestone 2) and the facade. Milestones 1, 4, 5, 7 are mechanical given the 2.6 templates.

**Dependencies**

- `phosphor-wayland` (PRIVATE link; provides `IdleNotifier` / `IdleInhibitor` + the generated protocol code). No new `wayland-protocols` dependency; the XMLs are already vendored in `phosphor-wayland`.
- Qt6 ≥ 6.6 Core / Qml / Gui (Gui for `QWindow` on the surface-bound inhibitor). No `phosphor-dbus` unless the optional `org.freedesktop.ScreenSaver` surface lands (U3).

**Risks**

- **Overlap with the existing shell wiring.** The shell already binds `Phosphor.Shell.IdleNotifier` / `IdleInhibitor` directly. Land 2.7 without breaking that: either the service composes them and the shell migrates to `Phosphor.Service.Idle`, or both coexist with a documented split. Decide before milestone 4 to avoid double idle-monitoring (two notifiers at the same timeout racing).
- **Surface-less inhibit.** `zwp-idle-inhibit-v1` is surface-bound by design; a programmatic, surface-less inhibit has no direct protocol mapping. Options: pause / disarm the notifiers while inhibited (works for our own `ext-idle-notify` monitoring but does not inhibit other clients' idle), or hold a hidden inhibitor surface. Resolve in U2.
- **Single source of truth.** If both the shell and the service monitor idle, the compositor sees two notifications and "idle" can flap. Keep exactly one armed monitor per timeout.
- **No live compositor on CI.** The protocol path needs our compositor; keep the state machine behind the injectable seam and `QSKIP` the live path, validated through the CLI against a running session.

**Unknowns to resolve before / during implementation**

| # | Question | Lean |
|---|----------|------|
| U1 | Stage config shape: a flat `(name, timeoutMs)` list on the host, or richer per-stage actions (dim %, lock, dpms)? | Lean a flat ordered `(name, timeoutMs)` list for 2.7. The action mapping (what *happens* at each stage) is a Phase 3/4 shell-policy concern; the service reports *which* stage fired, the shell decides what to do. |
| U2 | Surface-less inhibition: disarm the notifiers, or hold a hidden inhibitor surface? | Lean disarm-the-monitors for 2.7 (simplest; covers the shell's own idle actions); revisit a real `zwp-idle-inhibit` surface if we need to inhibit *other* clients' idle too. |
| U3 | Expose a D-Bus `org.freedesktop.ScreenSaver` / `org.freedesktop.PowerManagement.Inhibit` server so third-party apps can inhibit idle the standard way? | Out of scope for 2.7 (the table is Wayland-only); a thin `phosphor-dbus` adaptor over the same ref-counted inhibit core can land later, like 2.6's deferred `Authority` client. |
| U4 | Do the shell's existing `Phosphor.Shell.IdleNotifier` / `IdleInhibitor` QML regs stay, or migrate to `Phosphor.Service.Idle`? | Lean: keep the foundation types where a surface-bound inhibitor is genuinely local (a QML window inhibiting its own output), and route session-wide idle *policy* through the service. Document the split so they do not both arm monitors. |
| U5 | Default stage ladder (timeouts) shipped by the lib, or supplied entirely by the shell / settings? | Lean: the lib ships no policy (empty stage list by default); the shell / settings supply timeouts. The service is mechanism, not policy, matching the 2.x "surface state, do not decide" stance. |

**Out of scope for 2.7.** The visual idle actions (dim animation, lock screen, DPMS / display power), any power-management or suspend coupling (that is 2.10 session / logind), and the D-Bus inhibit server (U3). The service times inactivity and reports stage transitions; the shell decides what each stage does.

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
