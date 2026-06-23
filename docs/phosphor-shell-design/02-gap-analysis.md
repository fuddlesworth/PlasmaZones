<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 02: Gap Analysis (Phosphor vs. Reference Shells)

This doc is a prioritized roadmap. Each row tells you: what's missing, why it matters, what the reference shells call it, what we'd build, rough effort, and which surface(s) it unlocks.

**Effort scale** (single agent, focused work):
- **S** = days
- **M** = 1-2 weeks
- **L** = 3-6 weeks
- **XL** = sprint-scale (6+ weeks)

**Priority** reflects unlock value, what blocks the rest:
- **P0** = required for "not embarrassing"; nothing else lands without it
- **P1** = required for "competitive"
- **P2** = polish / differentiator
- **P3** = nice-to-have

## What we already have, and what's load-bearing about it

| Asset                                                   | Why it matters going forward                                                                                       |
|---------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| `libs/phosphor-shell` (PanelWindow / PopupWindow / FloatingWindow / ShellEngine / ScreenModel / LazyLoader / PersistentProperties) | Core shell primitives in place; foundation is solid.                                                                |
| `phosphor-config` (`ISettings`, `IConfigBackend`)       | Already DI-friendly per-domain, exactly the shape DMS's god-singleton lacks.                                       |
| `phosphor-layer` + our own layer-shell implementation   | Lockscreen, OSDs, overlays all built on this. We're the compositor; we expose the protocol rather than consume it.   |
| `phosphor-shaders` + 4 GLSL primitives                  | Foundation for blur / shadow / wallpaper transitions; we don't need to rebuild what's already here.                 |
| `phosphor-zones`, `phosphor-tile-engine`, `phosphor-snap-engine` | Tiling differentiator. None of the reference shells own this. Treat as a *win*, not a side-quest.            |
| `phosphor-layout-api` (`ILayoutSourceFactory`)          | Plugin / registry seam to generalize to bar widgets, control-center tiles, launcher providers, OSDs.                |
| `WallpaperService`, `UPowerHost`, `Process`, `FileView`, `SystemClock` | Data-source layer is in place; add NM / BlueZ / PipeWire / brightness in the same style.                  |

## Foundational gaps (must land before anything else)

| # | Gap                                  | What it is                                                                                                | Reference impl                                                                  | Build sketch                                                                                                     | Effort | Priority |
|---|--------------------------------------|-----------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------|--------|----------|
| 1 | **Theme token singleton**            | M3 color + typography + spacing + radius + motion tokens, hot-reloaded from JSON, bound by every widget   | DMS `Common/Theme.qml` + `Appearance.qml`; Noctalia `Commons/Style.qml`         | New `qml/Phosphor/Theme/` module: `Theme.qml`, `Tokens.qml`, `Motion.qml`. Tokens fed by `IThemeService` (C++) reading a JSON file via `FileView`. Use **upstream M3 token names** verbatim. | M      | **P0**   |
| 2 | **(shipped, PR #535) PopoutService** | One service owns lifetime / focus / screen affinity / exclusivity arbitration / animations for popouts. (Exclusivity here is the cooperative / modal / detached arbitration model, not wlr-layer-shell `exclusive_zone` routing.) | DMS `Services/PopoutService.qml` (25 KB)                                        | `phosphor-popout`: `IPopoutService`, `PopoutController`, `PopoutHost.qml`. Supersedes today's `PanelPopupHost` (migration in Phase 4.1).    | M      | ~~**P0**~~   |
| 3 | **Widget / provider registries**     | `IBarWidgetFactory`, `IControlCenterTileFactory`, `ILauncherProviderFactory`, `IOSDFactory`, `IDesktopWidgetFactory` | Noctalia `BarWidgetRegistry` family                                  | Generalize `ILayoutSourceFactory` pattern from `phosphor-layout-api`. One header per seam; C++ register-by-name. | M      | **P0**   |
| 4 | **Typed IPC + `phosphorctl` CLI**    | Compositor keybinds invoke shell actions by verb, not D-Bus introspection                                 | DMS `dms ipc call`, Noctalia `noctalia-shell ipc call`, QS `IpcHandler`         | `phosphor-ipc` lib: `IpcRouter`, `IpcTarget` (QML attached property), `IpcSchema` (typed). Ship `phosphorctl` Go binary. Wraps existing D-Bus adaptors. | M      | **P0**   |
| 5 | **`PerScreen` declarative helper**   | Per-monitor surface lifecycle as a one-binding QML primitive (hotplug correctness for free)               | (Phosphor-native)                                                                | `phosphor-shell` adds `PerScreen.qml` QML helper backed by `ScreenModel`. One delegate instance per screen, lifecycle managed by `ScreenModel` add/remove signals. | S | **P0** |
| 6 | **Service layer expansion**          | C++ services for NetworkManager, BlueZ, PipeWire, brightness, idle, polkit, notifications, MPRIS, tray host | DMS `Services/` (~55 singletons); Noctalia `Services/`                          | One `phosphor-<domain>` library per service (per Phase 2 of `04-implementation-plan.md`). Native C++ against `libnm` / `libbluez` / `libpipewire` / DBus. Brightness reads `/sys/class/backlight`. The freedesktop notification spec runs in-process. SNI host runs in-process. Global shortcuts come from the Phosphor compositor's input layer, not an external daemon. | XL (split) | **P0** |

## Surfaces, what we're missing

### Top bar

| Gap                                | Reference                              | Notes                                                                                              | Effort | Priority |
|------------------------------------|----------------------------------------|----------------------------------------------------------------------------------------------------|--------|----------|
| Connected-corner bar canvas        | DMS `BarCanvas.qml` + `ConnectorGeometry.js` + `Widgets/ConnectedCorner.qml` | Differentiator. QML `Shape` + JS geometry; popouts grow out of the bar as one painted surface.   | M      | **P1**   |
| Workspaces widget (multi-WM)       | Noctalia `Workspace.qml` + per-WM services | Already on plan via `CompositorService` polymorphism.                                          | M      | **P0**   |
| Focused-app widget                 | DMS `FocusedApp.qml`, HyprPanel `windowtitle` |                                                                                              | S      | **P1**   |
| System metric widgets (CPU/RAM/disk/net/gpu/temp) | DMS `Cpu`/`Ram`/`GpuTemperature` widgets | We have CPU/mem readouts; expose as a widget catalog driven by `IBarWidgetFactory`.    | M      | **P1**   |
| Network / Bluetooth / Audio / Battery widgets | Noctalia + DMS                | Depends on service layer (foundational gap #6).                                                    | M each | **P1**   |
| Tray widget (SNI host)             | `org.kde.StatusNotifierItem` spec      | Already partly via `TrayMenuPopup`; promote to a bar widget + a popout. Host implemented in-process. | S    | **P1**   |
| Media widget                       | DMS `Media`, Noctalia `MediaMini`      | Extend existing `MprisWidget`.                                                                     | S      | **P1**   |
| Privacy indicator (mic/cam)        | DMS `PrivacyIndicator`, Noctalia `PrivacyService` | Listens to PipeWire active-record nodes.                                                     | S      | **P2**   |
| Idle inhibitor toggle              | DMS `IdleInhibitor`, Noctalia `KeepAwake` |                                                                                                 | S      | **P2**   |
| Cava / audio visualizer widget     | DMS Cava, Noctalia `AudioVisualizer.qml` + `wave_spectrum.frag` | We already have shader infra.                                                | S      | **P3**   |
| Weather widget                     | Noctalia animated, DMS `WeatherService.qml` |                                                                                                | S      | **P3**   |

### Launcher

| Gap                          | Reference                                                                                              | Notes                                                                                            | Effort | Priority |
|------------------------------|--------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------|--------|----------|
| Spotlight-style runner       | DMS `DankLauncherV2`, Noctalia `Modules/Panels/Launcher/`                                              | Three skins (centered, bar-connected, fullscreen) is DMS's pattern, worth copying.              | L      | **P0**   |
| Provider plugin model        | Noctalia `LauncherProviderRegistry` + `Providers/` (Apps/Calc/Clipboard/Command/Emoji/Session/Settings/Windows) | Use `ILauncherProviderFactory` seam.                                                  | M      | **P0**   |
| Fuzzy match                  | both use bespoke `fzf.js` / `FuzzySort.qml`                                                            | Port `fzf` algorithm or use a small C++ port.                                                    | S      | **P1**   |
| App index                    | freedesktop `.desktop` files                                                                            | Parse `.desktop` files directly in `phosphor-launcher-apps` lib. DMS shells out; we shouldn't. | S | **P0** |
| Window switcher provider     | Noctalia `Windows.qml`                                                                                 | Drive via our own `wlr-foreign-toplevel-management-v1` server impl.                              | S      | **P1**   |
| Clipboard provider           | both                                                                                                   | Needs clipboard manager service.                                                                 | S      | **P2**   |

### Notifications

| Gap                                | Reference                                                          | Notes                                                                          | Effort | Priority |
|------------------------------------|--------------------------------------------------------------------|--------------------------------------------------------------------------------|--------|----------|
| Freedesktop notification daemon    | DMS `NotificationService.qml` (43 KB), Noctalia `NotificationService.qml` | Implement org.freedesktop.Notifications. C++ service.                    | L      | **P0**   |
| Toast surface (per-monitor)        | DMS `Notifications/Popup`, Noctalia `Modules/Toast/`               |                                                                                | M      | **P0**   |
| Notification center / history      | both                                                               | History panel in a popout; group by app.                                       | M      | **P1**   |
| Per-app rules / DND                | Noctalia `NotificationRulesService.qml`, DMS in Settings           |                                                                                | S      | **P1**   |
| Rich text / markdown               | DMS `markdown2html.js`                                             | Port or vendor.                                                                | S      | **P2**   |

### Control center / dashboard

| Gap                                | Reference                                                                | Notes                                                                          | Effort | Priority |
|------------------------------------|--------------------------------------------------------------------------|--------------------------------------------------------------------------------|--------|----------|
| Control Center popout              | DMS `ControlCenterPopout.qml` (expandable detail tiles)                  | Tiles registered via `IControlCenterTileFactory`.                              | L      | **P0**   |
| Tile catalog                       | DMS + Noctalia: network, bluetooth, audio, brightness, night-mode, darkmode, airplane, idle, power profile, wallpaper picker | Most depend on services landing first.                            | M      | **P1**   |
| Cards (calendar / weather / system stats / media / shortcuts) | Noctalia `Modules/Cards/`                     | Reuse `IDesktopWidgetFactory`.                                                 | M      | **P2**   |
| Dashboard tab view                 | DMS `DankDash/` (Overview/Wallpaper/Weather/Media)                       | Optional second surface; could fold into Control Center.                       | M      | **P2**   |

### Lockscreen

| Gap                                | Reference                                              | Notes                                                                                                 | Effort | Priority |
|------------------------------------|--------------------------------------------------------|-------------------------------------------------------------------------------------------------------|--------|----------|
| Lock surface (ext-session-lock-v1) | DMS `Modules/Lock/`, Noctalia `Modules/LockScreen/`    | The Phosphor compositor implements `ext-session-lock-v1`. PAM auth via `pam_authenticate` in `phosphor-service-lock`. | L | **P0** |
| Theming integration                | both                                                   | Same `Theme` tokens, blurred wallpaper, media card.                                                   | S      | **P1**   |
| Video screensaver                  | DMS `VideoScreensaver.qml`                             | Plays during idle-but-not-yet-DPMS.                                                                   | S      | **P3**   |
| On-screen keyboard (lock-only)     | DMS lockscreen OSK                                     | Touch convertibles only.                                                                              | M      | **P3**   |

### OSDs

| Gap                                | Reference                  | Notes                                                                       | Effort | Priority |
|------------------------------------|----------------------------|-----------------------------------------------------------------------------|--------|----------|
| OSD framework                      | DMS `Modules/OSD/`         | Single layer-shell surface per screen; `IOSDFactory` registry.              | M      | **P0**   |
| Volume / mic / brightness / caps-lock / num-lock OSDs | both                | Once framework + services land, each OSD is small.                          | S each | **P1**   |
| Idle inhibitor / power profile OSD | DMS                        |                                                                             | S      | **P2**   |

### Theming + wallpaper

| Gap                                | Reference                                                                       | Notes                                                                                              | Effort | Priority |
|------------------------------------|---------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------|--------|----------|
| Matugen integration                | DMS `dms` Go CLI shells out to matugen; Noctalia own impl                       | **(shipped, PR #534)** `MatugenRunner` in `libs/phosphor-theme` wraps the subprocess, handles v3 + v4+ JSON shapes, always passes `--prefer`. `phosphor-theme-cli set-wallpaper` and the demo's wallpaper button drive it end-to-end. | M | ~~**P0**~~ |
| Token store + design-token QML module | DMS `Common/Theme.qml`                                                       | **(shipped, PR #534)** `PaletteStore` + `Phosphor.Theme` singletons (`Theme` / `Tokens` / `Motion` / `StateLayer`). Hot-reloads from disk, bindings track `palette[...]` for live retint. | M | ~~**P0**~~ |
| Template engine                    | DMS uses `mustache`                                                             | **(shipped, PR #534)** `TemplateEngine` renders `{{token[.field]}}` with hex / hexa / r / g / b / alpha / rgb / rgba variants. Drive via `phosphor-theme-cli render-template`. Templates themselves still pending, see next row. | S | ~~**P0**~~ |
| Template fan-out                   | DMS `quickshell/matugen/templates/` (~30 templates)                             | Engine is in place; **templates still pending**, Phase 5 work. Mirror DMS's set: phosphor-theme JSON, qt6ct, gtk-css, kitty/foot/ghostty/alacritty/wezterm, firefox/zen userchrome, vesktop, vscode, neovim, emacs, zed. | M | **P1** |
| Wallpaper picker UI                | DMS gallery + cycling, Noctalia Wallhaven search                                | We have `WallpaperService` (C++), needs the QML face. Phase 4.7 in the plan.                     | M      | **P1**   |
| Theme browser                      | DMS `ThemeBrowser.qml` + stock JSON themes + custom JSON                        | Community themes shareable as JSON files. Phase 4.8.                                              | M      | **P1**   |
| Light/dark switch                  | DMS `Theme.setLightMode()` w/ screen-wipe; Noctalia `DarkModeService` sunrise   | Use `KSunPath` or local sunrise calc; transition via existing shader. `MatugenRunner.mode` already exposes the toggle at the data layer. | S | **P2** |
| Wallpaper transitions              | Noctalia 6 fragment shaders (disc/fade/honeycomb/pixelate/stripes/wipe)        | Trivial given `phosphor-shaders`.                                                                  | S      | **P3**   |

### Plugin model

| Gap                                | Reference                                       | Notes                                                                                              | Effort | Priority |
|------------------------------------|-------------------------------------------------|----------------------------------------------------------------------------------------------------|--------|----------|
| Plugin manifest + versioned ABI    | DMS `plugin-schema.json`, Noctalia `manifest.json` | C++/QML hybrid. Compiled C++ plugins use a Phosphor-native loader built on Qt's `QPluginLoader` with our own metadata schema. QML-only plugins ship as signed bundles. | L | **P1** |
| Capability model                   | none of them have it                            | Differentiator. Define per-plugin permissions: `bar.widget`, `popout.surface`, `network.read`, `network.write`, `notify.send`, `clipboard.read`, etc. | M | **P1** |
| Plugin browser UI                  | DMS `Modules/Settings/Plugins`, Noctalia browser | Local discovery first; remote registry behind a flag.                                              | M      | **P2**   |

### Smaller surfaces

| Gap                              | Reference                                          | Notes                                                       | Effort | Priority |
|----------------------------------|----------------------------------------------------|-------------------------------------------------------------|--------|----------|
| Power / session menu             | both                                               | Replace today's Menu popup stub.                            | S      | **P0**   |
| Polkit auth modal                | DMS                                                | C++ polkit-qt6 binding; modal layer-shell window.           | M      | **P1**   |
| Clipboard manager                | both                                               | cliphist backend; viewer modal.                             | M      | **P2**   |
| Color picker                     | both                                               | Hyprpicker-style overlay + `grim`-style screen sampling.    | S      | **P2**   |
| Screenshot tool                  | HyprPanel, end-4                                   | Region/window/screen via existing screencopy plumbing.      | M      | **P2**   |
| Dock                             | both Quickshell shells                             | Optional; toggleable per user.                              | M      | **P3**   |
| Notepad                          | DMS                                                | Easy QML widget on top of `FileView`.                       | S      | **P3**   |
| Process list                     | DMS `Modules/ProcessList`                         | Goes alongside System Monitor card.                         | M      | **P3**   |
| Keybinds cheatsheet              | end-4                                              | Overlay listing the Phosphor compositor's registered global shortcuts. | S      | **P2**   |
| Greeter                          | DMS `DMSGreeter.qml`                              | Separate process; reuses lockscreen primitives.             | L      | **P3**   |
| CUPS printer manager             | DMS `CupsService.qml`                              | Out of scope for shell, leave to standalone CUPS UI.       | n/a    | n/a       |

## Proposed milestones

Ordered for value delivery, not architectural purity. Each milestone is independently testable / shippable.

### M0: Foundations (foundational gaps #1-#5)
Theme tokens, PopoutService, widget registries, IPC + `phosphorctl`, `PerScreen` helper. **No new user-visible surfaces**, but everything after gets built on these. ~4-6 weeks.

*Partial progress as of 2026-05-26: theme tokens shipped (PR #534), PopoutService shipped (PR #535). Widget registries, IPC, and PerScreen helper remain.*

### M1: Bar parity
Connected-corner bar canvas, widget catalog (workspaces / focused-app / clock / metrics / battery / tray / media), `IBarWidgetFactory` working end-to-end. Migrates the current TopPanel to the new model. **Visible win: bar feels alive and distinct from any existing Wayland shell.**

### M2: Launcher + notifications
Spotlight-style launcher with provider plugins (apps / calc / windows). Freedesktop notification daemon + toasts + history popout. **Visible win: usable as a daily driver shell.**

### M3: Control center + OSDs + service layer
Network / Bluetooth / Audio / Brightness / Idle / Polkit services. Control Center popout with tiles. OSDs for volume/mic/brightness/caps. **Visible win: delivers a complete daily-driver desktop on top of the Phosphor compositor, no external shell, network UI, or audio panel required.**

### M4: Theming pipeline (the headline differentiator)
Matugen integration + ~30 templates. Wallpaper picker UI. Theme browser. **Visible win: drop a wallpaper → every themed surface, Phosphor shell, GTK apps, Qt6 apps, terminals, editors, retints in a second.**

*Partial progress as of 2026-05-26: the token store, matugen runner, template engine, and demo are shipped (PR #534). Wallpaper picker UI, theme browser, and the ~30 templates themselves remain.*

### M5: Lockscreen + dashboard + polish
Lockscreen via ext-session-lock. DankDash-style multi-tab dashboard. Color picker, screenshot, clipboard manager.

### M6+: Plugin browser, dock, novelty widgets
Once the plugin ABI is stable.

## What we *won't* copy

- DMS's `Common/SettingsData.qml` god-singleton, we keep our per-domain `ISettings` split. (See `project_settings_page_controllers`.)
- Noctalia's 40-step `Migration27..59.qml` chain, we keep one migration function per real schema bump per `CLAUDE.md` and `feedback_no_legacy_shims`.
- Noctalia's `noctalia-qs` Quickshell fork. We're not Quickshell-based. The lesson is "don't fork your engine to ship features".
- DMS's `Qt.createComponent()` plugin loader + remote registry without sandboxing, capability-scoped plugins from day one.
- DMS's `onSurface_12` opacity-baked token names, use upstream M3 names verbatim, layer state aliases on top.
- HyprPanel's GTK3-on-Wayland stack and AGS dependency chain, we're past this hurdle by being native Qt6.
- end-4's AI chat / OCR sidebars are fun but out of shell scope. Ship them as plugins instead.
