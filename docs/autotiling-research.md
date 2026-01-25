# Autotiling Window Manager Research Report

## Executive Summary

This document provides comprehensive research on autotiling/dynamic tiling algorithms and implementations from major tiling window managers including i3, Sway, dwm, bspwm, Bismuth, and Krohnkite. The goal is to understand core algorithms, user interaction patterns, and configuration options to inform the PlasmaZones autotiling feature design.

---

## Table of Contents

1. [Core Tiling Algorithms](#1-core-tiling-algorithms)
2. [Window Manager Implementations](#2-window-manager-implementations)
3. [User Interaction Patterns](#3-user-interaction-patterns)
4. [Configuration Options](#4-configuration-options)
5. [Key Features Analysis](#5-key-features-analysis)
6. [Recommendations for PlasmaZones](#6-recommendations-for-plasmazones)

---

## 1. Core Tiling Algorithms

### 1.1 Binary Space Partitioning (BSP)

**Used by:** bspwm, yabai (macOS)

Binary Space Partitioning recursively divides the screen into smaller rectangles using a binary tree structure.

**Data Structure:**
```
Tree Node {
    type: "internal" | "leaf"
    split_type: "horizontal" | "vertical"  // for internal nodes
    split_ratio: float (0 < r < 1)         // for internal nodes
    window: Window | null                   // for leaf nodes
    left_child: Node | null
    right_child: Node | null
    rectangle: { x, y, width, height }
}
```

**Pseudocode - Window Insertion:**
```
function insertWindow(tree, newWindow, insertionPoint):
    if tree is empty:
        tree.root = createLeafNode(newWindow, fullScreenRect)
        return

    targetNode = findLeafNode(tree, insertionPoint)

    // Create new internal node
    internalNode = createInternalNode()
    internalNode.split_type = determineSplitDirection(targetNode.rectangle)
    internalNode.split_ratio = 0.5

    // Calculate child rectangles
    (rect1, rect2) = splitRectangle(targetNode.rectangle,
                                     internalNode.split_type,
                                     internalNode.split_ratio)

    // Create children
    internalNode.left_child = createLeafNode(targetNode.window, rect1)
    internalNode.right_child = createLeafNode(newWindow, rect2)

    // Replace target with internal node
    replaceNode(tree, targetNode, internalNode)

function determineSplitDirection(rectangle):
    // "longest_side" scheme - most common
    if rectangle.width >= rectangle.height:
        return "horizontal"  // split vertically (side by side)
    else:
        return "vertical"    // split horizontally (stacked)
```

**Pseudocode - Window Removal:**
```
function removeWindow(tree, windowToRemove):
    leafNode = findLeafByWindow(tree, windowToRemove)
    parentNode = leafNode.parent

    if parentNode is null:
        // Only window in tree
        tree.root = null
        return

    // Get sibling node
    siblingNode = getSibling(parentNode, leafNode)

    // Replace parent with sibling (promote sibling up)
    grandparent = parentNode.parent
    siblingNode.rectangle = parentNode.rectangle
    replaceNode(tree, parentNode, siblingNode)

    // Recursively rebalance rectangles
    recalculateRectangles(siblingNode)
```

**Splitting Schemes:**
1. **Longest Side**: Split perpendicular to longest dimension
2. **Alternate**: Alternate between horizontal/vertical based on parent
3. **Spiral**: Creates rotating spiral pattern (clockwise/counter-clockwise)

---

### 1.2 Master-Stack Layout

**Used by:** dwm, Xmonad, Krohnkite, Hyprland

A layout with a "master" area (typically left side, larger) and a "stack" area for remaining windows.

**Data Structure:**
```
Layout {
    master_count: int           // Number of windows in master area (usually 1)
    master_factor: float        // Master area width ratio (0.0-1.0, typically 0.5-0.6)
    orientation: "left" | "right" | "top" | "bottom"
    stack_layout: "vertical" | "horizontal" | "monocle"
    windows: Window[]
}
```

**Pseudocode - Layout Calculation:**
```
function calculateMasterStack(screenRect, windows, config):
    if windows.length == 0:
        return []

    if windows.length == 1:
        // Single window takes full screen
        return [{ window: windows[0], rect: screenRect }]

    arrangements = []
    masterCount = min(config.master_count, windows.length)
    stackCount = windows.length - masterCount

    // Calculate master area
    masterWidth = screenRect.width * config.master_factor
    stackWidth = screenRect.width - masterWidth

    // Position master windows
    masterHeight = screenRect.height / masterCount
    for i in range(masterCount):
        arrangements.push({
            window: windows[i],
            rect: {
                x: screenRect.x,
                y: screenRect.y + (i * masterHeight),
                width: masterWidth,
                height: masterHeight
            }
        })

    // Position stack windows
    if stackCount > 0:
        stackHeight = screenRect.height / stackCount
        for i in range(stackCount):
            arrangements.push({
                window: windows[masterCount + i],
                rect: {
                    x: screenRect.x + masterWidth,
                    y: screenRect.y + (i * stackHeight),
                    width: stackWidth,
                    height: stackHeight
                }
            })

    return arrangements

function handleNewWindow(layout, newWindow, focused):
    if config.new_is_master:
        // New window becomes master, shift others to stack
        layout.windows.unshift(newWindow)
    else:
        // New window goes to stack (after master)
        insertPosition = max(layout.master_count, findIndex(focused) + 1)
        layout.windows.insert(insertPosition, newWindow)

    recalculateLayout(layout)
```

---

### 1.3 Fibonacci/Spiral Layout

**Used by:** dwm (patch), bspwm, Xmonad

Recursively subdivides space with each new window taking half of the remaining area.

**Pseudocode:**
```
function calculateFibonacci(screenRect, windows, pattern):
    if windows.length == 0:
        return []

    arrangements = []
    currentRect = screenRect

    for i in range(windows.length):
        if i == windows.length - 1:
            // Last window takes remaining space
            arrangements.push({ window: windows[i], rect: currentRect })
        else:
            // Split current rectangle
            splitDirection = getSplitDirection(i, pattern)
            (windowRect, remainingRect) = splitRectangle(
                currentRect,
                splitDirection,
                0.5  // Each window gets half
            )

            arrangements.push({ window: windows[i], rect: windowRect })
            currentRect = remainingRect

    return arrangements

function getSplitDirection(index, pattern):
    if pattern == "spiral":
        // Cycle: right, down, left, up (clockwise spiral)
        directions = ["horizontal", "vertical", "horizontal", "vertical"]
        return directions[index % 4]
    else if pattern == "dwindle":
        // Alternate: horizontal, vertical
        return index % 2 == 0 ? "horizontal" : "vertical"
```

**Visual Example (Spiral):**
```
+-------+-------+
|       |   2   |
|   1   +---+---+
|       | 4 | 3 |
+-------+---+---+

Window 1: 50% of screen (left)
Window 2: 25% of screen (top-right)
Window 3: 12.5% of screen (bottom-right-right)
Window 4: 12.5% of screen (bottom-right-left)
```

---

### 1.4 Monocle Layout

**Used by:** All tiling WMs

Single window visible at a time, others hidden or accessible via tabs/keybindings.

**Pseudocode:**
```
function calculateMonocle(screenRect, windows, focusedIndex):
    arrangements = []

    for i in range(windows.length):
        arrangements.push({
            window: windows[i],
            rect: screenRect,  // All windows same size
            visible: i == focusedIndex,
            z_index: i == focusedIndex ? 1 : 0
        })

    return arrangements
```

---

### 1.5 Columns/Grid Layout

**Used by:** Krohnkite, FancyWM, i3

**Three Column Layout Pseudocode:**
```
function calculateThreeColumn(screenRect, windows, config):
    if windows.length <= 2:
        return calculateMasterStack(screenRect, windows, config)

    // Calculate column widths
    leftWidth = screenRect.width * config.side_ratio
    centerWidth = screenRect.width * config.center_ratio
    rightWidth = screenRect.width * config.side_ratio

    // Distribute windows: center gets priority, then left, then right
    centerCount = 1  // Master in center
    sideCount = windows.length - 1
    leftCount = ceil(sideCount / 2)
    rightCount = floor(sideCount / 2)

    arrangements = []

    // Center column (master)
    centerX = leftWidth
    arrangements.push({
        window: windows[0],
        rect: { x: centerX, y: 0, width: centerWidth, height: screenRect.height }
    })

    // Left column
    leftHeight = screenRect.height / leftCount
    for i in range(leftCount):
        arrangements.push({
            window: windows[1 + i],
            rect: { x: 0, y: i * leftHeight, width: leftWidth, height: leftHeight }
        })

    // Right column
    rightX = leftWidth + centerWidth
    rightHeight = screenRect.height / rightCount
    for i in range(rightCount):
        arrangements.push({
            window: windows[1 + leftCount + i],
            rect: { x: rightX, y: i * rightHeight, width: rightWidth, height: rightHeight }
        })

    return arrangements
```

---

## 2. Window Manager Implementations

### 2.1 i3 Window Manager

**Architecture:** Tree-based container system

**Key Concepts:**
- **Containers**: Building blocks that hold windows or other containers
- **Split Containers**: Hold multiple children with orientation (H/V)
- **Leaf Containers**: Hold exactly one X11 window
- **Workspaces**: Top-level containers

**Tree Structure:**
```
Root (X11 root)
└── Output (Monitor)
    └── Content
        └── Workspace
            └── Split Container (H)
                ├── Window A
                └── Split Container (V)
                    ├── Window B
                    └── Window C
```

**Autotiling Behavior:**
i3 does not natively autotile. The [autotiling](https://github.com/nwg-piotr/autotiling) script adds this:

```python
# Simplified autotiling logic
def on_window_focus(window):
    rect = window.rect
    if rect.width >= rect.height:
        i3.command("split horizontal")
    else:
        i3.command("split vertical")
```

**Layout Modes:**
- `splith`: Horizontal split (windows side-by-side)
- `splitv`: Vertical split (windows stacked)
- `stacking`: Single window visible with title bar list
- `tabbed`: Single window with tab bar

---

### 2.2 Sway (Wayland)

**Architecture:** Drop-in i3 replacement for Wayland

Uses identical tree structure and commands as i3. The autotiling script works with both:

```bash
# Enable autotiling in sway config
exec_always autotiling
```

**Key Addition:** Native Wayland support with wlroots compositor library.

---

### 2.3 dwm (Dynamic Window Manager)

**Architecture:** Tag-based with multiple layout algorithms

**Default Layout Algorithm:**
```c
void tile(Monitor *m) {
    unsigned int n = 0;
    Client *c;

    // Count tiled windows
    for (c = m->clients; c; c = c->next)
        if (ISVISIBLE(c) && !c->isfloating)
            n++;

    if (n == 0) return;

    // Calculate master area
    unsigned int mw = m->mfact * m->ww;  // Master width
    unsigned int ty = 0;  // Stack y position

    for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
        if (i < m->nmaster) {
            // Master area
            h = (m->wh - my) / (MIN(n, m->nmaster) - i);
            resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw));
            my += HEIGHT(c);
        } else {
            // Stack area
            h = (m->wh - ty) / (n - i);
            resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw));
            ty += HEIGHT(c);
        }
    }
}
```

**Available Layouts:**
- `tile`: Master-stack (default)
- `monocle`: Full-screen single window
- `float`: Traditional floating
- `fibonacci/spiral`: Via patch
- `grid`: Via patch
- `bstack`: Bottom stack variant

---

### 2.4 bspwm

**Architecture:** Pure binary space partitioning

**Core Principles:**
1. Every window is a leaf in a binary tree
2. Internal nodes define splits (type + ratio)
3. Monitors contain desktops, desktops contain trees

**Insertion Modes:**

| Mode | Behavior |
|------|----------|
| Automatic (longest_side) | Split perpendicular to longest dimension |
| Automatic (alternate) | Alternate H/V based on parent |
| Automatic (spiral) | Rotating spiral pattern |
| Manual (preselection) | User specifies direction before opening window |

**Configuration (bspwmrc):**
```bash
# Split ratio
bspc config split_ratio 0.52

# Automatic scheme
bspc config automatic_scheme longest_side

# Initial polarity
bspc config initial_polarity first_child

# Window gaps
bspc config window_gap 12
bspc config border_width 2
```

---

### 2.5 Bismuth (KDE)

**Architecture:** KWin script (TypeScript/QML)

**Key Features:**
- Native KDE Plasma integration
- Virtual Desktop and Activity support
- Multiple layouts per workspace
- Window decorations (border-only style)

**Supported Layouts:**
- Tile (Master-Stack)
- Monocle
- Spread
- Stair
- Three Column
- Floating

**Configuration Location:** System Settings > Window Management > Window Tiling

**Why Users Loved It:**
1. Automatic tiling without complex config files
2. Native KDE integration (not a separate WM)
3. Keyboard-driven workflow similar to i3/dwm
4. Easy to enable/disable per-window or workspace
5. Beautiful integration with Plasma themes

---

### 2.6 Krohnkite (KWin)

**Architecture:** KWin script (JavaScript)

**Supported Layouts:**
```javascript
const layouts = [
    "tilelayout",      // Master-stack
    "monoclelayout",   // Single window
    "columns",         // Equal columns
    "threecolumnlayout", // Three column with center master
    "spreadlayout",    // Spread/cascade
    "stairlayout",     // Stair steps
    "spirallayout",    // Fibonacci spiral
    "stackedlayout",   // Tabbed stacking
    "floatinglayout",  // No tiling
    "btreelayout",     // Binary tree
    "quarterlayout"    // Quarter tiles
];
```

**Configuration Format:**
```
OutputName:ActivityId:VirtualDesktopName:layoutName
```

Example:
```
HDMI-A-1:99a12h44-e9a6-1142-55eedaa7:Desktop 1:tile
DP-2:::columns
```

**Key Shortcuts:**
| Action | Default Binding |
|--------|-----------------|
| Focus next | Meta + J |
| Focus previous | Meta + K |
| Move window down | Meta + Shift + J |
| Move window up | Meta + Shift + K |
| Toggle floating | Meta + F |
| Cycle layouts | Meta + \ |
| Set as master | Meta + Return |

---

## 3. User Interaction Patterns

### 3.1 Common Keyboard Shortcuts

**Navigation:**
| Action | i3/Sway | dwm | Krohnkite |
|--------|---------|-----|-----------|
| Focus left | Mod+Left/H | Mod+J | Meta+H |
| Focus right | Mod+Right/L | Mod+K | Meta+L |
| Focus up | Mod+Up/K | - | Meta+K |
| Focus down | Mod+Down/J | - | Meta+J |
| Focus next | Mod+Tab | Mod+J | Meta+J |
| Focus previous | Mod+Shift+Tab | Mod+K | Meta+K |

**Window Movement:**
| Action | i3/Sway | dwm | Krohnkite |
|--------|---------|-----|-----------|
| Move left | Mod+Shift+Left | - | Meta+Shift+H |
| Move right | Mod+Shift+Right | - | Meta+Shift+L |
| Move to master | - | Mod+Return | Meta+Return |
| Swap with next | - | Mod+Shift+J | Meta+Shift+J |

**Layout Control:**
| Action | i3/Sway | dwm | Krohnkite |
|--------|---------|-----|-----------|
| Toggle split | Mod+E | - | - |
| Horizontal split | Mod+H | - | - |
| Vertical split | Mod+V | - | - |
| Cycle layouts | - | Mod+Space | Meta+\ |
| Toggle floating | Mod+Shift+Space | Mod+Shift+Space | Meta+F |
| Toggle fullscreen | Mod+F | Mod+M | Meta+M |

**Resizing:**
| Action | i3/Sway | dwm | GlazeWM |
|--------|---------|-----|---------|
| Resize mode | Mod+R | - | Mod+R |
| Grow width | Right/L | Mod+L | resize --width +2% |
| Shrink width | Left/H | Mod+H | resize --width -2% |
| Grow height | Down/J | - | resize --height +2% |
| Shrink height | Up/K | - | resize --height -2% |

### 3.2 Mouse Interactions

**Common Patterns:**
1. **Drag to move**: Mod+Left-click drag moves floating windows
2. **Drag to resize**: Mod+Right-click drag resizes windows
3. **Edge snapping**: Drag to screen edge for tiling (FancyZones style)
4. **Focus follows mouse**: Optional cursor-based focus

**Resize Between Tiled Windows:**
- AquaSnap: Ctrl+drag border resizes adjacent windows
- GlazeWM: Inner gaps define resize handles
- i3: Resize mode with arrow keys

### 3.3 Master Window Concept

The "master" is a special designation:
- **Larger allocation**: Typically 50-65% of screen width
- **Primary focus**: Main working window
- **Promotion/Demotion**: Windows can be promoted to master
- **Count configurable**: Some layouts support multiple masters

**Common Actions:**
```
Mod+Return     - Swap focused window with master
Mod+I          - Increase master count
Mod+D          - Decrease master count
Mod+L          - Increase master width
Mod+H          - Decrease master width
```

---

## 4. Configuration Options

### 4.1 Layout Configuration

| Option | Description | Typical Values |
|--------|-------------|----------------|
| `split_ratio` | Default split ratio | 0.5 - 0.65 |
| `master_factor` | Master area ratio | 0.5 - 0.6 |
| `master_count` | Windows in master area | 1 - 3 |
| `gaps_inner` | Gap between windows | 0 - 20px |
| `gaps_outer` | Gap to screen edge | 0 - 20px |
| `border_width` | Window border width | 1 - 4px |
| `new_is_master` | New window becomes master | true/false |
| `smart_gaps` | Hide gaps with single window | true/false |
| `smart_borders` | Hide borders with single window | true/false |

### 4.2 Per-Window Rules

**Example Configurations:**

**i3 (for_window):**
```
for_window [class="Firefox"] floating enable
for_window [class="Spotify"] move to workspace 10
for_window [title="Calculator"] floating enable, resize set 400 300
```

**bspwm (bspc rule):**
```bash
bspc rule -a Firefox desktop='^2'
bspc rule -a Spotify state=floating
bspc rule -a Gimp state=floating follow=on
```

**Krohnkite (KWin rules):**
- System Settings > Window Management > Window Rules
- Match by window class, title, or role
- Apply actions: floating, no-tile, specific workspace

### 4.3 Multi-Monitor Configuration

| Feature | i3 | bspwm | Krohnkite |
|---------|-----|-------|-----------|
| Independent layouts | Yes | Yes | Yes |
| Window move between | Mod+Shift+Arrow | bspc node -m | Meta+Shift+Arrow |
| Focus follow cursor | Optional | Optional | Optional |
| Workspace per monitor | Configurable | Default | Per-Activity |

---

## 5. Key Features Analysis

### 5.1 Why Bismuth/Krohnkite Were Popular

**1. Zero-Config Usability**
- Works immediately after installation
- No config files to write
- GUI-based configuration

**2. KDE Integration**
- Respects Plasma panels and docks
- Works with Activities and Virtual Desktops
- Native window decorations option

**3. Familiar Controls**
- i3/dwm-like keybindings
- Intuitive mouse support
- Easy float toggle for stubborn apps

**4. Visual Polish**
- Smooth animations
- Gap support
- Border-only window decoration theme

**5. Flexibility**
- Per-workspace layouts
- Per-monitor configuration
- Window class exceptions

### 5.2 Common User Complaints (Opportunities)

| Issue | Description | Solution Approach |
|-------|-------------|-------------------|
| Dialog windows | Dialogs tiled instead of floating | Auto-detect transient windows |
| Resize jumpy | Resizing causes layout reflow | Smooth resize with preview |
| No manual override | Can't place window where wanted | Allow manual zone selection |
| Multi-monitor gaps | Inconsistent gaps across monitors | Normalize gap calculation |
| Performance | Lag on window open/close | Batch layout calculations |

### 5.3 Feature Comparison Matrix

| Feature | i3 | dwm | bspwm | Bismuth | Krohnkite | FancyZones |
|---------|-----|-----|-------|---------|-----------|------------|
| Auto-tile | Script | Yes | Yes | Yes | Yes | No |
| Manual zones | No | No | No | No | No | Yes |
| Layout presets | No | No | No | Limited | Yes | Yes |
| Custom zones | No | No | No | No | No | Yes |
| Drag to zone | No | No | No | No | No | Yes |
| Per-monitor | Yes | Yes | Yes | Yes | Yes | Yes |
| Gap config | Yes | Patch | Yes | Yes | Yes | Limited |
| Snap assist | No | No | No | No | No | Yes |

---

## 6. Recommendations for PlasmaZones

### 6.1 Hybrid Approach

Combine the best of both worlds:

1. **Keep Manual Zone Definition** (FancyZones strength)
   - Users define custom layouts visually
   - Templates for common patterns (grid, columns, etc.)
   - Save/load layout presets

2. **Add Autotiling Mode** (TWM strength)
   - BSP algorithm for automatic subdivision
   - Master-Stack layout option
   - Fibonacci/Spiral layout option

3. **Smart Mode Switching**
   - Autotile within defined zones
   - Fall back to manual when dragging
   - Per-zone autotile settings

### 6.2 Proposed Algorithm: Zone-Aware BSP

```
function autotileInZone(zone, windows):
    if zone.autotile_mode == "bsp":
        return bspLayout(zone.rect, windows)
    else if zone.autotile_mode == "master-stack":
        return masterStackLayout(zone.rect, windows, zone.master_config)
    else if zone.autotile_mode == "columns":
        return columnsLayout(zone.rect, windows, zone.column_count)
    else:
        // Manual mode - no auto arrangement
        return currentArrangement(zone)
```

### 6.3 Suggested Configuration Options

```json
{
  "autotiling": {
    "enabled": true,
    "default_algorithm": "bsp",
    "split_ratio": 0.5,
    "new_window_position": "after_focused",
    "gaps": {
      "inner": 8,
      "outer": 8
    }
  },
  "master_stack": {
    "master_count": 1,
    "master_factor": 0.55,
    "orientation": "left",
    "new_is_master": false
  },
  "keyboard": {
    "modifier": "Meta",
    "focus_next": "J",
    "focus_prev": "K",
    "move_next": "Shift+J",
    "move_prev": "Shift+K",
    "toggle_float": "F",
    "cycle_layout": "\\",
    "promote_master": "Return"
  },
  "per_zone_settings": {
    "zone_1": { "algorithm": "master-stack" },
    "zone_2": { "algorithm": "columns", "count": 2 },
    "zone_3": { "algorithm": "manual" }
  }
}
```

### 6.4 Key Keyboard Shortcuts to Implement

**Essential (Phase 1):**
- Focus navigation (H/J/K/L or arrows)
- Move window in layout
- Toggle floating
- Cycle layouts

**Advanced (Phase 2):**
- Resize focused window
- Promote to master
- Change split ratio
- Layout-specific controls

### 6.5 Window Event Handling

```
on window_opened(window):
    zone = detectTargetZone(window)
    if zone.autotile_enabled:
        zone.windows.add(window)
        recalculateZoneLayout(zone)
    else:
        // Manual mode - respect drag position or use zone default

on window_closed(window):
    zone = findZoneContaining(window)
    if zone and zone.autotile_enabled:
        zone.windows.remove(window)
        recalculateZoneLayout(zone)

on window_moved(window, new_position):
    old_zone = findZoneContaining(window)
    new_zone = detectZoneAt(new_position)

    if old_zone != new_zone:
        old_zone.windows.remove(window)
        new_zone.windows.add(window)
        recalculateZoneLayout(old_zone)
        recalculateZoneLayout(new_zone)
```

---

## Sources

### Official Documentation
- [i3 Window Manager](https://i3wm.org/)
- [i3 User's Guide - Tree Structure](https://i3wm.org/docs/userguide.html)
- [bspwm GitHub Repository](https://github.com/baskerville/bspwm)
- [dwm Fibonacci Patch](https://dwm.suckless.org/patches/fibonacci/)
- [Sway Window Manager](https://swaywm.org/)

### KDE Tiling Extensions
- [Bismuth GitHub](https://github.com/Bismuth-Forge/bismuth)
- [Krohnkite GitHub](https://github.com/esjeon/krohnkite)
- [Krohnkite KDE Store](https://store.kde.org/p/1281790/)

### Autotiling Scripts
- [nwg-piotr/autotiling](https://github.com/nwg-piotr/autotiling)
- [chmln/i3-auto-layout](https://github.com/chmln/i3-auto-layout)

### Related Window Managers
- [GlazeWM (Windows)](https://github.com/glzr-io/glazewm)
- [FancyWM (Windows)](https://github.com/FancyWM/fancywm)
- [hyprNStack (Hyprland)](https://github.com/zakk4223/hyprNStack)

### Articles and Guides
- [Master-stack layout for i3](https://goral.net.pl/post/i3a/)
- [Arch Wiki - i3](https://wiki.archlinux.org/title/I3)
- [Arch Wiki - Comparison of Tiling WMs](https://wiki.archlinux.org/title/Comparison_of_tiling_window_managers)

---

*Research compiled for PlasmaZones autotiling feature development*
*Last updated: 2026-01-24*
