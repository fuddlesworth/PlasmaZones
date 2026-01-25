# Feature Comparison: PlasmaZones vs Krohnkite

This document compares PlasmaZones with [Krohnkite](https://github.com/esjeon/krohnkite) (and its successors) to understand the fundamental differences between manual zone-based and automatic dynamic tiling approaches.

**Date:** 2026-01-22

---

## Overview

Krohnkite is a **dynamic tiling** window manager extension for KWin, inspired by dwm (suckless). Unlike PlasmaZones' manual zone placement, Krohnkite **automatically arranges windows** according to layout algorithms.

**Project Status:**
- **Original Krohnkite** (esjeon): Last release v0.8.1 (Feb 2022), unmaintained
- **Maintained Fork** (anametologin): KWin 6 support, archived Dec 2025, migrated to [Codeberg](https://codeberg.org/anametologin/Krohnkite)
- **Bismuth**: Spiritual successor, archived June 2026

---

## Fundamental Philosophy Differences

| Aspect | PlasmaZones | Krohnkite |
|--------|-------------|-----------|
| **Tiling Model** | Manual zone-based | Automatic dynamic |
| **Window Placement** | User drags to predefined zones | Windows auto-arrange on open |
| **Layout Control** | Fixed zones, user chooses | Algorithm determines position |
| **Inspiration** | Windows PowerToys FancyZones | dwm (suckless) |
| **User Interaction** | Mouse-first with keyboard support | Keyboard-first with mouse resize |
| **Learning Curve** | Low - intuitive drag-drop | Medium - requires shortcut learning |
| **Flexibility** | High - any zone shape/position | Medium - algorithm constraints |

**Key Distinction:** PlasmaZones is for users who want **control over where windows go**. Krohnkite is for users who want the **system to manage window arrangement automatically**.

---

## Krohnkite Layout Types

Krohnkite provides algorithm-based layouts (not user-defined zones):

| Layout | Description | PlasmaZones Equivalent |
|--------|-------------|------------------------|
| **Tile** | Master-stack (dwm-style), main window + stack | Could create similar zone layout manually |
| **Monocle** | Single fullscreen window, others hidden | Single-zone layout |
| **Three Column** | Left-center-right arrangement | 3-column zone layout |
| **Columns** | Equal vertical columns | N-column zone layout |
| **Spread** | Spaced arrangement for desktop visibility | No direct equivalent |
| **Stair** | Cascading diagonal windows | No equivalent |
| **Spiral** | Fibonacci-style arrangement | No equivalent |
| **Stacked** | Overlapping stack arrangement | No equivalent |
| **BTree** | Binary tree division | Recursive zone splitting |
| **Floating** | Disable tiling | No layout active |
| **Quarter** | 4-quadrant with overflow floating | 4-zone grid layout |

---

## Feature Comparison

### Features Both Have

| Feature | PlasmaZones | Krohnkite |
|---------|-------------|-----------|
| Multi-monitor support | ✅ Full support | ✅ With configuration |
| Virtual desktop support | ✅ Per-desktop layouts | ✅ Per-desktop layouts |
| Activities support | ✅ Per-activity layouts | ✅ Per-activity layouts |
| Floating windows | ✅ Via exclusion | ✅ Toggle per window |
| Keyboard shortcuts | ✅ Layout switching | ✅ Navigation + management |
| System theme integration | ✅ Kirigami/KDE | ✅ KWin native |

### Features We Have That Krohnkite Doesn't

| Feature | Notes |
|---------|-------|
| **WYSIWYG Zone Editor** | Krohnkite uses algorithm layouts, no visual editor |
| **Arbitrary zone shapes** | We support any zone geometry; Krohnkite uses fixed algorithms |
| **Zone overlay during drag** | We show zones; Krohnkite auto-places |
| **Multi-zone spanning** | Drag across zones; Krohnkite doesn't have zones |
| **Window geometry memory** | Restore pre-snap size; Krohnkite doesn't track this |
| **OSD layout messages** | Visual feedback on switch |
| **Edge trigger snapping** | Mouse-driven zone activation |
| **Zone selector popup** | Quick zone selection UI |
| **App exclusion by class** | Pattern-based filtering |
| **Configurable polling** | Performance tuning |

### Features Krohnkite Has That We Don't

| Feature | Description | Relevance to PlasmaZones |
|---------|-------------|--------------------------|
| **Automatic tiling** | Windows auto-arrange on open | Different paradigm - we have "auto-snap to last zone" |
| **Master window concept** | Primary window gets more space | Could add as zone property |
| **Resize affects layout** | Drag resize adjusts algorithm | Our zones are fixed |
| **Focus navigation** | Meta+J/K moves focus between tiles | We have adjacent zone detection but no focus shortcuts |
| **Tile jiggling** | Force refresh of frozen tiles | Not needed in our model |
| **Spiral/Fibonacci layout** | Mathematical arrangement | Could create as preset layout |

---

## Keyboard Shortcuts Comparison

### Krohnkite Default Shortcuts

| Action | Shortcut | PlasmaZones Equivalent |
|--------|----------|------------------------|
| Focus down/up | Meta + J/K | ❌ No focus navigation |
| Focus left/right | Meta + H/L | ❌ No focus navigation |
| Move window down/up | Meta + Shift + J/K | ⚠️ Adjacent zone ready, not implemented |
| Move window left/right | Meta + Shift + H/L | ⚠️ Adjacent zone ready, not implemented |
| Set as master | Meta + Return | ❌ No master concept |
| Toggle floating | Meta + F | ❌ Uses exclusion list |
| Cycle layouts | Meta + \ | ✅ Previous/Next layout |
| Tile layout | Meta + T | ✅ Quick layout shortcuts |
| Monocle layout | Meta + M | ✅ Quick layout shortcuts |
| Increase master | Meta + I | ❌ No dynamic resizing |
| Decrease master | Meta + D | ❌ No dynamic resizing |

### What We Should Consider Adding

Based on Krohnkite's keyboard-centric approach:

1. **Focus navigation shortcuts** (Meta + H/J/K/L) - Navigate between zones
2. **Window movement shortcuts** - Move active window to adjacent zone (already have detection)
3. **Toggle floating per-window** - Quick exclude without settings

---

## Configuration Comparison

| Aspect | PlasmaZones | Krohnkite |
|--------|-------------|-----------|
| **Settings UI** | Full KCM GUI | Requires manual symlink setup |
| **Apply changes** | Immediate | Requires script reload/reboot |
| **Per-screen config** | Built-in | Format: `Output:Activity:Desktop:layout` |
| **Window rules** | Exclusion patterns | Class/resource/caption filtering |
| **Debug logging** | Standard Qt logging | KSystemLog integration |

---

## Use Case Comparison

### Choose PlasmaZones When:
- You want **predictable, fixed zone positions**
- You need **custom zone shapes and layouts**
- You prefer **mouse-driven** window placement
- You want **visual feedback** during arrangement
- You need **fine-grained control** over window positions
- You're coming from **Windows PowerToys FancyZones**

### Choose Krohnkite When:
- You want **automatic window arrangement**
- You prefer **keyboard-driven** workflow
- You like **dwm/i3/Sway** style tiling
- You want windows to **self-organize**
- You're comfortable with **algorithm-based layouts**
- You don't need custom zone geometries

---

## Project Health Comparison

| Metric | PlasmaZones | Krohnkite (original) | Krohnkite Fork | Bismuth |
|--------|-------------|---------------------|----------------|---------|
| **Status** | Active | Unmaintained | Archived (Codeberg) | Archived |
| **Last Release** | Current | Feb 2022 | Dec 2025 | Sept 2022 |
| **Plasma 6 Support** | ✅ Yes | ❌ No | ✅ Yes | ❌ No |
| **Plasma 5 Support** | ✅ Yes | ✅ Yes | ❌ No | ✅ Yes |

**Note:** The dynamic tiling ecosystem for KDE is fragmented with no clearly maintained solution for Plasma 6. This presents an opportunity for PlasmaZones to be the stable, maintained window management solution.

---

## Potential Enhancements Inspired by Krohnkite

### High Priority

#### 1. Focus Navigation Shortcuts
**Description:** Meta + H/J/K/L to move focus between zones/windows

**Implementation:**
- Detect which zone has focused window
- Use `getAdjacentZone()` to find target
- Activate window in target zone

---

#### 2. Window Movement Shortcuts (Already Identified)
**Description:** Move active window to adjacent zone with keyboard

**Status:** Adjacent zone detection exists, implementation pending

---

### Medium Priority

#### 3. Per-Window Float Toggle
**Description:** Quick shortcut to toggle a window's exclusion from tiling

**Implementation:**
- Add `toggleWindowExclusion(windowId)` method
- Bind to shortcut (e.g., Meta + F)
- Temporarily exclude window from zone snapping

---

### Lower Priority

#### 4. Preset Algorithm Layouts
**Description:** Offer Krohnkite-style layouts as presets

**Candidates:**
- Master + Stack (dwm-style)
- Fibonacci/Spiral
- 3-Column with center main

---

## Conclusion

PlasmaZones and Krohnkite serve **fundamentally different use cases**:

- **PlasmaZones**: Manual control, visual zones, mouse-friendly
- **Krohnkite**: Automatic tiling, keyboard-driven, algorithm-based

They are **complementary rather than competitive**. Users who want automatic tiling choose Krohnkite-style tools; users who want zone control choose FancyZones-style tools.

Given the **fragmented and unmaintained state** of dynamic tiling solutions for KDE Plasma 6, PlasmaZones occupies a strong position as an **actively maintained, feature-complete** window management solution.

---

## References

- Krohnkite (Original): https://github.com/esjeon/krohnkite
- Krohnkite Fork (anametologin): https://github.com/anametologin/krohnkite
- Krohnkite on Codeberg: https://codeberg.org/anametologin/Krohnkite
- Bismuth: https://github.com/Bismuth-Forge/bismuth
- Bismuth Website: https://bismuth-forge.github.io/bismuth/
- KDE Store: https://store.kde.org/p/1281790/
