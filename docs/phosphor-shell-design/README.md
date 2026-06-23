<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Phosphor Shell: Design Studies

Synthesis of research on **Noctalia**, **Quickshell**, the **Hyprland ricer ecosystem** (HyprPanel, end-4 illogical-impulse, ML4W), and **DankMaterialShell** (DMS), aimed at giving Phosphor a concrete shell target beyond the current proof of concept.

**Scope reminder:** Phosphor is its own standalone Wayland compositor + WM + shell ([[project-phosphor-standalone-stack]], [[project-plugin-based-compositor]]). We implement the Wayland protocols (layer-shell, session-lock, foreign-toplevel, screencopy, etc.) ourselves in `phosphor-compositor`. The shell is Qt6/QML on the `phosphor-*` library tree, no other shell framework is consumed, extended, or targeted as a deployment surface.

## Where Phosphor stands today

`examples/phosphor-shell/`, ~2.6 KLOC QML, ~16 C++ classes in `libs/phosphor-shell/`:

| Surface / capability       | Today                                                                                       |
|----------------------------|---------------------------------------------------------------------------------------------|
| Bar                        | `TopPanel.qml` (clock, CPU, mem, battery)                                                   |
| Taskbar                    | `Taskbar.qml` (basic)                                                                       |
| Popups                     | One shared `xdg_popup` (`PanelPopupHost`) hosting Calendar / Menu / MPRIS / TrayMenu        |
| Floating window            | `SettingsWindow`                                                                            |
| Media                      | `MprisWidget` (450 lines, MPRIS player)                                                     |
| Shaders                    | `corners`, `frosted_glass`, `gradient`, `shadow` (GLSL)                                     |
| Shell engine (C++)         | `ShellEngine`, `PanelWindow`, `PopupWindow`, `FloatingWindow`, `ScreenModel`, `LazyLoader`, `PersistentProperties`, `ShellLoader` |
| Data sources (C++)         | `Process`, `FileView`, `SystemClock`, `UPowerHost`, `WallpaperService`, `Toplevels`        |
| Settings store             | `phosphor-config` (`ISettings` / `Settings` / `IConfigBackend`, JSON default)               |

What's **missing** vs. a competitive 2026 Wayland shell: app launcher, notification daemon + center, lockscreen, OSDs, control center, wallpaper picker UI, theme picker, power menu, network/Bluetooth/PipeWire/brightness services and UIs, polkit agent, screenshot tool, clipboard manager, color picker, idle daemon, dashboard, theme token system, matugen pipeline, typed IPC + CLI, plugin registry, dock. Roughly **two thirds of a desktop shell**.

## Headline decisions

These are defended in detail in the per-doc sections below.

1. **One process, many surfaces.** Bar + popouts + OSDs + lockscreen + launcher + dashboard live in one `phosphor-shell` binary so they share theme state, animations, focus arbitration, and a single layer-shell namespace. Most Wayland desktops inherit a fragmented model (separate shell, lockscreen, network UI, settings daemons). We get to start unified.
2. **M3 token system + matugen template fan-out.** Wallpaper → matugen → tokens propagate to the QML `Theme` singleton, GTK CSS, qt6ct, kitty/foot/ghostty, vscode in one reload. Mirror DMS's `matugen/templates/` (~30 templates). Use upstream M3 token names verbatim, not DMS's `onSurface_12` aliases.
3. **Widget registries as plugin seams.** `BarWidgetRegistry`, `LauncherProviderRegistry`, `ControlCenterTileRegistry`, `DesktopWidgetRegistry`, `OSDRegistry`. Generalizes our existing `ILayoutSourceFactory` pattern (see `project_layout_source_factory_registry`). Versioned ABI, capability-scoped (not DMS's `Qt.createComponent()` free-for-all on arbitrary user JS).
4. **Centralized `PopoutService`.** Steal DMS's pattern outright: one C++/QML service owns lifetime, focus, screen affinity, exclusive-zone arbitration, transition animations for every transient surface. Solves the "two popups fighting over a Wayland grab" class of bugs ahead of time.
5. **Typed `IpcHandler` + `phosphorctl` CLI.** Keep our existing D-Bus adaptors. Add a typed IPC façade in QML plus a single statically-linked CLI binary. The Phosphor compositor binds keys to `phosphorctl call launcher.toggle` instead of raw D-Bus introspection. Foreign compositors in guest-shell mode can bind the same way.
6. **Connected-corner bar geometry.** DMS's `BarCanvas.qml` shape language has popouts grow out of the bar as one continuous painted surface. It is the single most distinctive visual idea across the four shells and the easiest one to differentiate Phosphor on. We already have the shader stack. The geometry is QML plus JS.
7. **Compositor service abstraction.** A `CompositorService` façade so widgets bind to abstract `workspaces` / `monitors` / `toplevels` types rather than a raw IPC. The canonical implementation is `PhosphorBackend` (our own compositor). `HyprlandBackend` / `NiriBackend` / `SwayBackend` are optional plugins for the "run Phosphor shell as guest on another compositor" case, not P0, but the abstraction keeps the door open. (Aligns with `project_plugin_based_compositor`.)

## Docs in this directory

- [`01-feature-inventory.md`](01-feature-inventory.md), surface-by-surface comparison of the four reference shells
- [`02-gap-analysis.md`](02-gap-analysis.md), what Phosphor is missing today, with priority and effort estimates
- [`03-component-map.md`](03-component-map.md), proposed QML module / C++ service architecture
- [`04-implementation-plan.md`](04-implementation-plan.md), **phased execution playbook** (library-first, example-first roll-out)
- [`mockups/`](mockups/), SVG mockups of the major surfaces (animated)

## Conventions for the mockups

- Aesthetic baseline = **the canonical Phosphor built‑in theme** at https://phosphor-works.github.io/palette/ (M3 + ANSI 16, CC‑BY‑SA 4.0). Dark variant: background `#050916` (void), surface `#0B1730` (navy), surface_container `#070F22` (abyss), surface_variant `#1E293B`, on_surface `#E6EDFF`, on_surface_variant `#94A3B8`. Accents: primary `#3B82F6` (blue), secondary `#A855F7` (purple), tertiary `#22D3EE` (cyan), error `#F43F5E` (rose). Status from ANSI 16: success `#10B981`, warning `#FBBF24`, info `#67E8F9`. See [[project-phosphor-default-palette]] for the full token list.
- Color **tokens** drive everything. The mockups render the *default* theme. Matugen replaces that theme at runtime from any wallpaper. The token names stay stable. Names like `primary`, `on_primary`, and `surface_container_high` do not change. Values do.
- M3 elevation, large rounding (16-24px), generous spacing.
- **Mockups are animated** (SMIL `<animate>` / `<animateTransform>`): tasteful 2-6s loops demonstrating real shell interactions. Open the SVGs in a browser, VS Code preview, or any SVG viewer that supports SMIL (Firefox / Chromium / WebKit all do). Animations include:
  - Active workspace pill / CC button breathing (2.4s)
  - Inverted-corner highlight pulses on bar/popout joins (2.2s)
  - **Full Control Center popping out of the bar** (`control-center.svg`, 6s cycle): bar path morphs from closed pill → grows downward with concave-corner join → 3s hold with contents visible → contents fade → bar pulls back to a pill. Path interpolates between closed and open states via SMIL spline easing.
  - Connected-corner popout open/close demo (`bar-top.svg` inset, 6s cycle)
  - Toast slide-in from screen edge (`notification-center.svg`, 6s)
  - OSD full lifecycle: fade in → value updates → hold → fade out (`osd-volume.svg`, 5s)
  - Fan-out template checkmarks appearing in sequence as matugen writes them (`wallpaper-theme-picker.svg`, 6s)
  - PAM password dots typing one-by-one (`lockscreen.svg`, 5s)
  - Launcher selection ring stepping through results (`launcher-spotlight.svg`, 7s)
  - System metric bars wobbling, media progress advancing (`dashboard.svg`)
- Annotations are inline so each mockup is self-contained.

## Source reports

The raw research from the four parallel agent runs is captured in `01-feature-inventory.md` (consolidated), cited URLs, file paths, and service names are preserved so each claim is traceable.
