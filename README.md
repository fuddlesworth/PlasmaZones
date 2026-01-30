<div align="center">

# PlasmaZones

<img src="icons/hicolor/scalable/apps/plasmazones.svg" alt="PlasmaZones" width="96">

**FancyZones for KDE Plasma**

Define zones on your screen. Drag windows into them. Done.

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL%203.0-blue.svg)](LICENSE)
[![KDE Plasma 6](https://img.shields.io/badge/KDE%20Plasma-6-blue.svg)](https://kde.org/plasma-desktop/)

</div>

---

## How It Works

Hold **Alt** (or your configured modifier) while dragging a window. Zones light up. Drop the window into one and it resizes to fill that zone.

<p align="center">
  <img src="docs/media/videos/drag-snap.gif" alt="Drag and Snap" />
</p>

---

## Features

### Window Snapping

- Drag with modifier to snap windows to zones
- Move windows between zones with keyboard shortcuts
- Focus adjacent zones without mouse
- Cycle through windows stacked in the same zone (monocle-style)
- Push window to first empty zone
- Restore original size on unsnap
- Per-window floating toggle

<p align="center">
  <img src="docs/media/videos/keyboard-nav.gif" alt="Keyboard Navigation" />
</p>

### Layout Editor

- Visual canvas for drawing zones
- Templates for common layouts (columns, grids, focus+stack)
- Undo/redo, copy/paste
- Grid and edge snapping
- Per-zone colors and styling

<p align="center">
  <img src="docs/media/videos/editor.gif" alt="Layout Editor" />
</p>

### Shader Effects

GPU-accelerated zone overlays with 8 built-in effects:

<p align="center">

| Effect | Description |
|--------|-------------|
| Neon Glow | Glowing edges with bloom |
| Glass Morphism | Frosted glass blur |
| Minimalist | Clean, flat zones |
| Holographic | Iridescent shimmer |
| Gradient Mesh | Smooth color gradients |
| Liquid Warp | Fluid motion effect |
| Magnetic Field | Energy field lines |
| Particle Field | Floating particles |

</p>

<p align="center">
  <img src="docs/media/videos/shaders.gif" alt="Shader effects showcase" />
</p>

Custom shaders supported - see the [Shader Guide](https://github.com/fuddlesworth/PlasmaZones/wiki/Shaders) on the wiki.

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
- Per-activity layouts (optional, requires KActivities)

<p align="center">
  <img src="docs/media/screenshots/multi-monitor.png" alt="Multi-Monitor" />
</p>

### System Settings Integration

Full KCM module for configuration - no config file editing required.

<p align="center">
  <img src="docs/media/screenshots/kcm-settings.png" alt="Settings" />
</p>

#### Appearance
Customize zone visuals: shader effects, colors, opacity, border styles, and highlight animations.

<p align="center">
  <img src="docs/media/screenshots/kcm-appearance.png" alt="Appearance Settings" />
</p>

#### Behavior
Configure drag modifiers, snap thresholds, auto-snap for new windows, and session persistence options.

<p align="center">
  <img src="docs/media/screenshots/kcm-behavior.png" alt="Behavior Settings" />
</p>

#### Zone Selector
Set up the edge-triggered zone picker: trigger distance, display duration, and selector appearance.

<p align="center">
  <img src="docs/media/screenshots/kcm-zoneselector.png" alt="Zone Selector Settings" />
</p>

#### Shortcuts
Configure all keyboard shortcuts for layout switching, window navigation, and editor access.

<p align="center">
  <img src="docs/media/screenshots/kcm-shortcuts.png" alt="Shortcuts Settings" />
</p>

---

## Installation

### Requirements

- Qt 6.6+
- KDE Frameworks 6.0+
- LayerShellQt (required for Wayland)
- CMake 3.16+
- C++20 compiler

Optional:
- PlasmaActivities (plasma-activities package) for activity-based layouts

### Building

```bash
git clone https://github.com/fuddlesworth/PlasmaZones.git
cd PlasmaZones
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . -j$(nproc)
sudo cmake --install .
```

**Important:** After installation, refresh the KDE service cache:

```bash
kbuildsycoca6 --noincremental
```

Or log out and back in. This makes the settings module visible in System Settings.

Enable the daemon:

```bash
systemctl --user enable --now plasmazones.service
```

**Settings location:** System Settings → **Window Management** → PlasmaZones

### Local Install (No Root)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .
```

**Required for local installs:** Add these to your `~/.bashrc` or `~/.zshrc`:

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

---

## Quick Start

1. Open **System Settings → Window Management → PlasmaZones**
2. Enable the daemon (or run `systemctl --user enable --now plasmazones.service`)
3. Click **Open Editor** to create a layout
4. Draw zones or pick a template
5. Save with **Ctrl+S**
6. **Drag any window while holding Alt** - zones appear, drop to snap

> **Can't find PlasmaZones in System Settings?** See [Troubleshooting](#troubleshooting) below.

---

## Keyboard Shortcuts

### Global Shortcuts

<p align="center">

| Action | Default Shortcut |
|--------|------------------|
| Open editor | `Meta+Shift+E` |
| Previous layout | `Meta+Alt+[` |
| Next layout | `Meta+Alt+]` |
| Quick layout 1-9 | `Meta+Alt+1` through `Meta+Alt+9` |
| Snap to zone 1-9 | `Meta+Ctrl+1` through `Meta+Ctrl+9` |
| Move window left | `Meta+Alt+Shift+Left` |
| Move window right | `Meta+Alt+Shift+Right` |
| Move window up | `Meta+Alt+Shift+Up` |
| Move window down | `Meta+Alt+Shift+Down` |
| Swap window left | `Meta+Ctrl+Alt+Left` |
| Swap window right | `Meta+Ctrl+Alt+Right` |
| Swap window up | `Meta+Ctrl+Alt+Up` |
| Swap window down | `Meta+Ctrl+Alt+Down` |
| Rotate windows clockwise | `Meta+Ctrl+]` |
| Rotate windows counterclockwise | `Meta+Ctrl+[` |
| Focus zone left | `Alt+Shift+Left` |
| Focus zone right | `Alt+Shift+Right` |
| Focus zone up | `Alt+Shift+Up` |
| Focus zone down | `Alt+Shift+Down` |
| Push to empty zone | `Meta+Alt+Return` |
| Restore window size | `Meta+Alt+Escape` |
| Toggle float | `Meta+Alt+F` |
| Cycle window forward | `Meta+Alt+.` |
| Cycle window backward | `Meta+Alt+,` |

</p>

**Shortcut Pattern:** Shortcuts are designed to avoid conflicts with KDE defaults:
- `Meta+Alt+{key}` — Layout operations and actions
- `Meta+Alt+Shift+Arrow` — Window zone movement (avoids KDE's `Meta+Shift+Arrow` screen movement)
- `Meta+Ctrl+Alt+Arrow` — Swap windows between zones (avoids KDE's `Meta+Ctrl+Arrow` desktop switching)
- `Meta+Ctrl+[/]` — Rotate all windows through zones (complements layout cycling `Meta+Alt+[/]`)
- `Meta+Alt+./,` — Cycle through windows stacked in the same zone (monocle-style)
- `Alt+Shift+Arrow` — Focus navigation (avoids KDE's `Meta+Arrow` quick tile)
- `Meta+Ctrl+{1-9}` — Direct zone snapping

All shortcuts configurable in System Settings → Shortcuts → PlasmaZones.

### Editor Shortcuts

<p align="center">

| Action | Shortcut |
|--------|----------|
| Save | `Ctrl+S` |
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Shift+Z` |
| Delete zone | `Delete` |
| Duplicate zone | `Ctrl+D` |
| Split horizontal | `Ctrl+Shift+H` |
| Split vertical | `Ctrl+Alt+V` |
| Move zone | `Arrow keys` |
| Resize zone | `Shift+Arrow keys` |

</p>

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

After installing from source, you must refresh the KDE service cache:

```bash
kbuildsycoca6 --noincremental
```

Or simply log out and back in.

**Verify installation:**

```bash
# Check if KCM is registered
kcmshell6 --list | grep plasmazones

# Should output:
# kcm_plasmazones - Configure window zone snapping
```

If the KCM is registered but not visible in System Settings, try:

```bash
# Open directly
systemsettings kcm_plasmazones
```

### Local install (`~/.local`) not working

For local installs, KDE needs to know where to find the plugins. Add to `~/.bashrc` or `~/.zshrc`:

```bash
export QT_PLUGIN_PATH=$HOME/.local/lib/qt6/plugins:$QT_PLUGIN_PATH
export QML2_IMPORT_PATH=$HOME/.local/lib/qt6/qml:$QML2_IMPORT_PATH
export XDG_DATA_DIRS=$HOME/.local/share:$XDG_DATA_DIRS
```

Then:

```bash
source ~/.bashrc  # reload shell config
kbuildsycoca6 --noincremental
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

### D-Bus API

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

Full API documentation on the [wiki D-Bus API](https://github.com/fuddlesworth/PlasmaZones/wiki/D-Bus-API) (generated from `dbus/org.plasmazones.xml`) covering all interfaces and methods.

---

## Project Structure

```
src/
├── core/           # Zone, Layout, LayoutManager, ShaderRegistry
├── daemon/         # Background service, overlay windows
│   └── rendering/  # GPU rendering components
├── editor/         # Visual layout editor
│   ├── qml/        # Editor QML UI
│   ├── services/   # Editor services (snapping, templates)
│   └── undo/       # Undo/redo command system
├── dbus/           # D-Bus adaptors (7 interfaces)
├── config/         # Settings (KConfig)
├── ui/             # QML components (OSD, overlays, zone selector)
└── shared/         # Shared QML components and plugins
kcm/                # System Settings module (KCM)
└── ui/             # KCM QML pages
kwin-effect/        # KWin effect plugin (window tracking)
data/
├── layouts/        # Default layout templates
└── shaders/        # Built-in GLSL shader effects
tests/
├── unit/           # Unit tests
└── integration/    # Integration tests
dbus/               # D-Bus service files
icons/              # Application icons (hicolor)
po/                 # Translations
docs/               # Documentation
```

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## Support

If PlasmaZones is useful to you, consider supporting development:

- [Ko-fi](https://ko-fi.com/fuddlesworth)
- [GitHub Sponsors](https://github.com/sponsors/fuddlesworth)

---

## License

GPL-3.0-or-later

---

<div align="center">

Inspired by [FancyZones](https://learn.microsoft.com/en-us/windows/powertoys/fancyzones) from PowerToys.

**Made for KDE Plasma 6**

</div>
