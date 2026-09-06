<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 02: Remaining Gaps

What the spectrum shell still lacks after phases 1 through 6 of the identity
work (`04-implementation-plan.md`). Ordered by how much of the identity each
gap withholds, not by effort.

## Identity gaps

| Gap | Why it matters | Notes |
|---|---|---|
| Compositor-drawn chrome packs | A1 §2.4 wants the compositor to draw a pack on the bar exactly as on a window frame. Today the shell hosts the chain itself, with the same packs and the same tree, so the look is right but the renderer differs. | Needs a per-surface content rect the effect can decorate (a bar's band is a strip of a taller surface). Revisit when Phosphor is the compositor. |
| Touchpad gesture progress | Gestures are events. A 1:1 drawer (the launcher following the fingers) needs progress forwarded from the effect. | `registerTouchpadSwipeShortcut` takes a progress callback; the relay would need a rate limit. |
| Bundled faces | Manrope and JetBrains Mono are resolved, not shipped; most machines fall back to Noto Sans. | Bundling means binaries in the repo. Decide with packaging. |
| Bar keyboard navigation | The placement map is pointer-only. | A3 §1 lists the chords. |
| Filmstrip drag between desktops, Shift-drop as tab, double-click verbs on the map | Listed in A2 §5, unbuilt. | `moveWindowToDesktop` exists on the daemon; the UI does not. |
| Tiling and scrolling drop proxies | The bar's drop proxy is snapping-only. | `WindowDrag.registerDropProxy` takes any cell list. |

## Surface gaps

| Gap | Notes |
|---|---|
| Notification center (history, rules editor) | The toast host has the rules seam; the history popout is unbuilt. |
| Theme browser | `ThemePresets` reads `~/.local/share/plasmazones/palettes`; there is no browser beyond the picker strip. |
| Dock | Not planned for the identity; the bar's map is the window list. |
| Control-center tiles: night mode, dark mode, airplane, power profile, wallpaper | Five tiles deferred with service blockers; see the control-center README. |
| Emoji provider in the launcher | Deferred. |
| Weather cell on the dashboard | No service. |

## Verification gaps

| Gap | Notes |
|---|---|
| Lock screen in the nested harness | The virtual KWin backend does not advertise `ext_session_lock_manager_v1`; the lock path is unit-tested only. |
| Polkit prompt in the nested harness | The host session's agent owns the seat; the prompt stays inert under the harness. |
| Real touchpad gestures | The virtual backend cannot inject swipes; the relay is driven by calling `CompositorBridge.reportGesture` directly. |

## Non-gaps

Things an earlier version of this document listed as missing that are now in
the tree or were dropped on purpose: the launcher, the notification server,
the OSDs, the control center, the lock screen, the wallpaper picker, the
polkit agent, the idle and clipboard services, the theme tokens, the matugen
pipeline, the typed IPC and `phosphorctl`, the plugin registries. The
connected-corner bar geometry was dropped: it was a port of another shell's
signature and it is not ours.
