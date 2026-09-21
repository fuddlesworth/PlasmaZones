<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 01: Surface Inventory

What the Phosphor shell ships, surface by surface, and which of the identity's
claims each one carries. The identity itself is `05-visual-identity.md`; the
per-surface specifications are `identity/A3-surfaces.md`; the pictures are
`mockups-v2/`.

The first version of this document was a feature matrix of four reference
shells (DankMaterialShell, Noctalia, HyprPanel, illogical-impulse). That
comparison drove a parity checklist, and the parity checklist produced a
shell that looked like the references. It is kept only as the short "what we
do not copy" list at the end. Everything else here is about our own shell.

## The three claims

Every surface below is built on one or more of these. A surface that carries
none of them is a candidate for removal, not for polish.

1. **The bar is the live placement map.** The engines' state (zones, tiles,
   strip columns) is drawn as a miniature in the bar and is interactive there.
2. **Popouts are engine-placed windows.** The control center is a real toplevel
   the placement engine positions, tethered to the chip that opened it.
3. **Feedback lands on the window it concerns.** OSD bands, toasts and the
   polkit prompt sit on the edge of the focused window, not in a screen corner.

## Surfaces

| Surface | Module | Claim | Spec | Mock |
|---|---|---|---|---|
| Bar (rail + band + placement map) | `Phosphor.Bar` | 1 | A2 | `bar-modes.svg`, `bar-widgets.svg` |
| Control center (engine-placed pane) | `Phosphor.ControlCenter` + `PanePopoutTransport` | 2 | A3 §2 | `control-center.svg` |
| Launcher with viewfinder | `Phosphor.Launcher` | 1 | A3 §1 | `launcher.svg` |
| OSD edge bands | `Phosphor.OSD` | 3 | A3 §4 | `osd.svg` |
| Toasts under a window band | `Phosphor.Notifications` | 3 | A3 §3 | `notifications.svg` |
| Power word column | `Phosphor.Power` | – | A3 §5 | `power-menu.svg` |
| Lock screen (session lock) | `Phosphor.Lock` | 1 | A3 §6 | `lockscreen.svg` |
| Dashboard (every desktop's map) | `Phosphor.Dashboard` | 1 | A3 §7 | `dashboard.svg` |
| Cheatsheet on the map | `Phosphor.Dashboard` | 1 | A3 §10 | `polkit-cheatsheet.svg` |
| Polkit prompt on the window edge | `Phosphor.Polkit` | 3 | A3 §9 | `polkit-cheatsheet.svg` |
| Wallpaper and theme picker | `Phosphor.Picker` | – | A3 §8 | `wallpaper-theme-picker.svg` |
| Wallpaper surface (background layer) | `Phosphor.Picker` `WallpaperSurface` | – | A3 §8 | – |

Every surface is a layer-shell or session-lock window the shell owns. None is
a guest on another desktop's surface: the wallpaper, the lock, the agent and
the notification server are ours.

## Cross-cutting behaviour

| Behaviour | Where | Status |
|---|---|---|
| Colour as coordinate (rail axis, structure axis, state axis) | `Phosphor.Theme.Spectrum`, `SpectrumRail`, `SpectrumStroke`, `SpectrumUnderline` | shipped |
| Enter / hold / release motion, settle spring, reduced motion | `Phosphor.Theme.Motion`, `SettleAnimation`, `ShellMotion` | shipped |
| Tabular values with the underline tick | `TabularText` | shipped |
| Surface packs on the chrome (same packs as window frames) | `DecorationSlot` + `ShellChrome` + shared `SurfaceDecoration` | shipped for bar, OSD, toast, launcher, polkit, picker, pane, lock clock |
| Touchpad gestures through the compositor | effect `gestures.cpp` → `CompositorBridge.reportGesture` → `ShellGestures` | shipped |
| Matched-edge morph between placement modes | `MiniatureEdges.js` | shipped |
| Light theme (same spectrum on a bright field) | `ThemePresets::lightPalette`, `Theme.isDark` consumers | shipped |
| Typography (Manrope, JetBrains Mono, resolved with fallbacks) | `FontFaces` | shipped; faces are not bundled |

## Services behind the surfaces

`phosphor-service-*` provide the data every surface binds: SNI tray, UPower,
MPRIS, PipeWire, NetworkManager, BlueZ, brightness (backlight and DDC/CI),
notifications (the server), polkit (the agent), idle, clipboard, icon theme,
lock (PAM), session (logind). The daemon (`plasmazonesd`) provides the placement state
over D-Bus: `WindowTracking`, `Tiling`, `Scrolling`, `LayoutRegistry`,
`Control` (shortcuts), `Settings` (the decoration tree), `WindowDrag` (drop
proxies), `CompositorBridge` (gestures).

## What we deliberately do not copy

- Capsule bars with pill widgets and drop shadows. Chrome is thin strokes on
  navy at four radii, and depth is a stroke and a ground step.
- Screen-corner OSD cards and toast stacks. Feedback goes on the window.
- A dashboard that is a second desktop. Ours is the placement map of every
  desktop.
- Spring physics on opacity. Springs settle position and size only.
- A separate motion system for the compositor and the shell. Both read the
  same curve files and profiles.
