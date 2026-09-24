<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 01: Surface Inventory

The current identity is `05-visual-identity.md`; the approved studies are in
`mockups-v3/`. This inventory describes the shipped composition, including
the live placement model and the services retained by the redesign.

## Surfaces

| Surface | Module | Behavior |
|---|---|---|
| Floating bar and placement mini | `Phosphor.Bar` | Per-output placement map, workspace caption, saved widget groups and bounded overflow. |
| Navigator | `Phosphor.Bar.MapPane`, `Phosphor.Widgets.WorkspaceNavigator` | Anchored preview and complete window list with keyboard activation. |
| Stage | `Phosphor.Dashboard.StageOverview`, `DesktopStage` | Workspace maps, native desktop preview or map fallback, window inspector and bounded filmstrip. |
| Quick settings | `Phosphor.ControlCenter` | Vertical panel or wide bottom shelf, connection details, levels and media. |
| Calendar | `Phosphor.Bar.CalendarPanel` | Local time/date, month navigation, Today and keyboard day selection. |
| Appearance | `Phosphor.Bar.AppearancePanel` | Style and Widgets tabs, presets, fonts, layout editing and import/export. |
| Media card | `Phosphor.Widgets.MediaCard` | Real MPRIS state/artwork and CAVA Ribbon, Bars or Halo visualization. |
| Launcher | `Phosphor.Launcher` | Search, provider filters, readable results and keyboard actions. |
| OSD edge bands and readouts | `Phosphor.OSD` | Value feedback located through the focused-window placement map. |
| Toasts and notification history | `Phosphor.Notifications`, `Phosphor.Bar.NotificationPanel` | Shared material with retained server data and actions. |
| Power menu | `Phosphor.Power` | Session actions with their existing availability and confirmation behavior. |
| Session lock | `Phosphor.Lock` | Shell-owned lock surface and PAM authentication. |
| Shortcut cheatsheet | `Phosphor.Dashboard` | Daemon shortcuts over the placement map. |
| Authentication prompt | `Phosphor.Polkit` | Agent-owned prompt with the selected appearance. |
| Wallpaper and theme picker | `Phosphor.Picker` | Live wallpaper preview and palette selection. |
| Wallpaper surface | `Phosphor.Picker.WallpaperSurface` | Background-layer rendering. |

## Shared behavior

- `AppearanceStore`, `Appearance`, `Tokens` and `Spectrum` resolve saved
  materials and colors; `ShellSurface` and `ShellButton` apply them.
- `FontFaces` supplies fallback fonts for configurable interface and number
  families. Fonts are resolved from the system, not bundled.
- `Motion`, `ShellMotion` and `SettleAnimation` share motion preferences.
  Visualization consumers release the audio provider while inactive.
- `PlacementMap` provides visible map cells and complete navigation windows.
  Offscreen columns, minimized windows and inactive tabs stay reachable.
- `DecorationSlot` and `ShellChrome` provide optional custom surface packs.
- The effect's gesture relay still connects through `CompositorBridge` and
  `ShellGestures`; physical gesture progress is a separate service extension.

## Services behind the surfaces

`phosphor-service-*` provide the data every surface binds: SNI tray, UPower,
MPRIS, PipeWire, NetworkManager, BlueZ, brightness (backlight and DDC/CI),
notifications (the server), polkit (the agent), idle, clipboard, icon theme,
lock (PAM), session (logind). The daemon (`plasmazonesd`) provides the placement state
over D-Bus: `WindowTracking`, `Tiling`, `Scrolling`, `LayoutRegistry`,
`Control` (shortcuts), `Settings` (the decoration tree), `WindowDrag` (drop
proxies), `CompositorBridge` (gestures).
