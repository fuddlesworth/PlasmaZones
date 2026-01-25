<div align="center">

# PlasmaZones

<img src="icons/hicolor/scalable/apps/plasmazones.svg" alt="PlasmaZones" width="96">

</div>

A window tiling tool for KDE Plasma that brings FancyZones-style layouts to Linux. Define custom zones on your screen and snap windows into them by dragging with a modifier key held down.

## What It Does

PlasmaZones lets you divide your screen into zones. When you drag a window while holding Shift (or your configured modifier), the zones light up and you can drop the window into any of them. The window resizes to fill that zone.

Unlike automatic tiling window managers, you design your own layouts. The visual editor lets you drag to create zones, resize them, split them. Save and you're done.

## Features

**Editor**
- Visual layout editor - draw zones on a canvas
- Templates for common layouts (columns, grids, focus layout)
- Undo/redo for all operations
- Copy/paste zones within or between layouts
- Grid and edge snapping
- Per-zone colors, opacity, and border styling

**Window Management**
- Drag windows with modifier key to snap into zones
- Move windows between zones with keyboard
- Focus windows in adjacent zones
- Push window to first empty zone
- Restore window to original size
- Toggle per-window floating

**Integration**
- Multi-monitor support with per-display layouts
- Virtual desktop support with per-desktop layouts
- KDE Activity support (optional, needs KActivities)
- Settings module in System Settings
- D-Bus API for scripting
- Wayland and X11 support

## Requirements

- Qt 6.6+
- KDE Frameworks 6.0+
- CMake 3.16+
- C++20 compiler

Optional dependencies:
- `KF6::Activities` - enables activity-based layouts
- `LayerShellQt` - better Wayland overlay support

## Building

```bash
git clone https://github.com/fuddlesworth/PlasmaZones.git
cd PlasmaZones
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . -j$(nproc)
sudo cmake --install .
```

After installing, enable the daemon:

```bash
systemctl --user enable --now plasmazones.service
```

For a local install without root:

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .

# Enable the service
systemctl --user enable --now plasmazones.service
```

## Getting Started

1. Enable the daemon: `systemctl --user enable --now plasmazones.service`
2. Enable the KWin effect: System Settings > Desktop Effects > PlasmaZones
3. Run `plasmazones-editor`
4. Create a new layout or pick a template
5. Drag on the canvas to draw zones
6. Save with Ctrl+S
7. Drag any window while holding Shift to snap it

The KWin effect detects modifier keys during window dragging. Without it, you can set the activation modifier to "Always" in settings as a workaround.

## Keyboard Shortcuts

**In the editor:**

| Action | Shortcut |
|--------|----------|
| Save | Ctrl+S |
| Undo/Redo | Ctrl+Z / Ctrl+Shift+Z |
| Delete zone | Delete |
| Duplicate | Ctrl+D |
| Split H/V | Ctrl+Shift+H / Ctrl+Alt+V |
| Move zone | Arrow keys |
| Resize zone | Shift+Arrow |

**Global:**

| Action | Shortcut |
|--------|----------|
| Open editor | Meta+Shift+E |
| Previous/next layout | Meta+Alt+[ / ] |
| Quick layout 1-9 | Meta+Alt+1-9 |
| Move window to adjacent zone | Meta+Alt+Shift+Arrow |
| Focus window in adjacent zone | Alt+Shift+Arrow |
| Push to empty zone | Meta+Return |
| Restore original size | Meta+Escape |
| Toggle float | Meta+F |

All shortcuts can be customized in System Settings.

## Configuration

Access settings in **System Settings > PlasmaZones** or run `systemsettings plasmazones`.

Layout files are stored in `~/.local/share/plasmazones/layouts/` as JSON.

### D-Bus Interface

```bash
# List layouts
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList

# Show zone overlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.show
```

Full interface in `dbus/org.plasmazones.xml`.

## Project Layout

```
src/
├── core/       # Zone, Layout, detection logic
├── daemon/     # Background service
├── editor/     # Layout editor
├── dbus/       # D-Bus adaptors
└── config/     # KConfig settings
kcm/            # System Settings module
kwin-effect/    # KWin integration
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on code style, testing, and submitting changes.

## Support

- [Ko-fi](https://ko-fi.com/fuddlesworth)
- [GitHub Sponsors](https://github.com/sponsors/fuddlesworth)

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) for details.

---

Inspired by [FancyZones](https://learn.microsoft.com/en-us/windows/powertoys/fancyzones) from PowerToys.
