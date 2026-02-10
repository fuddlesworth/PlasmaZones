<div align="center">

# PlasmaZones

<img src="icons/hicolor/scalable/apps/plasmazones.svg" alt="PlasmaZones" width="96">

**Advanced window zone management for KDE Plasma 6**

Define zones on your screen. Drag windows into them. Done.

[![CI](https://github.com/fuddlesworth/PlasmaZones/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/fuddlesworth/PlasmaZones/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/fuddlesworth/PlasmaZones)](https://github.com/fuddlesworth/PlasmaZones/releases/latest)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL%203.0-blue.svg)](LICENSE)
[![KDE Plasma 6](https://img.shields.io/badge/KDE%20Plasma-6-blue.svg)](https://kde.org/plasma-desktop/)
[![AUR](https://img.shields.io/aur/version/plasmazones-bin)](https://aur.archlinux.org/packages/plasmazones-bin)

</div>

---

## Table of Contents

- [How It Works](#how-it-works)
- [Features](#features)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [D-Bus API](#d-bus-api)
- [Project Structure](#project-structure)
- [Contributing](#contributing)
- [Support](#support)

---

## How It Works

Hold **Alt** (or your configured modifier) while dragging a window. Zones light up. Drop the window into one and it resizes to fill that zone.

<p align="center">
  <img src="docs/media/videos/drag-snap.gif" alt="Drag and Snap" />
</p>

---

## Features

### Window Snapping

- Drag with modifier key or mouse button to snap windows to zones
- Snap all visible windows to zones at once
- Auto-assign windows to first empty zone per layout
- Multi-zone snapping support
- Move windows between zones with keyboard shortcuts
- Focus adjacent zones without mouse
- Cycle through windows stacked in the same zone (monocle-style)
- Push window to first empty zone
- Restore original size on unsnap
- Per-window floating toggle
- App-to-zone rules: auto-snap specific applications to designated zones

<p align="center">
  <img src="docs/media/videos/keyboard-nav.gif" alt="Keyboard Navigation" />
</p>

### Layout Editor

- Visual canvas for drawing and resizing zones
- 12 built-in templates (columns, grids, fibonacci, master-stack, focus+stack, and more)
- Undo/redo, copy/paste, cut, duplicate
- Split zones horizontally or vertically
- Grid and edge snapping
- Fill available space / auto-expand
- Fullscreen editing mode
- Per-zone colors and styling
- Layout visibility filtering: restrict layouts to specific screens, desktops, or activities

<p align="center">
  <img src="docs/media/videos/editor.gif" alt="Layout Editor" />
</p>

### Shader Effects

GPU-accelerated zone overlays with 14 built-in effects, including multipass shaders with 3D raymarching and bloom:

| Effect | Description |
|--------|-------------|
| Aretha Shell | Cyberpunk effect with color grading, hex grid, and data streams |
| Cosmic Flow | Flowing fractal noise with animated color palette |
| Crystalline Labels | Zone numbers fractured into Voronoi cells with stained-glass edges |
| Filled Labels | Zone numbers filled with gradient (vertical, horizontal, or radial) |
| Flow Labels | Animated flowing plasma zone numbers driven by FBM noise and configurable color palette |
| Fractal Flow | Organic fractal-like flowing pattern with iterative distortion |
| Magnetic Field | Mouse-reactive magnetic field with orbiting particles |
| Neon Glow | Cyberpunk neon borders with bloom and pulsing |
| Nexus Cascade | Multi-pass plasma with distortion, bloom, and chromatic aberration |
| Particle Field | Animated floating particles with glow and smooth motion |
| Rotating Tiles | Rotating tiled grid with radial wave and pulsing edges |
| Spectrum Pulse | Audio-reactive neon energy with bass glow, spectrum aurora, and CAVA integration |
| Toxic Circuit | Glowing circuit traces with toxic drip and digital corruption |
| Voronoi Stained Glass | 3D raymarched stained glass with lead came, backlighting, and bloom |

<p align="center">
  <img src="docs/media/videos/shaders.gif" alt="Shader effects showcase" />
</p>

Custom shaders supported — see the [Shader Guide](https://github.com/fuddlesworth/PlasmaZones/wiki/Shaders) on the wiki.

### Zone Selector

Drag to screen edge to reveal a layout picker. Choose any layout and zone without cycling.

<p align="center">
  <img src="docs/media/videos/zone-selector.gif" alt="Zone Selector" />
</p>

### Visual Layout OSD

See a preview of the layout when switching, not just text.

<p align="center">
  <img src="docs/media/videos/layout-switch.gif" alt="Cycling layouts with OSD" />
</p>

### Multi-Monitor & Virtual Desktops

- Per-monitor layouts (same or different)
- Per-virtual-desktop layouts
- Per-activity layouts (optional, requires PlasmaActivities)
- Per-monitor zone selector settings
- Per-screen shader selection
- Screen-targeted app-to-zone rules

### System Settings Integration

Full KCM module with 7 tabs — no config file editing required. Includes built-in update checker with GitHub release notifications.

<details>
<summary>Screenshots</summary>

#### Layouts
Create, duplicate, and manage zone layouts with built-in templates, import/export, visibility filtering.

![Layouts](docs/media/screenshots/kcm-settings.png)

#### Editor
Keyboard shortcuts for zone operations (duplicate, split, fill), grid/edge snapping, snap modifier keys.

#### Assignments
Per-monitor, virtual desktop, and activity layout assignments; quick-switch keyboard slots; app-to-zone auto-snap rules.

#### Zones
Colors, opacity, borders, blur, shader effects, zone numbers, OSD style, animations, activation modifiers, multi-zone selection, zone padding, and window snap behavior.

![Zones — Appearance](docs/media/screenshots/kcm-appearance.png)

![Zones — Behavior](docs/media/screenshots/kcm-behavior.png)

#### Display
Zone selector popup configuration: enable/disable, screen position, trigger distance, grid/list mode, preview sizing, per-monitor overrides.

![Display](docs/media/screenshots/kcm-zoneselector.png)

#### Exclusions
Window class exclusion lists with interactive window picker, minimum size thresholds.

#### About
Version info, update checker with GitHub release notifications, repository/wiki/bug report links, license, credits.

</details>

---

## Installation

### Requirements

- KDE Plasma 6 (Wayland)
- Qt 6.6+
- KDE Frameworks 6.0+
- LayerShellQt (required for Wayland overlays)
- CMake 3.16+
- C++20 compiler

Optional:
- PlasmaActivities for activity-based layouts

### Arch Linux (AUR)

```bash
# Binary package (prebuilt)
yay -S plasmazones-bin

# Source package (builds locally)
yay -S plasmazones
```

### Building from Source

```bash
git clone https://github.com/fuddlesworth/PlasmaZones.git
cd PlasmaZones
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . -j$(nproc)
sudo cmake --install .
```

After installation, refresh the KDE service cache and enable the daemon:

```bash
kbuildsycoca6 --noincremental
systemctl --user enable --now plasmazones.service
```

Or log out and back in. Settings appear in **System Settings → Window Management → PlasmaZones**.

<details>
<summary>Local install (no root)</summary>

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .
```

Add these to your `~/.bashrc` or `~/.zshrc`:

```bash
export QT_PLUGIN_PATH=$HOME/.local/lib/qt6/plugins:$QT_PLUGIN_PATH
export QML2_IMPORT_PATH=$HOME/.local/lib/qt6/qml:$QML2_IMPORT_PATH
export XDG_DATA_DIRS=$HOME/.local/share:$XDG_DATA_DIRS
```

Then reload your shell and refresh the cache:

```bash
source ~/.bashrc  # or ~/.zshrc
kbuildsycoca6 --noincremental
systemctl --user enable --now plasmazones.service
```

</details>

<details>
<summary>RPM-based distros (Fedora/openSUSE)</summary>

An RPM spec is included in `packaging/rpm/plasmazones.spec` for building packages on Fedora, openSUSE, and other RPM-based distributions.

</details>

<details>
<summary>Universal Linux / Fedora Atomic (Portable Tarball)</summary>

For distributions where installing system packages is difficult (Fedora Atomic/Silverblue) or if you lack root privileges:

1. Download `plasmazones-linux-x86_64.tar.gz` from the [Latest Release](https://github.com/fuddlesworth/PlasmaZones/releases/latest).
2. Extract the archive.
3. Run the installer script:

```bash
tar xzf plasmazones-linux-x86_64.tar.gz
cd plasmazones-linux-x86_64
./install.sh
```

The script will install PlasmaZones to `~/.local` and help you configure your environment variables.

**Or use the one-liner:**

```bash
bash <(curl -s https://raw.githubusercontent.com/fuddlesworth/PlasmaZones/main/packaging/local-install/web-install.sh)
```

**Upgrading:**

Run the installer again with a newer tarball. It will detect the existing installation and upgrade in place.

**Uninstalling:**

```bash
~/.local/share/plasmazones/uninstall.sh
```

</details>

---

## Quick Start

1. Open **System Settings → Window Management → PlasmaZones**
2. Enable the daemon (or run `systemctl --user enable --now plasmazones.service`)
3. Click **Open Editor** to create a layout
4. Draw zones or pick a template
5. Save with **Ctrl+S**
6. **Drag any window while holding Alt** — zones appear, drop to snap

> **Can't find PlasmaZones in System Settings?** See [Troubleshooting](#troubleshooting) below.

---

## Keyboard Shortcuts

### Global Shortcuts

All configurable in **System Settings → Shortcuts → PlasmaZones**.

<details>
<summary>Layout switching</summary>

| Action | Default Shortcut |
|--------|------------------|
| Previous layout | `Meta+Alt+[` |
| Next layout | `Meta+Alt+]` |
| Quick layout 1–9 | `Meta+Alt+1` through `Meta+Alt+9` |
| Resnap windows to new layout | `Meta+Ctrl+Z` |

</details>

<details>
<summary>Window movement & snapping</summary>

| Action | Default Shortcut |
|--------|------------------|
| Snap to zone 1–9 | `Meta+Ctrl+1` through `Meta+Ctrl+9` |
| Move window left/right/up/down | `Meta+Alt+Shift+Arrow` |
| Swap window left/right/up/down | `Meta+Ctrl+Alt+Arrow` |
| Push to empty zone | `Meta+Alt+Return` |
| Snap all windows to zones | `Meta+Ctrl+S` |
| Restore window size | `Meta+Alt+Escape` |
| Toggle float | `Meta+Alt+F` |

</details>

<details>
<summary>Focus & cycling</summary>

| Action | Default Shortcut |
|--------|------------------|
| Focus zone left/right/up/down | `Alt+Shift+Arrow` |
| Cycle window forward | `Meta+Alt+.` |
| Cycle window backward | `Meta+Alt+,` |
| Rotate windows clockwise | `Meta+Ctrl+]` |
| Rotate windows counterclockwise | `Meta+Ctrl+[` |

</details>

<details>
<summary>Other</summary>

| Action | Default Shortcut |
|--------|------------------|
| Open editor | `Meta+Shift+E` |

</details>

**Shortcut pattern:** Designed to avoid conflicts with KDE defaults:
- `Meta+Alt+{key}` — Layout operations and actions
- `Meta+Alt+Shift+Arrow` — Zone movement (avoids KDE's `Meta+Shift+Arrow` screen movement)
- `Meta+Ctrl+Alt+Arrow` — Swap windows (avoids KDE's `Meta+Ctrl+Arrow` desktop switching)
- `Alt+Shift+Arrow` — Focus navigation (avoids KDE's `Meta+Arrow` quick tile)

### Editor Shortcuts

| Action | Shortcut |
|--------|----------|
| Save | `Ctrl+S` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Select all | `Ctrl+A` |
| Copy / Cut / Paste | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| Paste with offset | `Ctrl+Shift+V` |
| Delete zone | `Delete` |
| Duplicate zone | `Ctrl+D` |
| Split horizontal | `Ctrl+Shift+H` |
| Split vertical | `Ctrl+Alt+V` |
| Fill available space | `Ctrl+Shift+F` |
| Toggle fullscreen | `F11` |
| Move zone | `Arrow keys` |
| Resize zone | `Shift+Arrow keys` |
| Next / previous zone | `Ctrl+Tab` / `Ctrl+Shift+Tab` |

---

## Configuration

Settings available in **System Settings → Window Management → PlasmaZones** or directly via:

```bash
systemsettings kcm_plasmazones
```

Layouts stored as JSON in `~/.local/share/plasmazones/layouts/`.

---

## Troubleshooting

### PlasmaZones not appearing in System Settings

Refresh the KDE service cache after installing from source:

```bash
kbuildsycoca6 --noincremental
```

Or log out and back in. To verify and open directly:

```bash
# Check if KCM is registered
kcmshell6 --list | grep plasmazones

# Open directly
systemsettings kcm_plasmazones
```

### Daemon not starting

```bash
# Check status
systemctl --user status plasmazones.service

# View logs
journalctl --user -u plasmazones.service -f

# Restart
systemctl --user restart plasmazones.service
```

### Zones not appearing when dragging

1. Ensure daemon is running: `systemctl --user status plasmazones.service`
2. Check drag modifier in settings (default: Alt)
3. Verify you have at least one layout with zones
4. Check if the application is excluded in settings

---

## D-Bus API

PlasmaZones exposes 6 D-Bus interfaces for scripting and integration:

| Interface | Purpose |
|-----------|---------|
| `LayoutManager` | Layout CRUD, screen/desktop/activity assignment, quick slots |
| `Overlay` | Zone overlay visibility, highlighting, zone detection, Zone Selector |
| `Screen` | Screen enumeration, geometry, scale, add/remove notifications |
| `Settings` | Configuration load/save/reset, get/set by key |
| `WindowDrag` | Window drag lifecycle from KWin, snap geometry response |
| `WindowTracking` | Window-to-zone tracking, pre-snap geometry, floating state |

```bash
# List all layouts
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList

# Get active layout (returns JSON)
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getActiveLayout

# Switch layout
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.setActiveLayout "{uuid}"

# Show/hide overlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.showOverlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.hideOverlay

# Get all screens
qdbus org.plasmazones /PlasmaZones org.plasmazones.Screen.getScreens
```

Full API documentation: [wiki — D-Bus API](https://github.com/fuddlesworth/PlasmaZones/wiki/D-Bus-API)

---

## Project Structure

```
src/
├── core/           # Zone, Layout, LayoutManager, ShaderRegistry
├── daemon/         # Background service, overlay windows
│   └── rendering/  # GPU rendering (QRhi), label texture builder
├── editor/         # Visual layout editor
│   ├── qml/        # Editor QML UI
│   ├── helpers/    # D-Bus queries, serialization, batch operations
│   ├── services/   # Editor services (snapping, templates, zone manager)
│   └── undo/       # Undo/redo command system
├── dbus/           # D-Bus adaptors (6 interfaces)
├── config/         # Settings (KConfig), update checker
├── ui/             # QML components (OSD, overlays, zone selector)
└── shared/         # Shared QML components and plugins
kcm/                # System Settings module (KCM)
└── ui/             # KCM QML pages
    └── tabs/       # Tab components (7 tabs)
kwin-effect/        # KWin effect plugin (modifier detection, window tracking)
data/
├── layouts/        # Default layout templates (12)
└── shaders/        # Built-in GLSL shader effects (14)
packaging/
├── arch/           # AUR PKGBUILD (source, binary, git)
├── debian/         # Debian packaging (control, changelog, triggers)
└── rpm/            # RPM spec
cmake/              # CMake helpers (extract-pot, format-qml, uninstall)
tests/              # Unit and integration tests
dbus/               # D-Bus XML interface definitions
icons/              # Application icons (hicolor + hicolor-light)
po/                 # Translations (KI18n/Gettext)
docs/               # Documentation and media
```

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

**Made for KDE Plasma 6**

</div>
