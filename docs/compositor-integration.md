# Compositor Integration Guide

PlasmaZones can run on any Wayland compositor that supports
`layer-shell-v1`.  This guide covers how shortcuts, config, and
wallpaper integration work on non-KDE compositors.

## Build for your compositor

```bash
cmake -B build -DUSE_KDE_FRAMEWORKS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

This produces `plasmazonesd` (daemon) and `plasmazones-editor` (layout
editor) with no KDE dependencies.  The KWin effect plugin and KCM
(System Settings) are skipped — they only work on KDE Plasma.

**Dependencies:** Qt 6.6+, LayerShellQt 6.6+

---

## Shortcuts

PlasmaZones auto-detects the best shortcut backend at startup:

| Backend | Compositor | How it works |
|---------|-----------|-------------|
| KGlobalAccel | KDE Plasma | KDE's native shortcut system (only with `USE_KDE_FRAMEWORKS=ON`) |
| XDG Portal | Hyprland, GNOME 48+, KDE | `org.freedesktop.portal.GlobalShortcuts` — compositor assigns keys |
| D-Bus Trigger | Sway, COSMIC, any | Manual keybinding → D-Bus call |

### XDG Portal (Hyprland, GNOME)

If your compositor supports `org.freedesktop.portal.GlobalShortcuts`,
PlasmaZones uses it automatically.  The compositor may assign different
keys than requested — use your compositor's shortcut settings to remap.

**Hyprland:** Install `xdg-desktop-portal-hyprland`.  Shortcuts work
out of the box.  No GUI remap UI — use `hyprctl` or edit config.

**GNOME 48+:** Portal support is built in.  Use GNOME Settings →
Keyboard → Shortcuts to remap.

### D-Bus Trigger (Sway, COSMIC, others)

When no portal is available, PlasmaZones exposes a D-Bus method for
each action.  Bind your compositor's keybindings to call it:

```
dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts \
    org.plasmazones.daemon.TriggerAction \
    string:"<action-id>"
```

#### Sway

Add to `~/.config/sway/config`:

```
# Open layout editor
bindsym $mod+Shift+e exec dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"open_editor"

# Toggle autotile
bindsym $mod+t exec dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"toggle_autotile"

# Cycle layout forward
bindsym $mod+bracketright exec dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"next_layout"

# Cycle layout backward
bindsym $mod+bracketleft exec dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"previous_layout"
```

#### Hyprland (without portal)

Add to `~/.config/hypr/hyprland.conf`:

```
bind = SUPER SHIFT, E, exec, dbus-send --session --dest=org.plasmazones.daemon /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"open_editor"
bind = SUPER, T, exec, dbus-send --session --dest=org.plasmazones.daemon /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"toggle_autotile"
bind = SUPER, bracketright, exec, dbus-send --session --dest=org.plasmazones.daemon /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"next_layout"
bind = SUPER, bracketleft, exec, dbus-send --session --dest=org.plasmazones.daemon /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction string:"previous_layout"
```

#### COSMIC

```
# When COSMIC supports custom keybindings, bind to:
dbus-send --session --dest=org.plasmazones.daemon \
    /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction \
    string:"toggle_autotile"
```

### Available action IDs

| Action ID | Description |
|-----------|-------------|
| `open_editor` | Open the layout editor |
| `toggle_autotile` | Toggle automatic tiling |
| `next_layout` | Switch to next layout |
| `previous_layout` | Switch to previous layout |
| `quick_layout_1` through `quick_layout_9` | Apply layout 1-9 |
| `snap_to_zone_1` through `snap_to_zone_9` | Snap active window to zone 1-9 |
| `nav_left`, `nav_right`, `nav_up`, `nav_down` | Navigate between zones |
| `move_left`, `move_right`, `move_up`, `move_down` | Move window between zones |
| `swap_left`, `swap_right`, `swap_up`, `swap_down` | Swap windows between zones |
| `cycle_forward`, `cycle_backward` | Cycle window position within zone |
| `focus_master` | Focus the master window (autotile) |
| `swap_master` | Swap focused window with master (autotile) |
| `rotate_clockwise`, `rotate_counterclockwise` | Rotate window positions |
| `toggle_floating` | Toggle window floating/tiled |

---

## Configuration

PlasmaZones stores settings in `~/.config/plasmazonesrc` (standard INI
format).  The daemon reads/writes this file via `IConfigBackend`.

### Editing settings without KDE System Settings

Since the KCM (System Settings module) requires KDE, use one of these
alternatives on other compositors:

**1. Edit the config file directly:**

```ini
[Activation]
ShiftDrag=true
DragActivationModifier=3
SnappingEnabled=true

[Display]
ShowOnAllMonitors=false
BorderWidth=3
BorderOpacity=80

[Appearance]
HighlightColor=39,62,102,128
InactiveColor=211,218,227,64
BorderColor=211,218,227,200

[Autotiling]
AutotileEnabled=false
Algorithm=bsp
```

After editing, tell the daemon to reload:

```bash
dbus-send --session --dest=org.plasmazones \
    /PlasmaZones org.plasmazones.Settings.reload
```

**2. Use the D-Bus settings API:**

```bash
# Read a setting
dbus-send --session --print-reply --dest=org.plasmazones \
    /PlasmaZones org.plasmazones.Settings.getSetting \
    string:"snappingEnabled"

# Write a setting
dbus-send --session --dest=org.plasmazones \
    /PlasmaZones org.plasmazones.Settings.setSetting \
    string:"borderWidth" variant:int32:5
```

**3. Use the layout editor** (`plasmazones-editor`) for visual layout
   creation — it works on any compositor.

### Custom config backend

For deep integration, implement `IConfigBackend` (see
`src/config/configbackend.h`).  This lets your shell read/write
PlasmaZones config from its own configuration system instead of the
INI file.

---

## Wallpaper (shader textures)

Some PlasmaZones shader effects use the desktop wallpaper as a texture
input.  The daemon auto-detects your compositor and reads the wallpaper
path:

| Compositor | Source |
|-----------|--------|
| KDE Plasma | `~/.config/plasma-org.kde.plasma.desktop-appletsrc` |
| Hyprland | `swww query` or `~/.config/hypr/hyprpaper.conf` |
| Sway | `swww query` |
| GNOME | `gsettings get org.gnome.desktop.background picture-uri` |

If auto-detection fails (or your compositor isn't listed), set the
wallpaper path manually in `plasmazonesrc`:

```ini
[Shaders]
WallpaperPath=/path/to/your/wallpaper.jpg
```

To implement support for a new compositor, see
`src/core/wallpaperprovider.cpp` — add a new `IWallpaperProvider`
implementation.

---

## Autostart

### Systemd (recommended)

The daemon installs a systemd user service:

```bash
systemctl --user enable --now plasmazones.service
```

### Manual

```bash
# Add to your compositor's autostart:
plasmazonesd &
```

#### Sway

```
# ~/.config/sway/config
exec plasmazonesd
```

#### Hyprland

```
# ~/.config/hypr/hyprland.conf
exec-once = plasmazonesd
```

---

## Compositor-specific features

The KWin effect plugin handles drag detection and modifier-key
activation on KDE Plasma.  Other compositors need their own equivalent
plugin or IPC handler — contributions welcome!

| Feature | KDE | Other compositors |
|---------|-----|-------------------|
| Zone overlay during drag | KWin effect (native) | Needs compositor plugin (Hyprland IPC, wlroots plugin, etc.) |
| Modifier-key activation | KWin effect detects modifier during drag | Needs compositor plugin — detect drag + modifier via compositor API |
| System Settings (KCM) | Full GUI | Edit `~/.config/plasmazonesrc` or use D-Bus API |
| Activity-based layouts | Full support | Not applicable (activities are a KDE concept) |
| Desktop notifications | KDE OSD | Custom QML OSD overlay (works everywhere) |
| Wallpaper in shaders | Auto-detected | Auto-detected or set `WallpaperPath` in config |

### Writing a compositor plugin

The KWin effect plugin (`kwin-effect/`) is a reference implementation.
A compositor plugin for another WM needs to:

1. **Detect window drag start/end** — notify the daemon via D-Bus
   (`org.plasmazones.WindowDrag.StartDrag` / `EndDrag`)
2. **Monitor modifier keys during drag** — check if the activation
   modifier (Shift/Ctrl/Alt) is held and notify the daemon
3. **Forward window geometry** — send the dragged window's position
   so the daemon can highlight matching zones

Each compositor has its own plugin/extension API:
- **Hyprland:** [Plugin API](https://wiki.hyprland.org/Plugins/Development/Getting-Started/) or IPC events (`activewindow`, `movewindow`)
- **wlroots/Sway:** wlroots C API for compositor modules
- **niri:** IPC event stream (`EventStream` with window move events)
- **COSMIC:** Plugin API (in development)
