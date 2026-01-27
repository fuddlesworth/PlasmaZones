<div align="center">

# PlasmaZones

<img src="icons/hicolor/scalable/apps/plasmazones.svg" alt="PlasmaZones" width="96">

</div>

FancyZones for KDE Plasma. Define zones on your screen, drag windows into them with a modifier key held.

## How it works

Hold Shift (or your configured modifier) while dragging a window. Zones light up. Drop the window into one and it resizes to fill that zone.

You design your own layouts with a visual editor. Draw zones, resize them, split them, save. Done.

## Features

**Editor**
- Draw zones on a canvas
- Templates for common layouts (columns, grids, focus)
- Undo/redo, copy/paste
- Grid and edge snapping
- Per-zone colors and styling
- Shader effects with live preview

**Window snapping**
- Drag with modifier to snap
- Move between zones with keyboard
- Focus adjacent zones
- Push to first empty zone
- Restore original size
- Per-window floating toggle

**Shaders**
- GPU-accelerated zone overlays
- 8 built-in effects (neon glow, glass, holographic, etc)
- Custom shader support
- Hot-reload while editing

**Integration**
- Multi-monitor with per-display layouts
- Virtual desktop layouts
- Activity layouts (optional)
- System Settings module
- D-Bus API
- Wayland (requires LayerShellQt)

## Requirements

- Qt 6.6+
- KDE Frameworks 6.0+
- LayerShellQt
- CMake 3.16+
- C++20 compiler

Optional:
- KF6::Activities for activity layouts

## Building

```bash
git clone https://github.com/fuddlesworth/PlasmaZones.git
cd PlasmaZones
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . -j$(nproc)
sudo cmake --install .
```

Enable the daemon:

```bash
systemctl --user enable --now plasmazones.service
```

Local install (no root):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .
systemctl --user enable --now plasmazones.service
```

## Quick start

1. System Settings > Desktop Effects > enable PlasmaZones
2. `systemctl --user enable --now plasmazones.service`
3. Run `plasmazones-editor`
4. Create a layout or pick a template
5. Draw zones, save with Ctrl+S
6. Drag windows while holding Shift

## Shortcuts

**Editor**

| Action | Key |
|--------|-----|
| Save | Ctrl+S |
| Undo/Redo | Ctrl+Z / Ctrl+Shift+Z |
| Delete | Delete |
| Duplicate | Ctrl+D |
| Split H/V | Ctrl+Shift+H / Ctrl+Alt+V |
| Move | Arrows |
| Resize | Shift+Arrows |

**Global**

| Action | Key |
|--------|-----|
| Open editor | Meta+Shift+E |
| Prev/next layout | Meta+Alt+[ / ] |
| Quick layout 1-9 | Meta+Alt+1-9 |
| Move to zone | Meta+Alt+Shift+Arrow |
| Focus zone | Alt+Shift+Arrow |
| Push to empty | Meta+Return |
| Restore size | Meta+Escape |
| Toggle float | Meta+F |

Customize in System Settings.

## Config

Settings in System Settings > PlasmaZones or `systemsettings plasmazones`.

Layouts stored in `~/.local/share/plasmazones/layouts/` as JSON.

### D-Bus

```bash
# List layouts
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList

# Show overlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.show
```

Full interface in `dbus/org.plasmazones.xml`.

## Shaders

GPU shaders for zone overlays. Pick one in the editor.

Built-in: Neon Glow, Glass Morphism, Minimalist, Holographic, Gradient Mesh, Liquid Warp, Magnetic Field, Particle Field.

Custom shaders go in `~/.local/share/plasmazones/shaders/`. See [docs/shaders.md](docs/shaders.md).

## Layout

```
src/
├── core/       # Zone, Layout, ShaderRegistry
├── daemon/     # Background service, overlay
├── editor/     # Layout editor
├── dbus/       # D-Bus adaptors
└── config/     # KConfig
kcm/            # System Settings module
kwin-effect/    # KWin integration
data/shaders/   # Built-in shaders
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Support

- [Ko-fi](https://ko-fi.com/fuddlesworth)
- [GitHub Sponsors](https://github.com/sponsors/fuddlesworth)

## License

GPL-3.0-or-later

---

Inspired by [FancyZones](https://learn.microsoft.com/en-us/windows/powertoys/fancyzones) from PowerToys.
