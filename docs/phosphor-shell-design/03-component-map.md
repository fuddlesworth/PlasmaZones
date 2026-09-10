<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 03: Component Map

The modules the shell is made of and how they connect. Libraries are LGPL and
own no policy; the shell process and its composition root are GPL and own
all of it.

## Layout

```
libs/
  phosphor-theme/            Phosphor.Theme: Theme, Tokens, Motion, StateLayer, Spectrum,
                             PaletteStore, FontFaces, MatugenRunner, TemplateEngine
  phosphor-shell-widgets/    Phosphor.Widgets: SpectrumRail/Stroke/Underline, TabularText,
                             PlacementMiniature (+ MiniatureEdges.js), SettleAnimation,
                             DecorationSlot, PhosphorButton/Slider/TextField/Card/Ripple
  phosphor-shell/            Phosphor.Shell: ShellEngine, PanelWindow, FloatingWindow,
                             PerScreenPanels, PlacementMap (D-Bus model of the engines),
                             Toplevels, Workspaces, WallpaperService
  phosphor-shell-bar/        Phosphor.Bar: BarHost, Slot, PaneTether, MapPane, Widgets/*
  phosphor-shell-control-center/  Phosphor.ControlCenter: ControlCenter, Tile, SliderTile, tiles
  phosphor-shell-launcher/   Phosphor.Launcher + PhosphorShellLauncher (providers, fzf port)
  phosphor-shell-osd/        Phosphor.OSD: OSDHost, OSDCard, Volume/Brightness/Mic/CapsLock
  phosphor-shell-notifications/  Phosphor.Notifications: ToastHost, Toast
  phosphor-shell-power/      Phosphor.Power: the word column
  phosphor-shell-lock/       Phosphor.Lock: LockScreen, LockController, LockAuthField, LockRegion.js
  phosphor-shell-dashboard/  Phosphor.Dashboard: Dashboard, Cheatsheet, FullMap, cells, ChordLayout.js
  phosphor-shell-picker/     Phosphor.Picker: Picker, WallpaperSurface, WallpaperCandidates,
                             RetintController, ThemePresets
  phosphor-shell-polkit/     Phosphor.Polkit: PolkitPrompt, PolkitAnchor, PolkitDim
  phosphor-popout/           PopoutController, transports, PaneHost, PaneTether
  phosphor-shell-patterns/   Layer roles (Wallpaper, Hud, Modal, Floating, plus Panel(edge) and Toast(corner))
  phosphor-layer/ phosphor-surfaces/ phosphor-wayland/
                             Layer-shell and session-lock surfaces, the QPA plugin
  phosphor-ipc/ phosphor-registry/
                             IpcTarget + phosphorctl; typed factory registries
  phosphor-service-*/        sni, upower, mpris, pipewire, network, bluetooth, brightness,
                             notifications, polkit, idle, clipboard, lock, session, icontheme

src/shell/                   The shell process (GPL)
  main.cpp                   Composition: registries, transports, context properties
  BarController              IBarWidgetFactory registry owner
  ControlCenterController    Tile registry + pane open state per screen
  LauncherController         Provider registry + result model
  OsdController / ToastController / PolkitController / PickerController
  LayerPopoutTransport / PanePopoutTransport / SocketPopoutTransport / RoutingPopoutTransport
  PaneRules                  Seeds the pane's window rule into the daemon
  ShellEffects               Compositor blur behind a region (kde-blur)
  ShellMotion                Profile registry (shell.settle) + reduced motion from the portal
  ShellGestures              CompositorBridge.gestureReported relay
  ShellChrome                Decoration tree + pack registry → stage lists for DecorationSlot

examples/phosphor-shell/shell.qml   The composition root: every surface mounted per screen
```

## Data flow

**Placement state.** The daemon publishes the engines over D-Bus:
`WindowTracking` (windows, focus, metadata, urgency, per-desktop states),
`Tiling` (current tiles), `Scrolling` (strip model, view), `LayoutRegistry`
(assignments). `PlacementMap` in `phosphor-shell` folds these into one model
per screen (`PlacementMap.forScreen(name)`), which the bar's miniature, the
launcher's viewfinder, the dashboard, the cheatsheet, the OSD host and the
toast host all read. Interactions go back the same way (`activateWindow`,
`moveColumnTo`, `switchDesktop`, drop proxies).

**Popouts.** `PopoutController` arbitrates every transient surface through one
transport. The routing transport sends the control center to the pane
transport (a frameless toplevel with the pane's app id, placed by the engine
through a seeded window rule) and everything else to the layer transport
(a layer surface with the requested anchor and exclusivity). The bar draws
the tether to wherever the pane landed.

**Feedback.** `OsdRegistry` and `ToastRegistry` fan a show request out to the
host on the right screen; the host places the band on the focused window's
edge from the placement map, or on the screen edge with no focus.

**Chrome packs.** `ShellChrome` fetches the decoration tree from
`Settings.getSetting(decorationProfileTree)` (seeded with the shell's
defaults), resolves a surface path against the pack registry, and hands the
stage list to the shared `SurfaceDecoration` host that a surface's
`DecorationSlot` instantiates. The composition root supplies the host
Component through `ShellChrome.decorationComponent`, because per-screen
delegates cannot see ids in `shell.qml`.

**Gestures.** The KWin effect registers the shell's touchpad gestures and
reports each completed one over `CompositorBridge.reportGesture`; the daemon
validates and re-emits `gestureReported`; `ShellGestures` relays it to
`shell.qml`, which maps it to a surface.

**Motion.** `ShellMotion` owns the process's profile registry and publishes
it as the QML default, so `SettleAnimation` (a `PhosphorMotionAnimation` on
`shell.settle`) resolves. Reduced motion is read from the settings portal and
bound onto `Motion.reducedMotion`.

**Theme.** `PaletteStore` is a per-engine singleton; the picker's
`RetintController` previews and commits palettes into it (and to
`~/.local/share/plasmazones/palettes/current.json`), `MatugenRunner` derives
one from a wallpaper, `TemplateEngine` fans it out to other applications.
`Spectrum` exposes the four brand stops as a coordinate.

## Rules that keep the map honest

- A library never reads a context property or a singleton it does not own.
  `PlacementMap`, `OsdRegistry`, `ToastRegistry`, `ShellChrome` and the rest
  are injected by the composition root; tests bind fakes.
- Per-screen delegates read only context properties. `PerScreenPanels` and the
  popout transports instantiate against fresh contexts, so an id in
  `shell.qml` does not resolve inside one.
- Kirigami is a `DEPENDENCIES` entry, never an `IMPORTS` entry, in every QML
  module: its `Theme` attached type would shadow ours.
- A surface never carries a drop shadow or a radius above 10 px. Depth is a
  stroke and a ground step (`05-visual-identity.md` R2, R3).
