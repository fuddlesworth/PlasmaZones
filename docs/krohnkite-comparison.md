<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: CC0-1.0
-->
# PlasmaZones vs Krohnkite: Feature Comparison

## Overview

| | PlasmaZones | Krohnkite (Codeberg fork) |
|---|---|---|
| **Type** | Native C++ KWin Effect + D-Bus Daemon | KWin Script (TypeScript) |
| **Version** | 1.8.2 | Active (733 commits, Dec 2025) |
| **KDE Support** | Plasma 6 / KWin 6 | KWin 6 only |
| **Architecture** | Zone-based (FancyZones-style) | Dynamic tiling (dwm-style) |
| **License** | GPL-3.0 | MIT |

---

## Tiling Layouts

| Layout | PlasmaZones | Krohnkite |
|--------|:-----------:|:---------:|
| Master-Stack (Tile) | Built-in template | Dynamic algorithm |
| Fibonacci / Spiral | Built-in template | Dynamic algorithm |
| BSP (Binary Tree) | Built-in template | Dynamic algorithm |
| Columns (2, 3, N) | Built-in templates | Dynamic algorithm |
| Rows (2, N) | Built-in templates | - |
| Grid (2x2, 3x2) | Built-in templates | - |
| Three Column | - | Dynamic algorithm |
| Monocle | - | Dynamic algorithm |
| Focus / Split Focus | Built-in templates | - |
| Wide (center primary) | Built-in template | - |
| Priority Grid | Built-in template | - |
| Spread | - | Dynamic algorithm |
| Stair | - | Dynamic algorithm |
| Stacked | - | Dynamic algorithm |
| Cascade | - | Dynamic algorithm |
| Quarter | - | Dynamic algorithm |
| Floating (disable tiling) | - | Toggle per desktop |
| **Custom user-drawn** | **Unlimited via editor** | **Not possible** |

**Key difference:** PlasmaZones layouts are static zone templates that define fixed regions. Krohnkite layouts are dynamic algorithms that generate window positions based on current window count. PlasmaZones has 12 built-in templates; Krohnkite has 12 dynamic algorithms.

---

## Window Placement

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Manual drag-to-zone | Yes (modifier key) | - |
| Auto-assign to first empty zone | Yes (`autoAssign` per layout) | - |
| Auto-place all new windows (dynamic) | - | Yes (always on) |
| Per-app zone rules | Yes (pattern -> zone number) | - |
| Per-app screen targeting | Yes (app rule `targetScreen`) | - |
| Snap to last used zone | Yes (`moveNewWindowsToLastZone`) | - |
| Snap all windows shortcut | Yes | - |
| Master window promotion | - | Yes (Meta+Return) |
| New window position control | - | Yes (`newWindowPosition`) |
| New window as master | - | Yes (`newWindowAsMaster`) |
| Layout reflow on window add/remove | - | Yes (automatic) |
| Layout capacity / overflow | - | Yes (per-layout capacity) |
| Overflow to other screen | - | Yes (`surfacesIsMoveWindows`) |
| Overflow oldest windows | - | Yes (`surfacesIsMoveOldestWindows`) |

---

## Keyboard Navigation

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Move window left/right/up/down | Yes | Yes |
| Focus zone left/right/up/down | Yes | Yes |
| Swap windows between zones | Yes | Yes (via move) |
| Push to first empty zone | Yes | - |
| Snap to zone by number (1-9) | Yes | - |
| Rotate windows clockwise/CCW | Yes | - |
| Cycle windows within zone | Yes | - |
| Resnap to current layout | Yes | - |
| Quick layout switch (Meta+1-9) | Yes | - |
| Layout cycle next/prev | Yes | Yes |
| Switch to specific layout by name | - | Yes (Meta+T, Meta+M) |
| Increase/decrease master area | - | Yes (Meta+I/D) |
| Toggle floating per window | Yes | Yes |
| Restore original window size | Yes | - |
| Meta Mode (temp keybindings) | - | Yes (with timeout) |
| Move window to next/prev screen | - | Yes |
| Focus next/prev screen | - | Yes |

---

## Window Filtering & Exclusion

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Exclude by window class | Yes | Yes |
| Exclude by app name | Yes | - |
| Exclude by window title | - | Yes |
| Exclude by window role | - | Yes |
| Exclude by activity | - | Yes |
| Exclude by screen | - | Yes |
| Exclude by virtual desktop | - | Yes |
| Float by window class | - | Yes |
| Float by window title | - | Yes |
| Float all by default (opt-in tiling) | - | Yes (`tileNothing`) |
| Tiling whitelist (opt-in) | - | Yes (`tilingClass`) |
| Auto-float utility windows | - | Yes (`floatUtility`) |
| Auto-float oversized windows | - | Yes (`unfitGreater`) |
| Auto-float undersized windows | - | Yes (`unfitLess`) |
| Exclude transient windows | Yes | - |
| Minimum window size filter | Yes (width + height) | - |
| Interactive window picker (KCM) | Yes | - |
| Disabled monitors list | Yes | - |
| Debug new windows (log class/role) | - | Yes |

---

## Gap & Spacing Management

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Gap between zones (inner) | Yes (`zonePadding`, global) | Yes (`screenGapBetween`) |
| Outer gap (screen edges) | Yes (`outerGap`, global) | Yes (L/R/T/B independent) |
| Per-layout gap override | Yes | - |
| Per-zone gap override | - | - |
| Per-screen gap override | - | Yes (`gapsOverrideConfig`) |
| Sole window no gaps | - | Yes (`soleWindowNoGaps`) |
| Sole window custom size | - | Yes (width/height %) |

---

## Multi-Monitor Support

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Per-screen layouts | Yes (per screen/desktop/activity) | Yes (per screen/desktop/activity) |
| Show zones on all monitors | Yes (configurable) | N/A (always tiling) |
| Disabled monitors list | Yes | Yes (`ignoreScreen`) |
| Independent zone definitions | Yes (native) | Yes (separate tiling per screen) |
| Per-screen layout assignment | Yes (KCM UI) | Yes (config string format) |
| Separate screen focus required | No | Yes (KWin setting) |
| Window overflow between screens | - | Yes (`surfacesIsMoveWindows`) |
| Per-output sole window override | - | Yes |

---

## Layout Management

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Visual zone editor | Yes (full GUI) | - |
| Drag-to-resize zones | Yes | - |
| Zone splitting | Yes | - |
| Zone duplication | Yes | - |
| Auto-fill on zone delete | Yes (`ZoneAutoFiller`) | - |
| Undo/redo in editor | Yes (45+ command types) | - |
| Layout import/export | Yes | - |
| Built-in templates | Yes (12) | - |
| Layout assignment per desktop | Yes | Yes |
| Layout assignment per activity | Yes | Yes |
| Layout assignment per screen | Yes | Yes |
| Quick layout slots (1-9) | Yes | - |
| Layout cycling | Yes | Yes |
| Layout reorder | - | Yes (per-layout order) |
| Enable/disable layouts | - | Yes (per-layout toggle) |
| Layout capacity limits | - | Yes |
| Layout rotation angle | - | Yes (tile/columns) |

---

## Window State Management

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Session persistence (survive restart) | Yes (KConfig) | - |
| Restore windows on login | Yes (`restoreWindowsToZonesOnLogin`) | - |
| Pre-snap geometry storage | Yes (restore original size) | - |
| Floating window tracking | Yes | Yes |
| Sticky window handling | Yes (3 modes) | - |
| Keep windows in zones on resolution change | Yes | - |
| Multi-zone snap (span zones) | Yes | - |
| Zone selector (edge-triggered picker) | Yes (full GUI) | - |
| Window-to-zone assignment tracking | Yes (D-Bus queryable) | Internal only |
| Resnap on layout change | Yes (zone-number mapping) | Yes (automatic reflow) |

---

## Visual Customization

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Zone highlight colors | Yes (per-zone or global) | - |
| Inactive zone colors | Yes | - |
| Border color/width/radius | Yes (per-zone) | - |
| Zone opacity (active/inactive) | Yes | - |
| Background blur | Yes | - |
| Label font customization | Yes (family, size, weight, color) | - |
| Shader effects (14 GPU effects) | Yes | - |
| Audio visualizer shader | Yes | - |
| System color integration | Yes (`useSystemColors`) | - |
| OSD on layout switch | Yes (text or preview) | Yes (basic notification) |
| Flash zones on switch | Yes | - |
| No tile borders option | - | Yes (`noTileBorder`) |
| Window layer control | - | Yes (tiled/float layers) |

---

## Dock Support

| Feature | PlasmaZones | Krohnkite |
|---------|:-----------:|:---------:|
| Dock windows per edge | - | Yes (L/T/R/B) |
| Dock size (% of screen) | - | Yes |
| Dock gaps and alignment | - | Yes |
| Dock order per edge | - | Yes |
| Per-class dock config | - | Yes |
| Per-surface dock config | - | Yes |

---

## Architecture & Stability

| Aspect | PlasmaZones | Krohnkite |
|--------|:-----------:|:---------:|
| Implementation | Native C++ KWin Effect | KWin Script (TypeScript) |
| IPC | D-Bus (6 interfaces) | KWin Script API |
| Config changes | Hot-reload | Requires reboot |
| Survives KDE updates | Yes (compiled plugin) | Risk of API breakage |
| Survives sleep/wake | Yes | Known issues |
| Multiple instance bug | No | Yes (toggle on/off) |
| Config UI | Full KCM (System Settings) | KWin Script settings |
| Scripting / automation | D-Bus API | - |
| Build system | CMake (C++20, Qt 6.6+) | npm + go-task |

---

## Features PlasmaZones Has That Krohnkite Doesn't

1. **Visual zone editor** with drag-to-resize, split, duplicate, undo/redo
2. **Per-app zone rules** (pin specific apps to specific zones)
3. **Multi-zone snap** (span a window across multiple zones)
4. **Session persistence** (window positions survive restarts)
5. **14 GPU shader effects** for zone visualization
6. **Zone selector** (edge-triggered layout picker during drag)
7. **Per-zone appearance customization** (colors, opacity, borders per zone)
8. **Snap to zone by number** (1-9 keyboard shortcuts)
9. **Window rotation** (clockwise/counterclockwise between zones)
10. **Cycle windows within zone** (monocle-style in any zone)
11. **Pre-snap geometry restore** (return to original size)
12. **Layout import/export** and shareable templates
13. **Hot-reload configuration** (no reboot needed)
14. **D-Bus API** for external scripting and automation
15. **Resolution change handling** (reposition windows when screen changes)
16. **Background blur** and system color integration
17. **Label font customization** (family, size, weight, italic, color)
18. **Sticky window handling** (3 configurable modes)
19. **OSD with layout preview** (not just text notification)
20. **Interactive window class picker** in settings (point-and-click exclusion)

## Features Krohnkite Has That PlasmaZones Doesn't

1. **Dynamic zone generation** (zones created/destroyed based on window count)
2. **Automatic layout reflow** (all windows reposition when one is added/removed)
3. **Master window concept** (promote any window to primary position)
4. **Master area resize** (Meta+I/D to grow/shrink master)
5. **Layout capacity** (max windows per layout, overflow behavior)
6. **Window overflow to other screens** (when layout is full)
7. **Dock system** (per-edge dock windows with size/gap/alignment)
8. **Monocle layout** (single fullscreen window cycling)
9. **Spread / Stair / Cascade / Stacked layouts**
10. **Three Column layout** (designed for ultrawide)
11. **Meta Mode** (temporary keybinding mode with timeout)
12. **Opt-in tiling** (`tileNothing` + whitelist)
13. **Auto-float utility windows** by type
14. **Auto-float over/undersized windows**
15. **Filter by window role, title, virtual desktop**
16. **Balanced columns** (new windows go to shortest column)
17. **Sole window controls** (custom size, no borders/gaps)
18. **Per-screen gap overrides**
19. **Focus mode switching** (dwm-style vs spatial)
20. **New window insertion position control**
21. **Float window initial size and randomized positioning**
22. **Move pointer on focus**
23. **Keep tiling on drag**
24. **Per-layout enable/disable toggle**
25. **Layout ordering** (custom cycle order)

---

## Gap Analysis: What PlasmaZones Needs for Auto-Tiling Parity

### Must-Have (Core Auto-Tiling)

| Priority | Feature | Complexity | Notes |
|----------|---------|------------|-------|
| P0 | Dynamic zone generation algorithms | High | New `TilingAlgorithm` engine |
| P0 | Layout reflow on window add/remove | High | Window lifecycle hooks in KWin effect |
| P0 | Auto-tile mode toggle (per layout) | Medium | Extend `LayoutCategory` enum |
| P0 | Master window concept + promotion | Medium | New property on zone/window tracking |
| P0 | Master area ratio resize | Medium | Shortcut + algorithm parameter |
| P0 | Monocle layout algorithm | Low | Single-zone, cycle windows within it |

### High Priority (Competitive Parity)

| Priority | Feature | Complexity | Notes |
|----------|---------|------------|-------|
| P1 | Layout capacity + overflow | Medium | Per-layout max window count |
| P1 | Window overflow to other screen | Medium | Extends capacity system |
| P1 | Three column algorithm | Low | New tiling algorithm variant |
| P1 | Sole window maximize | Low | Single window = maximize |
| P1 | Filter by window title/role | Low | Extend existing exclusion system |
| P1 | Opt-in tiling mode | Low | Invert exclusion logic |

### Medium Priority (User Experience)

| Priority | Feature | Complexity | Notes |
|----------|---------|------------|-------|
| P2 | Per-screen gap overrides | Low | Already have per-layout overrides |
| P2 | Auto-float utility windows | Low | Check window type in effect |
| P2 | Focus mode switching | Low | Already have directional focus |
| P2 | Balanced columns | Low | Algorithm variant |
| P2 | Per-layout enable/disable | Low | New property on Layout |
| P2 | Layout ordering for cycle | Low | New property on Layout |

### Low Priority (Nice-to-Have)

| Priority | Feature | Complexity | Notes |
|----------|---------|------------|-------|
| P3 | Dock system | High | New subsystem |
| P3 | Spread/Stair/Cascade layouts | Medium | Floating-style algorithms |
| P3 | Meta Mode | Medium | Temporary shortcut context |
| P3 | Float initial size/position | Low | New settings |
| P3 | Move pointer on focus | Low | KWin cursor API |
| P3 | Keep tiling on drag | Low | Drag behavior flag |

---

## Strategic Positioning

### PlasmaZones' Unique Advantage

PlasmaZones can offer something no pure dynamic tiler can: **hybrid tiling**. Users can:

1. **Draw custom layouts** that no algorithm can produce
2. **Pin specific apps** to specific zones (Krohnkite can't do this at all)
3. **Enable auto-tiling** on layouts that dynamically fill zones as windows open
4. **Mix manual and auto zones** in the same layout (future potential)
5. **Keep session state** across restarts (Krohnkite loses everything)

### Target Message

> "PlasmaZones: The stability of a native KWin effect, the customization of a visual zone editor, and now the automation of dynamic tiling. Everything Krohnkite does, without the script breakage."

### Key Migration Hooks for Krohnkite Users

1. Built-in layout presets that mirror Krohnkite's Tile, Spiral, Three Column
2. Familiar keyboard shortcuts (can match Krohnkite's defaults)
3. Auto-tile mode that "just works" like Krohnkite but with zone-based persistence
4. Per-app rules as a clear upgrade over Krohnkite's purely algorithmic placement
5. No more reboots for config changes
6. No more breakage on KDE updates
