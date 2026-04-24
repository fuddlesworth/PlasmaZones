<div align="center">

# PlasmaZones

<img src="icons/hicolor/scalable/apps/plasmazones.svg" alt="PlasmaZones" width="96">

**Window zone management for Wayland compositors**

Define zones on your screen. Drag windows into them. Done.

[![CI](https://github.com/fuddlesworth/PlasmaZones/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/fuddlesworth/PlasmaZones/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/fuddlesworth/PlasmaZones)](https://github.com/fuddlesworth/PlasmaZones/releases/latest)
[![AUR](https://img.shields.io/aur/version/plasmazones-bin)](https://aur.archlinux.org/packages/plasmazones-bin)
[![COPR](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fcopr.fedorainfracloud.org%2Fapi_3%2Fpackage%3Fownername%3Dfuddlesworth%26projectname%3DPlasmaZones%26packagename%3Dplasmazones%26with_latest_succeeded_build%3Dtrue&query=%24.builds.latest_succeeded.source_package.version&label=COPR&color=blue)](https://copr.fedorainfracloud.org/coprs/fuddlesworth/PlasmaZones/package/plasmazones/)
<br>
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL%203.0-blue.svg)](LICENSE)
[![Wayland](https://img.shields.io/badge/Wayland-native-blue.svg)](https://wayland.freedesktop.org/)

**Docs and screenshots: [phosphor-works.github.io/plasmazones](https://phosphor-works.github.io/plasmazones/)**

</div>

---

## How It Works

Hold **Alt** (or your configured modifier) while dragging a window. Zones light up. Drop the window into one and it resizes to fill that zone.

<p align="center">
  <img src="docs/media/videos/drag-snap.gif" alt="Drag and Snap" />
</p>

---

## Highlights

- **Drag-to-snap tiling** with a modifier-held overlay and post-snap zone thumbnails for follow-up placement — [Features →](https://phosphor-works.github.io/plasmazones/#features)
- **27 built-in layouts** (grids, BSP, master+stack, fibonacci, portrait / ultrawide / superwide variants) plus a visual editor — [Getting started →](https://phosphor-works.github.io/plasmazones/getting-started/)
- **24 JavaScript autotile algorithms** in a sandboxed engine, with hot-reload for custom ones — [Tiling authoring guide →](https://phosphor-works.github.io/guides/tiling/)
- **23 GLSL shader overlays** (audio-reactive, procedural, distro-themed) with up to 4 image textures each — [Shader authoring guide →](https://phosphor-works.github.io/guides/shaders/)
- **Per-monitor, per-desktop, and virtual-screen layouts** — subdivide any physical monitor into independent logical workspaces with their own layouts, autotile state, and shortcuts
- **Coming from FancyZones?** [Feature map →](https://phosphor-works.github.io/plasmazones/from-fancyzones/)

---

## Install

```bash
yay -S plasmazones-bin                                                # Arch (AUR, prebuilt)
sudo dnf copr enable fuddlesworth/PlasmaZones && sudo dnf install plasmazones   # Fedora (COPR)
nix profile install github:fuddlesworth/PlasmaZones                   # Nix
```

openSUSE Tumbleweed, a portable tarball for Fedora Atomic / no-root setups, and source-build instructions (including the `-DUSE_KDE_FRAMEWORKS=OFF` portable build): **[Install page →](https://phosphor-works.github.io/plasmazones/#install)**.

After install, enable the daemon:

```bash
systemctl --user enable --now plasmazones.service
kbuildsycoca6 --noincremental    # KDE only — refresh the service cache
```

Requirements: a Wayland compositor with layer-shell support, Qt 6.6+, CMake 3.16+, and a C++20 compiler. Optional: KDE Frameworks 6.6+ for the KWin effect and KCM integration, PlasmaActivities for activity-based layouts.

---

## Quick Start

1. Enable the daemon: `systemctl --user enable --now plasmazones.service`
2. Open the settings app: `plasmazones-settings` (or **System Settings → PlasmaZones** on KDE)
3. **Drag any window while holding Alt** — zones appear, drop to snap.

Full first-run tour: **[Getting started →](https://phosphor-works.github.io/plasmazones/getting-started/)**.

---

## Shortcuts

| Action | Default |
|---|---|
| Open editor | `Meta+Shift+E` |
| Open settings | `Meta+Shift+P` |
| Snap window to zone 1–9 | `Meta+Ctrl+1` … `Meta+Ctrl+9` |
| Previous / next layout | `Meta+Alt+[` / `Meta+Alt+]` |
| Open layout picker | `Meta+Alt+Space` |
| Toggle autotile | `Meta+Shift+T` |
| Toggle floating | `Meta+F` |
| Restore window size | `Meta+Alt+Escape` |

Full reference — around 50 bindings across core actions, zone movement, snap, layouts, autotile, virtual screens, and the editor: **[Keyboard shortcuts →](https://phosphor-works.github.io/plasmazones/shortcuts/)**.

All bindings are rebindable in **System Settings → Shortcuts → PlasmaZones** (KDE) or the PlasmaZones settings app.

---

## Configuration

Settings live in `~/.config/plasmazones/config.json`. Layouts live in `~/.local/share/plasmazones/layouts/`. Everything is edited through the settings app:

```bash
plasmazones-settings                           # overview
plasmazones-settings -p layouts                # jump straight to layouts
plasmazones-settings --page tiling-behavior
```

The app is single-instance — launching it again while running raises the existing window and switches to the requested page.

---

## Troubleshooting

Daemon startup, verbose logging, KWin minimum-size rules, and the full support-report flow: **[Troubleshooting →](https://phosphor-works.github.io/plasmazones/troubleshooting/)**.

When filing a bug, attach a support report:

```bash
plasmazones-report
```

The archive lands in `/tmp` by default with home paths redacted, so it's safe to attach to a public issue.

---

## D-Bus API

13 interfaces on `org.plasmazones` for scripting and integration. Interface inventory, scripting recipes, and signal watching: **[D-Bus scripting guide →](https://phosphor-works.github.io/plasmazones/dbus/)**.

---

## Project Structure

Directory tree and data locations: [Project Structure](https://github.com/fuddlesworth/PlasmaZones/wiki/Project-Structure) on the wiki.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on code style, license headers, testing, and translations.

---

## Support

If PlasmaZones is useful to you, consider supporting development:

- [Ko-fi](https://ko-fi.com/fuddlesworth)
- [GitHub Sponsors](https://github.com/sponsors/fuddlesworth)

Bug reports and feature requests: [GitHub Issues](https://github.com/fuddlesworth/PlasmaZones/issues)

---

## License

GPL-3.0-or-later

---

<div align="center">

Inspired by [FancyZones](https://learn.microsoft.com/en-us/windows/powertoys/fancyzones) from PowerToys.

**Works on KDE Plasma, Hyprland, Sway, GNOME, and any Wayland compositor with layer-shell support.**

</div>
