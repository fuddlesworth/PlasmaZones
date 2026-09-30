<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 02: Remaining Gaps

These are service extensions and hardware verification limits beyond the
completed floating-shell redesign (`04-implementation-plan.md`). The current
visual reference is `mockups-v3/`; historical A1–A4 proposals below are not
requirements for this redesign.

## Control-center service integration

Night light, dark mode, airplane mode, power profiles, wallpaper access and
battery details now have native panels and service wiring in the tree. The
status-icon group is also complete, including saved order and visibility
policies, overflow, keyboard access and per-icon menus.
Automated regressions and isolated native visual checks pass. The current
validation record is in `04-implementation-plan.md`.

Night light uses KWin and the shared KNightTime schedule. The native service does
not expose geolocation availability, so the browser's missing-location fixture
does not become a claimed native service state. Power profiles show the daemon's
supported choices; `PerformanceDegraded` is informational and does not disable
Performance. Airplane mode covers Wi-Fi, WWAN and Bluetooth with remembered
radio restoration and a Bluetooth exception. Wired networking stays available.
Dark mode and wallpaper reuse Appearance's state and preview transaction.
Expanded UPower details include service availability, charging, estimates,
health, energy and connected devices. Power-profile actions use their separate
service; no charging-limit control is implied by UPower support.

The status group reads the same NetworkManager, BlueZ, UPower, PipeWire,
notification and power-profile state as the detail pages. Actions are routed
through those controllers, and the native tests cover service loss, pending
audio changes, unavailable devices and persistence failures. The remaining
hardware gap is verification with physical radios, audio devices and batteries.

## Optional interactions inherited from the earlier design

These proposals are not unfinished v3 visual-port requirements. They need a
current interaction decision before implementation.

| Gap | Why it matters | Notes |
|---|---|---|
| Compositor-drawn chrome packs | A1 §2.4 wants the compositor to draw a pack on the bar exactly as on a window frame. Today the shell hosts the chain itself, with the same packs and the same tree, so the look is right but the renderer differs. | Needs a per-surface content rect the effect can decorate (a bar's band is a strip of a taller surface). Revisit when Phosphor is the compositor. |
| Touchpad gesture progress | Gestures are events. A 1:1 drawer (the launcher following the fingers) needs progress forwarded from the effect. | `registerTouchpadSwipeShortcut` takes a progress callback; the relay would need a rate limit. |
| Bundled faces | Manrope and JetBrains Mono are resolved, not shipped; most machines fall back to Noto Sans. | Bundling means binaries in the repo. Decide with packaging. |
| Direct bar-mini keyboard navigation | Navigator and Stage support arrows, Home/End and Enter. | The mini itself still uses pointer interactions. |
| Filmstrip drag between desktops, Shift-drop as tab, double-click verbs on the map | Listed in A2 §1.5–1.6, unbuilt. | Drag onto another desktop preview to move a window. Shift-drop onto a scrolling column to make a tab. Proposed double-click actions were layout editing, promote-to-master and column maximize. Stage already exposes move-to-workspace buttons. |
| Tiling and scrolling drop proxies | The bar's drop proxy is snapping-only. | `WindowDrag.registerDropProxy` takes any cell list. |

## Surface gaps

| Gap | Notes |
|---|---|
| Notification rules editor | The toast host has the rules seam and nothing edits it. The history is built: `NotificationPanel` is the centre, reading the shell's retained list, and the chip opens it. |
| Palette-library browser | Appearance now has Phosphor/Paper/Ember, wallpaper colors and preset import/export. A browser for the `ThemePresets` palette library is separate. |
| Dock | Not planned for the identity; the bar's map is the window list. |
| Emoji provider in the launcher | Deferred. |
| Weather cell on the dashboard | No service. |

## Verification gaps

| Gap | Notes |
|---|---|
| Lock screen in the nested harness | The virtual KWin backend does not advertise `ext_session_lock_manager_v1`. The production lock UI is visually testable through `phosphor-shell/scripts/nested-shell/lock-preview.sh`; PAM and compositor-lock lifecycle remain service-test coverage. |
| Real polkit authentication in the nested harness | The host session's agent owns the seat. `authentication-preview.sh` exercises the production UI with fixtures, while service tests cover the agent. Seat-owned authentication needs a real-session check. |
| Physical Wi-Fi, Bluetooth, audio and battery behavior | Private D-Bus and PipeWire fixtures exercise service contracts. Hardware association, pairing, device changes and battery reporting still need real devices. |
| Additional control-center services | Native service tests and isolated visual checks pass. Actual display warmth, radio switches, WWAN hardware and power-profile effects require the corresponding services and hardware. |
| Real touchpad gestures | The virtual backend cannot inject swipes; the relay is driven by calling `CompositorBridge.reportGesture` directly. |

## Non-gaps

Things an earlier version of this document listed as missing that are now in
the tree or were dropped on purpose: the launcher, the notification server,
the OSDs, the control center, the lock screen, the wallpaper picker, the
polkit agent, the idle and clipboard services, the theme tokens, the matugen
pipeline, the typed IPC and `phosphorctl`, the plugin registries, the
notification centre, and the decoration slot on layer-routed surfaces
(`PopoutHost` now takes the same `decoration` component `PaneHost` does, so a
pack reaches the dashboard, the cheatsheet, the power menu and the bar's
panels). The
connected-corner bar geometry was dropped: it was a port of another shell's
signature and it is not ours.
