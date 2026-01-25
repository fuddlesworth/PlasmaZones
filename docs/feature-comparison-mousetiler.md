# Feature Comparison: PlasmaZones vs MouseTiler

This document compares PlasmaZones with [MouseTiler](https://github.com/rxappdev/MouseTiler) to identify feature gaps, advantages, and potential enhancements.

**Date:** 2026-01-22

---

## Overview

MouseTiler is a KWin script for KDE Plasma 6+ that bills itself as "the fastest, simplest tiler for KDE Plasma 6+ that gives you full freedom at your fingertip." It focuses on mouse-driven window tiling with minimal keyboard shortcuts.

**Key Philosophy Differences:**
- **MouseTiler**: Emphasizes mouse-first interaction, minimal keyboard shortcuts, text-based configuration
- **PlasmaZones**: Provides both mouse and keyboard interaction, WYSIWYG editor, comprehensive settings UI

---

## Features We Already Have (Shared Features)

| Feature | PlasmaZones | MouseTiler | Notes |
|---------|-------------|------------|-------|
| Zone Overlay | ✅ `OverlayService` | ✅ "Overlay Tiler" | Both show fullscreen zone overlay during drag |
| Multi-zone snap (spanning) | ✅ `combineZoneGeometries()` | ✅ Built-in | Both allow snapping across multiple zones |
| Multiple Layouts | ✅ Full `LayoutManager` | ✅ Predefined + custom | Both support multiple layouts |
| Multi-monitor support | ✅ `layoutForScreen()` | ✅ Supported | Both work across monitors |
| System theme colors | ✅ Kirigami/KDE integration | ✅ Theme integration | Both follow system theme |
| Custom zone layouts | ✅ Visual editor | ✅ Text-based config | Different approaches, same capability |
| Edge/trigger snapping | ✅ Configurable trigger distance | ✅ Small mouse movement | Both trigger on movement near edges |

---

## Features We Have That MouseTiler Doesn't

| Feature | Status | Our Advantage |
|---------|--------|---------------|
| **WYSIWYG Layout Editor** | ✅ | MouseTiler requires text-based configuration; we have full visual drag-and-drop editor |
| **Per-virtual-desktop layouts** | ✅ | MouseTiler has no per-desktop layout support (planned feature) |
| **Window geometry memory** | ✅ `preSnapGeometry` | No explicit support for restoring original window size |
| **Window exclusion by class** | ✅ `excludedWindowClasses` | No app filtering/exclusion system |
| **Configurable polling rate** | ✅ `pollIntervalMs` | No equivalent configuration |
| **Adjacent zone detection** | ✅ `adjacentZones()` | No directional zone navigation |
| **Layout cycling shortcuts** | ✅ Previous/Next layout | No layout switching shortcuts |
| **Quick layout shortcuts (1-9)** | ✅ Direct layout switch | No numbered layout shortcuts |
| **OSD messages** | ✅ Layout name display | No on-screen display for feedback |
| **Auto-snap new windows** | ✅ "Move new windows to last used zone" | Planned but not implemented |
| **Keep windows in zones on resolution change** | ✅ Setting available | No equivalent feature |
| **Track windows in zones** | ✅ `getWindowsInZone()` | No zone occupancy tracking |
| **Keyboard shortcuts for window movement** | ⚠️ Adjacent zone detection ready | No keyboard window movement at all |
| **Per-screen layouts** | ✅ Full support | Planned but not implemented |
| **KDE Plasma 5 support** | ✅ Compatible | Plasma 6+ only |
| **GUI Settings interface** | ✅ Full KCM | Text config only (GUI planned) |

---

## Features MouseTiler Has That We Could Consider

### 1. Grid Tiler (Popup Grid Mode)
**Description:**
MouseTiler's "Grid Tiler" shows a small popup grid when moving a window just a few pixels, allowing quick zone selection without a fullscreen overlay.

**Current State in PlasmaZones:**
- We have `ZoneSelectorController` that appears at top of screen
- Our overlay is fullscreen only

**Potential Enhancement:**
- Could add a compact popup grid mode as alternative to fullscreen overlay
- Useful for quick placement without visual clutter

**Priority:** Low - Our zone selector already provides similar quick-access functionality

---

### 2. Input Method Toggle (Mouse/Stylus/Touch/Wacom)
**Description:**
MouseTiler supports Ctrl+Alt+I to toggle between different input devices (mouse, stylus, touch, Wacom).

**Current State in PlasmaZones:**
- We support mouse dragging
- Touch support depends on Qt/KWin handling

**Potential Enhancement:**
- Explicit input device mode switching
- Better stylus/tablet support

**Priority:** Low - Niche use case, Qt handles most input abstraction

---

### 3. Center-in-Tile Functionality
**Description:**
MouseTiler has an option to center windows within tiles rather than filling them completely.

**Current State in PlasmaZones:**
- Windows fill the zone geometry completely
- No centering option

**Potential Enhancement:**
- Add zone property: `fillMode: "fill" | "center" | "contain"`
- When centered, window maintains aspect ratio and centers in zone
- Could specify padding/margin around centered window

**Priority:** Medium - Could be useful for specific workflows

---

## MouseTiler Limitations We Don't Have

| Limitation | MouseTiler | PlasmaZones |
|------------|------------|-------------|
| **Config reload required** | Must untick/retick script after settings change | Settings apply immediately |
| **No visual editor** | Text configuration only | Full WYSIWYG editor |
| **Plasma 6+ only** | No older Plasma support | Works on Plasma 5+ |
| **No layout names in UI** | Issue #9 requests this | Layouts have names and display OSD |
| **Fractional scaling issues** | Issue #11 on ultra-wide 125% | Better fractional scaling handling |
| **Maximize bug** | Issue #6 - fullscreen doesn't maximize | Proper geometry application |
| **No keyboard navigation** | Mouse-only philosophy | Full keyboard shortcut support |
| **No auto-tiling rules** | Planned but not implemented | "Move new windows to zone" feature |

---

## Known MouseTiler Issues (As of Jan 2026)

From [GitHub Issues](https://github.com/rxappdev/MouseTiler/issues):

1. **#11** - Layout margins on Ultra-wide (5120x1440) with 125% fractional scaling on Wayland
2. **#9** - Request for layout names/labels
3. **#7** - Documentation improvements needed
4. **#6** - Whole-screen tiling doesn't maximize window properly

---

## Competitive Analysis Summary

### Where PlasmaZones Excels:
1. **Configuration UX** - Visual editor vs text config
2. **Keyboard Integration** - Full shortcut support
3. **Feature Completeness** - More mature feature set
4. **Stability** - Settings apply immediately, better scaling support
5. **Backwards Compatibility** - Plasma 5 support

### Where MouseTiler Excels:
1. **Simplicity** - Minimal learning curve for basic tiling
2. **Mouse-first Design** - Optimized for pointer-only workflows
3. **Lightweight** - Single KWin script, minimal complexity
4. **Quick Setup** - Works out of box with sensible defaults

### Our Competitive Position:
PlasmaZones is the more **feature-complete** and **power-user oriented** solution, while MouseTiler targets users who want **simple, mouse-driven tiling** without configuration overhead.

---

## Potential Enhancements from This Analysis

### Worth Considering:
1. **Compact popup grid mode** - Alternative to fullscreen overlay for quick placement
2. **Center-in-tile option** - For workflows requiring centered windows
3. **Simplified "quick start" mode** - Preset layouts that work out of box

### Not Prioritizing:
1. **Input device toggle** - Qt handles input abstraction adequately
2. **Text-based config** - Our visual editor is superior

---

## References

- MouseTiler Repository: https://github.com/rxappdev/MouseTiler
- MouseTiler README: https://github.com/rxappdev/MouseTiler/blob/main/README.md
- MouseTiler Issues: https://github.com/rxappdev/MouseTiler/issues
- KDE Store: Search "Mouse Tiler" in KWin Scripts
- Discord Support: https://discord.gg/Js6AYsnQQj
