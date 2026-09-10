<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Overview / exposé implementations survey

Research for the PlasmaZones dynamic-workspaces work. Covers Hyprland plugins, KWin's own Overview effect (read at source level), GNOME, COSMIC, and the scrolling-WM adjacent designs (PaperWM, Scroll).

---

## 1. hyprexpo

Status matters first: hyprexpo was **retired from hyprwm/hyprland-plugins**. The `hyprexpo` directory 404s; only `borders-plus-plus`, `csgo-vulkan-fix`, `hyprbars` and `hyprfocus` remain in the official repo. The maintained continuation is https://github.com/sandwichfarm/hyprexpo. Tracking issue: https://github.com/hyprwm/hyprland-plugins/issues/672

**What it shows.** A fixed NxN grid of workspace tiles over the current monitor, each tile a live render of that workspace. Dispatcher `hyprexpo:expo` with `on` / `off` / `toggle`. Selection is a click on a tile, which switches to that workspace and closes the overview.

**Config** (from `PluginConfig.cpp` and `docs/configuration/options.md` on the fork):

| option | default | meaning |
|---|---|---|
| `columns` | 3 | desktops per row, clamped 1..7 |
| `gaps_in` | 5 | spacing between tiles (px) |
| `gaps_out` | 0 | outer margin around the grid |
| `bg_col` | 0xFF111111 | grid background colour |
| `workspace_method` | `center current` | placement: `center current` or `first <workspace>` |
| `skip_empty` | 0 | skip empty workspaces using selector `m` |
| `max_workspace` | 0 | when `skip_empty = 0`, cap sequential tiles at this workspace id; 0 keeps Hyprland selector behaviour |
| `gesture_fingers` | 0 | 0 disables, otherwise 2..9 |
| `gesture_distance` | 200 | swipe distance to complete |
| `gesture_direction` | `up` | `up`/`down`/`left`/`right`/`vertical`/`horizontal`/`pinch` |
| `cancel_key` | `escape` | comma-separated keys that close without selecting; `none`/`off` disables |
| `drag_drop_enable` | fork addition | drag windows between tiles |
| `show_pinned_windows` | 0 | show pinned / picture-in-picture windows in previews |
| `show_cursor` | 1 | pointer visible during overview |
| `keynav_enable` | 1 | keyboard navigation and the overview submap |
| `tile_rounding` | 0 | corner radius on preview tiles |
| `border_width` | 2 | tile border thickness |
| `label_enable` | 1 | workspace label toggle |
| `wallpaper_bg` | fork addition | draw the monitor wallpaper behind the tiles |

The fork keeps the legacy upstream keys as compatibility aliases (`dynamic_grid`, `fill_gaps`, `mru_sort`, `active_highlight_col`, `active_highlight_border`, `hover_highlight_col`, `hover_highlight_border`, `label_pos`, `label_size`, `label_col`, `show_workspace_names`, `enable_keyboard_nav`, `enable_drag_move`, `animate_entry`), and adds per-state border colours (`border_color`, `border_color_current`, `border_color_focus`, `border_color_hover`) plus drag proxy colours (`drag_drop_proxy_color`, `drag_drop_proxy_active_color`). Note the older upstream option was named `gesture_positive`; the fork replaced that boolean with the richer `gesture_direction` string.

**The distinctive bit.** `workspace_method` decouples *which* workspaces the grid shows from *where the current one sits*. `center current` keeps the active workspace in the middle of the grid, so the grid is relative to you rather than an absolute 1..9 board. `first <workspace>` anchors the grid to a fixed starting workspace instead.

**What users complain about.** Upstream hyprexpo had **no drag and drop** for a long time, which is exactly the gap Hyprspace and hyprtasking filled. Plugin ABI breakage on every Hyprland release is the recurring pain, and it is what eventually killed the official plugin.

---

## 2. Hyprspace (KZDKM/Hyprspace)

https://github.com/KZDKM/Hyprspace

**What it shows.** A **top (or bottom) panel strip** of workspace thumbnails, not a full-screen grid. The current workspace stays live and full size below the panel; the panel slides in over or beside it. This is the closest existing design to a scrolling-WM-friendly overview, because it does not take over the whole screen.

**Dispatchers.** `overview:toggle`, `overview:open`, `overview:close`, each accepting an `all` argument to affect every monitor rather than just the current one. It also opens on a vertical workspace swipe.

**Interactions.**
- Click a workspace thumbnail to switch to it.
- Click a window inside a thumbnail and drag it into another thumbnail to move it there. `autoDrag` makes any click start a drag rather than requiring a drag threshold.
- `autoScroll` makes scrolling over the panel switch workspaces. Scroll or swipe pans the strip when there are more workspaces than fit.
- `switchOnDrop` follows the window to its destination workspace after a drop.
- `exitOnClick`, `exitOnSwitch` and `exitKey` (default Escape) control dismissal.

**Config**, grouped:
- Colours: `panelColor`, `panelBorderColor`, `workspaceActiveBackground`, `workspaceInactiveBackground`, `workspaceActiveBorder`, `workspaceInactiveBorder`, `dragAlpha` (0..1), `disableBlur`.
- Layout: `panelHeight`, `panelBorderWidth`, `onBottom`, `workspaceMargin`, `reservedArea` (notch padding), `workspaceBorderSize`, `centerAligned`, `overrideGaps` with `gapsIn` / `gapsOut`, `affectStrut`.
- Layer hiding: `hideBackgroundLayers`, `hideTopLayers`, `hideOverlayLayers`, `hideRealLayers`.
- Visibility: `drawActiveWorkspace`, `showNewWorkspace`, `showEmptyWorkspace`, `showSpecialWorkspace`.
- Gestures: `disableGestures`, `reverseSwipe`.
- Animation: `overrideAnimSpeed`.

**Three settings worth stealing outright.** `affectStrut` decides whether the panel reserves space (pushing tiled windows down) or floats over them. `centerAligned` toggles between a KDE/macOS centred strip and a Windows-style left-aligned one. `showNewWorkspace` renders a trailing placeholder tile representing the not-yet-created next workspace, which is the dynamic-workspace affordance without needing a "+" button.

**Compatibility.** Documented as working alongside hyprsplit, split-monitor-workspaces, hyprexpo and third-party layout plugins.

**Known bugs and complaints.** The roadmap still lists "dragging windows between workspace views" as incomplete, so the drag path has rough edges. Generic Hyprland-side flicker when moving windows between workspaces (wallpaper flashes for a frame before the client repaints) shows up in this workflow: https://github.com/hyprwm/Hyprland/issues/9204 and https://github.com/hyprwm/Hyprland/issues/5430.

---

## 3. hyprtasking (raybbian/hyprtasking)

https://github.com/raybbian/hyprtasking

The most complete of the three. Supports Hyprland `v0.46.2`-`v0.56.1` with per-release ABI pins in `hyprpm.toml`.

**Layouts.** Two shipped, a third planned:
- **grid**: rows x cols, plus **layers** (`grid.layers = 2`, `grid.loop_layers`), so you get a zoomed-out grid of grids and `hyprtasking:setlayer` / `move("in"|"out")` moves between zoom levels.
- **linear**: a single scrollable horizontal strip of workspaces. `linear.top` puts it at the top, `linear.height = 400`, `linear.scroll_speed = 1.0`, `linear.blur`. This is the Hyprspace shape reimplemented inside a unified layout abstraction.
- **minimap**: planned, not implemented.

**Dispatchers.** `hyprtasking:toggle` (`cursor` opens on one monitor and closes on all; `all` affects every monitor), `hyprtasking:move` (up/down/left/right/**in**/**out**), `hyprtasking:movewindow` (moves the hovered window to an adjacent workspace and switches there), `hyprtasking:setlayer`, `hyprtasking:killhovered`. Also exposes `is_active()` so a non-consuming Escape binding can close it.

**Interaction split.** Deliberate and clever: **right click switches workspace, left click drags a window**. Configurable via `select_button = 0x111` and `drag_button = 0x110` (Linux input event codes). That removes the click-versus-drag ambiguity that Hyprspace papers over with `autoDrag`.

**Jump labels.** `jump.enabled` overlays labels `1`-`9`, then `0`, then `a`-`z` over workspaces; press the label to jump there. Styled by `jump.label_color`, `jump.label_background`, `jump.label_size`.

**Gestures.** `gestures.enabled`, `move_fingers = 3` with `move_distance = 300` to navigate, and `open_fingers = 4` with `open_distance = 300` and `open_positive` to open. Separate finger counts for open versus navigate is the right factoring.

**Other config.** `gap_size = 10`, `bg_color`, `border_size = 2`, `exit_on_hovered = false`, `warp_on_move_window = 1`, `close_overview_on_reload = false`, `grid.rows = 3`, `grid.cols = 3`, `grid.loop = false`, `grid.gaps_use_aspect_ratio = true`.

Multi-monitor and monitor scaling are both listed as tested. Touchscreen support is not implemented.

---

## 4. Hyprland native

There is no built-in overview. The feature request for a Wayfire-style overview with window dragging is https://github.com/hyprwm/Hyprland/issues/1902, closed as an enhancement without implementation. The adjacent native concept is the **special workspace**, a named overlay workspace toggled with `togglespecialworkspace`, which is a scratchpad rather than an overview: it shows one workspace over the current one, has no thumbnails, and no cross-workspace drag. This absence is why the plugin ecosystem carries the whole burden, and why every plugin breaks on ABI bumps.

---

## 5. KWin Overview effect (Plasma 6)

Source: https://invent.kde.org/plasma/kwin/-/tree/master/src/plugins/overview

Files: `overvieweffect.cpp/h`, `overviewconfig.kcfg`, `main.cpp`, `metadata.json`, and `qml/Main.qml`, `qml/DesktopBar.qml`, `qml/DesktopView.qml`.

This is the most directly relevant prior art for PlasmaZones, because it is QML on top of a KWin effect, exactly the shape our overlay work already takes.

### Architecture (Main.qml)

- One `FocusScope` per screen, rooted on `KWinComponents.SceneView.screen`. It is genuinely **per-output**: `targetScreen`, `outputName: targetScreen.name`, and `effect.desktopOffsetForScreen(targetScreen)` for the per-output desktop offset.
- A `Repeater` over `desktopModel` produces one item per virtual desktop. Each contains a `KWinComponents.DesktopBackground` plus a **`WindowHeap`** whose model is a `KWinComponents.WindowFilterModel` scoped by `activity`, `desktop` and `screenName`, filtered by `effect.searchText`, with `minimizedWindows: !effect.ignoreMinimized` and Dock / Desktop / Notification / CriticalNotification types masked out. Delegates are `WindowHeapDelegate`, which render window thumbnails.
- **Overview and Grid are one effect with two continuous state values**, `overviewVal` and `gridVal`, driven by `effect.overviewPartialActivationFactor`, `effect.gridPartialActivationFactor` and `effect.transitionPartialActivationFactor`. States are `initial`, `overview`, `grid`, `partialOverview`, `partialGrid`, `transition`; when a gesture ends without completing, the state machine snaps to whichever value crossed `effect.gestureThreshold`. This is how Plasma 6 merged the old Desktop Grid into Overview: not two effects, one 2D interpolation. The desktop bar's opacity is literally `overviewVal * Math.atan2(overviewVal, gridVal) / Math.PI * 2`, treating (overviewVal, gridVal) as a point whose angle selects the flavour.
- Desktop placement in grid mode is a stack of four `Scale` and `Translate` transforms applied to the full-size desktop item, so nothing re-lays out; it is pure transform. `deltaColumn` / `deltaRow` express each desktop's grid offset from the active one, and both have `Behavior` animations gated on `overviewVal > 0 && !container.desktopJustCreated`.
- Rows come from `KWinComponents.Workspace.desktopGridHeight`; `verticalDesktopBar` flips the desktop bar to a vertical column when the grid is taller than it is wide.

### Desktop bar (DesktopBar.qml)

- A `Flickable` of `DesktopView` thumbnails at `Kirigami.Units.gridUnit * 5` tall, scaled by `desktopHeight / targetScreen.geometry.height` with `transformOrigin: Item.TopLeft`, masked to rounded corners via `layer.effect: OpacityMask` with `radius: width / 20` (a fraction of width so it stays constant under scaling).
- **Add**: a trailing `PC3.Button` calling `desktopModel.create(desktopModel.rowCount())`, which disables itself at `desktopModel.maximum`. The `+` / `=` keys do the same from anywhere in the overview.
- **Remove**: a hover-revealed delete button per thumbnail calling `desktopModel.remove(delegate.index)`, guarded so the last desktop can never be deleted, and moving focus to a neighbour first. The `Delete` key removes the focused thumbnail; `-` removes the last desktop.
- **Rename in place**: click the label or press F2 to swap the `PC3.Label` for a `PC3.TextField` writing back to `desktop.name`, with Escape cancelling.
- **Drop target**: each thumbnail carries a `DropArea` whose `onDropped` does `drag.source.desktops = [delegate.desktop]`. The drop target is the **thumbnail**, not a positional insert point. It ignores the drop when the window is already on that desktop or is on-all-desktops (`desktops.length === 0`).
- Click activates; clicking the already-current desktop deactivates the effect entirely.

### Grid-mode drops are richer

The full-size desktop item's `DropArea` inspects `drop.keys`:

```qml
if (drop.keys.includes("kwin-desktop")) {
    // dragging a desktop as a whole
    KWinComponents.Workspace.moveDesktop(drag.source.desktop, desktop.x11DesktopNumber - 1);
} else {
    // dragging a KWin::Window
    drag.source.desktops = [mainBackground.desktop];
}
```

So one drop surface handles both window-to-desktop moves and **desktop reordering**, distinguished by the drag payload's `Drag.keys`. The `WindowHeap` itself sets `Drag.keys: ["kwin-desktop"]` and is dragged by a `DragHandler` that is only `enabled: gridVal !== 0`.

### Other details worth noting

- `onItemDroppedOutOfScreen(globalPos, item, screen)` reassigns a window dropped onto a different screen's view, so cross-output drags land correctly.
- Right-click on a window thumbnail toggles on-all-desktops (`window.desktops = []` and back); middle-click closes the window.
- A downward flick closes a window, with `targetScale` tracking drag progress and `opacity: 1 - downGestureProgress`.
- A `PlasmaExtras.SearchField` feeds `effect.searchText` into every heap's filter model. It is `readOnly` in pure grid mode and clears itself on that transition. Escape clears the search before it closes the effect.
- Wheel over the overview steps desktops in 120-unit notches, respecting `KWinComponents.Workspace.virtualDesktopNavigationWrapsAround`.
- Number keys 1-9/0 and F1-F12 jump directly to a desktop; arrow keys move selection between desktops and hand off to the adjacent output's view when they run off the edge (`effect.getView(Qt.LeftEdge)` and friends).
- `desktopJustCreated` suppresses the reflow `Behavior` for one `effect.animationDuration` after a desktop is added, so a new desktop does not make the whole grid lurch.
- The single-desktop case renders a `Kirigami.PlaceholderMessage` ("No other Virtual Desktops to show") with "Add Virtual Desktop" and "Configure Virtual Desktops…" buttons, rather than an empty bar.

### Config surface is tiny

`overviewconfig.kcfg` has only: `IgnoreMinimized` (false), `FilterWindows` (true), `OrganizedGrid` (true), plus `BorderActivate` (defaults to `ElectricTopLeft`), `GridBorderActivate`, `TouchBorderActivate`, `GridTouchBorderActivate`. Everything else is derived from Kirigami units and the virtual-desktop KCM.

---

## 6. GNOME Shell activities overview

Window thumbnails centred, workspace thumbnails as a strip along the top (horizontal since GNOME 40).

**Dynamic workspaces** is the reference implementation of the model: a new empty workspace is created automatically as soon as a window is moved to the last workspace, and an existing workspace is removed automatically when its last window is closed or moved elsewhere. There is no "+" button in the dynamic model; the affordance is a trailing empty workspace you can drop onto, which then grows a new trailing empty one behind it.

**Moving windows.** Drag a window's miniature onto a workspace thumbnail to move it. The workspace strip is only **partially visible until a drag starts, then expands to full** — progressive disclosure that makes a compact strip usable as a drop target. Dragging to the right screen edge, or `Ctrl+Alt+Down`, also creates a workspace.

Sources: https://help.gnome.org/gnome-help/shell-workspaces.html and https://help.gnome.org/gnome-help/shell-workspaces-movewindow.html

**What users complain about.** The dynamic model evaporates a workspace you deliberately emptied but wanted to keep, and the fixed-count alternative is only reachable through a settings toggle rather than from the overview itself.

---

## 7. COSMIC (pop-os/cosmic-comp + cosmic-workspaces-epoch)

Design thread: https://github.com/pop-os/cosmic-epoch/issues/47

**What it shows.** A vertical column of workspace thumbnails on one side of the screen, each containing window previews. Workspace names and numbers show by default, names ellipsising when tight. When more workspaces are in use than fit, thumbnails scale down to a floor (tentatively 150px wide) and then the container scrolls.

**Interactions.**
- Drag a window preview out of one workspace container and into another to move it.
- Drag a workspace thumbnail to **reorder workspaces**.
- Click a workspace to switch to it. `Super`+1..9 switches by number, `Super`+0 reaches the last.
- Click a workspace name to rename it; the full text auto-selects.

**Workspace lifecycle.** Workspaces can be **pinned** so they persist even when empty, which is the explicit answer to dynamic workspaces losing a workspace you wanted to keep. Deleting a workspace relocates its windows to an adjacent one (the previous, or the next if you deleted the first), and the last workspace cannot be deleted. Under the "fixed number of workspaces" configuration the overview grows an explicit add control.

**Known rough edge.** Dragging a window to another workspace and then clicking that workspace could break the session: https://github.com/pop-os/cosmic-comp/issues/1865

---

## 8. Scrolling-WM adjacent: PaperWM and Scroll

### PaperWM

https://github.com/paperwm/PaperWM — scrollable tiling plus per-monitor workspaces, as a GNOME Shell extension (currently GNOME 47-49).

A **minimap** appears while navigating and stays visible as long as Super is held. It is not a modal overview; it is a transient HUD showing your position on the strip. The workspace name replaces the Activities button in the top left: scrolling on it browses the workspace stack the same way `Super`+Above_Tab does, left click opens the full GNOME overview, right click renames the workspace.

The distinctive design point is that the minimap is *navigation feedback*, not a destination. You never "enter" it; it exists only while you are moving.

### Scroll (dawsers/scroll)

https://github.com/dawsers/scroll

The most interesting model for a scrolling WM, because **overview is a scale, not a mode**.

- `scale_workspace overview` fits all windows of the current workspace into the viewport at a computed scale. You remain fully interactive at that scale: change focus, move windows, create or destroy them, even type into them.
- `scale_workspaces` extends that to a global overview where every output renders all its workspaces scaled to fit.
- Layered on top is **jump**, a vim-easymotion-style labelling: `jump` puts labels on every completely visible window and the matching keystrokes focus it. Variants: `jump tiling` (tiled only), `jump floating` (repositions floating windows so they do not overlap, then labels them), `jump container` (windows in the active column or row), `jump workspaces` (preview and switch workspaces), `jump all` (every window across all workspaces, per monitor). Clicking an item also exits jump and focuses it.

Because overview is just a viewport scale, there is no separate thumbnail pipeline, no frozen-screenshot uncanny valley, and no mode-entry animation to get right.

---

## Ideas worth borrowing beyond niri

1. **Overview as a continuous scale rather than a mode** (Scroll, and KWin's `overviewVal` / `gridVal` pair). For a scrolling engine this is the natural fit: the strip is already a continuous coordinate space, so zooming out is a camera change, not a separate scene. It also means gestures interrupt and reverse cleanly. KWin's trick of expressing two overview flavours as one 2D point, and deriving every opacity and transform from its magnitude and angle, is worth copying wholesale.

2. **Interactive at overview scale.** Scroll lets you type into a window while zoomed out. That removes the whole class of "the overview is a screenshot that lies" bugs, and for PlasmaZones it means the overview is the strip renderer at a different scale factor rather than a parallel thumbnail path.

3. **Jump labels.** Cheap to implement, keyboard-first, and works identically for windows, columns and workspaces. It gives a scrolling WM a way to reach a distant column without scrolling through everything in between. hyprtasking and Scroll independently landed on the same alphabet: `1`-`9`, `0`, then `a`-`z`.

4. **Separate mouse buttons for select and drag** (hyprtasking: right click switches, left click drags). Eliminates the drag-threshold guessing that Hyprspace has to work around with `autoDrag`.

5. **A trailing placeholder workspace instead of a "+" button** (GNOME's dynamic model, Hyprspace's `showNewWorkspace`). The drop target and the creation gesture become the same thing. Pair it with COSMIC's **pinned workspaces** so a deliberately-kept empty workspace does not evaporate, which is the single most common complaint about GNOME's model.

6. **Reveal more of the workspace strip when a drag starts** (GNOME). Progressive disclosure that costs nothing and makes a compact strip usable as a drop target without permanently spending the screen space.

7. **Drop keys to distinguish payload types on one surface** (KWin's `drop.keys.includes("kwin-desktop")`). A single `DropArea` handles both "move this window here" and "reorder this workspace here", which matters if we want workspace reordering without inventing a second interaction mode.

8. **Suppress reflow animation immediately after creating a workspace** (KWin's `desktopJustCreated` latch). Small, but it is the difference between adding a workspace feeling instant and feeling like the whole board lurched sideways.

9. **Zoom layers in the grid** (hyprtasking's `grid.layers` with `move("in"|"out")`). If workspace counts get large, a grid of grids beats an ever-shrinking single grid, and `in` / `out` as directional verbs slots into an existing navigation vocabulary rather than needing new concepts.

10. **Strut versus overlay as a setting** (Hyprspace's `affectStrut`). Whether the overview panel reserves space or floats over the strip is genuinely a per-user preference, and for a scrolling engine reserving space changes column geometry, so it needs an explicit answer either way.

11. **Rename in place, delete on hover, never delete the last one** (KWin's DesktopBar). The full set of workspace-management affordances lives in the overview rather than in a settings page, which is what makes an overview feel like a manager rather than a switcher.

12. **A relative rather than absolute board** (hyprexpo's `workspace_method: center current`). Centring the grid on the active workspace makes the overview a local neighbourhood view. For an endless strip that is arguably more correct than showing workspaces 1..N from the origin.

---

## Sources

- https://github.com/hyprwm/hyprland-plugins
- https://github.com/hyprwm/hyprland-plugins/issues/672
- https://github.com/sandwichfarm/hyprexpo
- https://github.com/KZDKM/Hyprspace
- https://github.com/raybbian/hyprtasking
- https://github.com/hyprwm/Hyprland/issues/1902
- https://github.com/hyprwm/Hyprland/issues/9204
- https://github.com/hyprwm/Hyprland/issues/5430
- https://invent.kde.org/plasma/kwin/-/tree/master/src/plugins/overview
- https://help.gnome.org/gnome-help/shell-workspaces.html
- https://help.gnome.org/gnome-help/shell-workspaces-movewindow.html
- https://github.com/pop-os/cosmic-epoch/issues/47
- https://github.com/pop-os/cosmic-comp/issues/1865
- https://github.com/paperwm/PaperWM
- https://github.com/dawsers/scroll
