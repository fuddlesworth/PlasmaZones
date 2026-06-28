<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 01: Reference Shell Feature Inventory

Consolidated from the four research agents. The goal is not exhaustive cataloging, it's a shared vocabulary for the gap analysis and component map that follow.

## Stacks at a glance

| Shell             | Toolkit                           | Framework                    | License        | Compositor focus                        |
|-------------------|-----------------------------------|------------------------------|----------------|-----------------------------------------|
| Noctalia          | QML / Qt6                         | Quickshell fork (`noctalia-qs`) | MIT          | Niri, Hyprland, Sway, labwc, Mango      |
| DankMaterialShell | QML / Qt6 + Go helper (`dms`)     | Upstream Quickshell          | (see repo)     | Niri (deepest), Hyprland, Sway, MangoWC, labwc, MiracleWM, dwl |
| HyprPanel         | GTK3 + SCSS + AGS (TypeScript)    | AGS / Astal                  | (archived)     | Hyprland                                |
| end-4 / illogical | QML / Qt6                         | Upstream Quickshell          | (dotfiles)     | Hyprland                                |
| ML4W              | Waybar + rofi + swaync + native Hypr* tools | n/a                | (dotfiles)     | Hyprland                                |
| **Phosphor (us)** | QML / Qt6 + C++                   | **Custom** (`phosphor-shell`)| GPL/LGPL split | **Phosphor** (own compositor); guest mode possible |

Quickshell itself is documented at https://quickshell.org/docs/types/ and source at https://git.outfoxxed.me/outfoxxed/quickshell. It's the closest thing to a *de facto* QML shell framework on Wayland today; ~93 % C++, LGPL 3.0.

## Surface matrix

Legend: ● ships, ○ stub / external tool, — absent. Notable widget / file names in parens.

| Surface                  | Noctalia                                                    | DMS                                                                 | HyprPanel                                               | end-4                                                            | Phosphor today                              |
|--------------------------|-------------------------------------------------------------|---------------------------------------------------------------------|---------------------------------------------------------|------------------------------------------------------------------|---------------------------------------------|
| Top/side bar             | ● `Modules/Bar/` (~30 widgets, 4 edges)                     | ● `Modules/DankBar/BarCanvas.qml` (connected-corner)                | ● single AGS bar (modules per slot)                     | ● top bar + dock                                                 | ● `TopPanel.qml`                            |
| App launcher             | ● `Modules/Panels/Launcher/` (8 providers)                  | ● `Modals/DankLauncherV2/` "Spotlight" (3 skins)                    | ● bar dropdown                                          | ● sidebar launcher                                               |                                            |
| Notification daemon      | ● `NotificationService.qml`                                 | ● `NotificationService.qml` (43 KB)                                 | ● via `astal-notifd`                                    | ● via `astal-notifd`                                             |                                            |
| Notification center      | ● `NotificationHistoryPanel.qml`                            | ● `Modules/Notifications/Center/`                                   | ● `menu:notifications`                                  | ● right sidebar                                                  |                                            |
| Toasts                   | ● `Modules/Toast/`                                          | ● `Modules/Notifications/Popup/`                                    | ●                                                       | ●                                                                |                                            |
| Lockscreen               | ● PAM via QS                                                | ● `Modules/Lock/` + `VideoScreensaver.qml` + OSK                    | ○ external `hyprlock`                                   | ○ external `hyprlock`                                            |                                            |
| OSDs                     | ● vol/bright/mic/lock-keys                                  | ● 7 OSDs (vol/bright/mic/caps/idle/power/media)                     | ●                                                       | ●                                                                |                                            |
| Control center           | ● `Modules/Panels/ControlCenter/` (11 tiles + cards)        | ● `Modules/ControlCenter/ControlCenterPopout.qml` (expandable)      | ● dashboard tile                                        | ● right sidebar                                                  |                                            |
| Dashboard / dash         |  (cards inside CC)                                         | ● `Modules/DankDash/` (Overview/Wallpaper/Weather/Media tabs)       | ● dashboard module                                      | ● left sidebar (AI / anime / OCR)                                |                                            |
| Workspaces widget        | ● `Workspace.qml` + per-WM service                          | ● `WorkspaceSwitcher` + per-WM service                              | ● `workspaces`                                          | ●                                                                |                                            |
| Workspace overview       | ● `Modules/Background/Overview.qml`                         | ● per-compositor                                                    | ●                                                       | ● live previews                                                  | (compositor-side, in phosphor-compositor)   |
| Wallpaper picker UI      | ● Wallhaven search, 6 transition shaders                    | ● gallery + per-monitor + cycling schedule                          | ● in dashboard                                          | ●                                                                |  (have C++ `WallpaperService` only)        |
| Theme / color picker     | ● scheme browser + matugen + color-picker shader            | ● `Modals/DankColorPickerModal.qml` + `ThemeBrowser.qml`            | ●                                                       | ●                                                                |                                            |
| Power / session menu     | ● `SessionMenu.qml`                                         | ● `Modals/PowerMenuModal.qml`                                       | ● `menu:powerdropdown`                                  | ●                                                                | (Menu popup stub)                           |
| Calendar widget          | ● `CalendarService.qml`                                     | ● `OverviewTab.qml` + iCal/Khal sync                                | ● `menu:calendar`                                       | ●                                                                | ● `CalendarContent.qml`                     |
| Media (MPRIS)            | ● `MediaService.qml`, `MediaPlayerPanel.qml`                | ● `MprisController` + `MultimediaService`                           | ● `menu:media`                                          | ●                                                                | ● `MprisWidget.qml`                         |
| System tray (SNI)        | ● `Tray.qml`                                                | ● `SystemTrayBar` + DBusMenu                                        | ● `systray`                                             | ●                                                                | ● `TrayMenuPopup`                           |
| Network (NM)             | ● `NetworkService.qml`                                      | ● `DMSNetworkService.qml` (33 KB) + legacy fallback                 | ● `menu:network`                                        | ●                                                                |                                            |
| Bluetooth                | ● `BluetoothService.qml`                                    | ● `BluetoothService.qml`                                            | ● `menu:bluetooth`                                      | ●                                                                |                                            |
| Audio (PipeWire)         | ● `Audio` service + `Spectrum`                              | ● `AudioService.qml` (35 KB) per-app mixer                          | ● `menu:audio`                                          | ● volume mixer                                                   |                                            |
| Brightness               | ●                                                           | ● `DisplayService.qml` (41 KB, gamma + night mode)                  | ●                                                       | ●                                                                |                                            |
| Battery / power profiles | ● `Battery`, `PowerProfile`                                 | ● `BatteryService` + `PowerProfileOSD`                              | ● `battery`, `menu:energy`                              | ●                                                                | ● UPower (read-only)                        |
| Idle inhibitor / daemon  | ● `IdleService`, `IdleInhibitorService`                     | ● `IdleService.qml`                                                 | ○ `hypridle` external                                   | ●                                                                |                                            |
| Polkit agent             |                                                            | ● `Modals/PolkitAuthModal.qml` + `PolkitService`                    | ○ external `hyprpolkitagent`                            | ○                                                                |                                            |
| Clipboard manager        | ● `ClipboardService` (cliphist)                             | ● `Modals/Clipboard/` (image previews)                              |                                                        | ●                                                                |                                            |
| Color picker             | ● shader-based                                              | ● `DankColorPickerModal`                                            | ●                                                       | ●                                                                |                                            |
| Screenshot tool          | ○ external (grim)                                           | ○ external                                                          | ● snapshot shortcut                                     | ●                                                                |                                            |
| Screen recorder          | ○                                                           | ○                                                                   | ● recorder                                              | ●                                                                |                                            |
| On-screen keyboard       |                                                            | ● lockscreen-only                                                   |                                                        | ● framework widget                                               |                                            |
| Dock                     | ● `Modules/Dock/`                                           | ● `Modules/Dock/`                                                   |                                                        | ● dock                                                           |                                            |
| Desktop widgets layer    | ● `DesktopWidgets/` (clock, weather, viz, system)           | ● `DesktopWidgetLayer.qml`                                          |                                                        | ● parallax wallpaper + clock + weather                           |                                            |
| Notepad widget           |                                                            | ● `Modules/Notepad/` persistent multi-tab                           |                                                        | ●                                                                |                                            |
| Process list             |                                                            | ● `Modules/ProcessList/` (htop-like, `dgop` backend)                |                                                        |                                                                 |                                            |
| Audio visualizer (Cava)  | ● `AudioVisualizer.qml` + `wave_spectrum.frag`              | ● Cava bar widget                                                   | ● `cava`                                                | ●                                                                |                                            |
| Weather                  | ● animated (5 fragment shaders)                             | ● `WeatherService.qml` (31 KB)                                      | ● `weather`                                             | ●                                                                |                                            |
| Greeter                  |                                                            | ● `DMSGreeter.qml` + `Modules/Greetd/`                              |                                                        |                                                                 |                                            |
| Rules UI          |                                                            | ● `Settings/Rules` GUI                                        |                                                        |                                                                 | ○ (`window-rule-refactor-design.md` plan)   |
| Printer manager (CUPS)   |                                                            | ● `CupsService.qml` (26 KB)                                         |                                                        |                                                                 |                                            |
| Tailscale / VPN          | ● `VPNService`                                              | ● Tailscale + `VPNService`                                          |                                                        |                                                                 |                                            |
| System updater UI        | ● `UpdateService.qml`                                       | ● `SystemUpdateService.qml`                                         | ● `updates`                                             |                                                                 |                                            |
| Settings UI              | ● full settings app                                         | ● 35+ tabs + `SettingsSearchService`                                | ● in dashboard                                          | ● settings app                                                   | ● `SettingsWindow.qml` (basic)              |
| Keybinds cheatsheet      | ●                                                           | ● `KeybindsService`                                                 |                                                        | ● popup                                                          |                                            |
| AI chat / OCR (novelty)  |                                                            |                                                                    |                                                        | ● Gemini/Ollama chat, Google OCR translator                      |                                            |
| Anti-flashbang dim       |                                                            |                                                                    |                                                        | ●                                                                |                                            |
| Connected-corner geometry|                                                            | ● `Widgets/ConnectedCorner.qml` + `ConnectorGeometry.js`            |                                                        |                                                                 |                                            |

## Theming pipelines

| Aspect                       | Noctalia                                                                              | DMS                                                                                          | HyprPanel / end-4 / ML4W                                                                  |
|------------------------------|---------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------|
| Palette extraction           | `ColorSchemeService.qml` (own)                                                        | `dms` Go CLI → external **matugen** binary → `dms-colors.json`                               | external **matugen** (some still on `pywal` / `wallust`)                                  |
| Token system                 | `Commons/Style.qml`, `Commons/Color.qml`                                              | `Common/Theme.qml` (M3-flavored + `onSurface_12` opacity aliases, ad-hoc, not pure M3)      | per-rice; templated via matugen                                                            |
| Hot reload                   | property bindings; `FileView` watchers                                                | `FileView { watchChanges: true }` + QML binding rebroadcast                                  | `hyprctl reload` + SIGUSR per service                                                      |
| Templates for external apps  | `Services/Theming/TemplateProcessor.qml` + `TemplateRegistry.qml` (Mustache)          | ~30 matugen templates: GTK CSS, **kcolorscheme.colors**, qtct, kitty/foot/ghostty/alacritty/wezterm, firefox-userchrome, vesktop, vscode (extension `dms-theme.vsix`), emacs, zed, neovim, dgop | matugen templates: hyprland, waybar, kitty, rofi, btop, cava, swaync, wlogout, yazi, GTK, Qt |
| Light / dark switch          | `DarkModeService` (manual + sunrise/sunset via `LocationService`)                     | `Theme.setLightMode()` w/ screen-wipe transition, debounced                                  | matugen mode flag                                                                          |
| Per-monitor                  | `MonitorsSubTab` (scale, theme)                                                       | per-monitor wallpaper, palette currently session-wide                                        | per-monitor wallpaper via `swww`                                                           |
| Community theme sharing      | `SchemeDownloader.qml` + remote registry                                              | stock JSON themes + custom JSON schema (`docs/CUSTOM_THEMES.md`)                             | informal (dotfile repos)                                                                   |

## Architecture summary

### Quickshell-based shells share a shape

```
shell.qml
└── Variants { model: Quickshell.screens }            ← per-monitor PanelWindow lifecycle
    └── PanelWindow { screen: modelData }
        └── Bar / Notifications / Lock / Wallpaper modules
Common/Theme.qml                                       ← color tokens (M3-ish)
Services/                                              ← singletons wrapping Quickshell services
  Audio, Battery, Bluetooth, Network, Notifications,
  MPRIS, Compositor (per-WM dispatch), Wallpaper, ...
```

Key Quickshell primitives the reference shells lean on (see https://quickshell.org/docs/types/):

- `PanelWindow` (wlr-layer-shell wrapper), `PopupWindow`, `FloatingWindow`
- `Variants` (per-screen instantiation), `LazyLoader`, `PersistentProperties`, `Scope`, `Region`
- `Quickshell.Io`: `Process`, `FileView` + `JsonAdapter`, `DataStream*`, `IpcHandler`, `Socket`/`SocketServer`
- `Quickshell.Services.{Pipewire, Mpris, UPower, Notifications, SystemTray, Pam, Greetd}`
- `Quickshell.Wayland`: `WlrLayershell`, `WlSessionLock`/`WlSessionLockSurface`, `ScreencopyView`, `ToplevelManager`
- Compositor modules: `Quickshell.Hyprland` (`HyprlandWorkspace`, `HyprlandMonitor`, `GlobalShortcut`, `HyprlandFocusGrab`, `HyprlandEvent`), `Quickshell.I3`

Quickshell deliberately doesn't ship `NetworkManager`, `BlueZ`, brightness, or idle daemons, configs shell out via `Process` to `nmcli` / `bluetoothctl` / `brightnessctl` / `swayidle`. **Phosphor can do better because we own the compositor and write native C++ services** against `libnm` / `libbluez` / `libpipewire` / DBus directly, register globals through our own input layer instead of an external shortcut daemon, and host StatusNotifierItem in-process. KF6 libraries (Solid for hardware enumeration, the freedesktop notification spec) are optional dependencies we may pull in where they save work, not architectural commitments.

### IPC patterns (worth lifting verbatim)

```
Compositor keybind ──► CLI binary ──► UNIX socket / D-Bus ──► QML IpcHandler { target: "launcher" }
                                                                                ↓
                                                                  function toggle() { ... }
```

DMS wraps this with a Go CLI `dms ipc call <target> <fn>` (~25 targets, spotlight, audio, brightness, wallpaper, theme, dash, notepad, settings, processlist, powermenu, control-center, color-picker, hypr, …). Noctalia wraps it with `noctalia-shell ipc call <target> <fn>`. Phosphor should ship `phosphorctl call <target> <fn>` with a typed schema.

### Plugin / widget registries

- Noctalia: `BarWidgetRegistry`, `ControlCenterWidgetRegistry`, `DesktopWidgetRegistry`, `LauncherProviderRegistry`, plus a `PluginRegistry` with 7 extension points and a remote source registry (`SourcesSubTab.qml`).
- DMS: `PluginService.qml` (30 KB) plus `PLUGINS/plugin-schema.json`. Types are `widget`, `daemon`, `launcher`, `desktop`, `control-center`, and `control-center-detail`. **No sandboxing**. Plugins get full shell privileges. Browsable via `dms plugins search`.
- HyprPanel / end-4: no real plugin API. "Custom modules" are user-edited TS or QML files in the rice tree. Every rice is a hard fork.

## Three things to steal (cross-shell verdict)

1. **DMS's matugen template fan-out**, including `kcolorscheme.colors` (so KF6 apps a user happens to install retint with everything else). Tooling: `quickshell/matugen/templates/` is the model.
2. **DMS's `PopoutService.qml` (25 KB)**, central focus/exclusive-zone/animation arbitration for transient surfaces. Avoids the ad-hoc Loader spaghetti that bit every other shell.
3. **Noctalia's widget registries**, five seams, one pattern, plugin-friendly without recompiling. Slots straight into a Phosphor-native C++ plugin loader.

## Three anti-patterns to avoid

1. **DMS's `Common/SettingsData.qml`**, a 121 KB QML singleton holding every setting. Merge-conflict magnet, binding cost scales with consumers. Keep our per-domain `ISettings` split (matches `project_settings_page_controllers` memory and the SettingsController breakup we already did).
2. **Noctalia's `Migration27..59.qml`**, one file per release, never consolidated. Exactly the rot our `CLAUDE.md` warns about. Keep one migration function per *real* schema bump.
3. **DMS's `Qt.createComponent()` plugin loader plus remote registry** is a supply-chain attractive nuisance. Phosphor's `ILayoutSourceFactory` already implies a compiled or signed surface. Extend that, not a JS free-for-all.
