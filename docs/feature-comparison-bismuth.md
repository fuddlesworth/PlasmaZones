# Feature Comparison: PlasmaZones vs Bismuth

This document compares PlasmaZones with [Bismuth](https://github.com/Bismuth-Forge/bismuth), the successor to Krohnkite that was the most popular dynamic tiling solution for KDE Plasma until its archival.

**Date:** 2026-01-22

---

## Overview

Bismuth was a KDE Plasma add-on that automatically tiled windows using keyboard-driven management, similar to i3, Sway, or dwm. It was a fork of Krohnkite with improved Plasma integration and Wayland support.

**Project Status: ARCHIVED**
- Repository archived: June 16, 2026
- Last release: v3.1.4 (September 2022)
- 142 open issues unresolved
- No Plasma 6 support

**Archive Reason:** Compatibility challenges with KDE Plasma 5.27+ (Issue #471: "Future of the project after 5.27")

---

## Fundamental Philosophy Differences

| Aspect | PlasmaZones | Bismuth |
|--------|-------------|---------|
| **Tiling Model** | Manual zone-based | Automatic dynamic |
| **Window Placement** | User drags to predefined zones | Windows auto-arrange on open |
| **Primary Input** | Mouse-first, keyboard support | Keyboard-first, mouse resize |
| **Layout Definition** | User-created visual zones | Algorithm-based presets |
| **Inspiration** | Windows PowerToys FancyZones | i3, Sway, dwm |
| **Target User** | Control-oriented users | Keyboard-centric power users |

---

## Bismuth Layouts

Bismuth provided algorithm-based layouts:

| Layout | Description | PlasmaZones Equivalent |
|--------|-------------|------------------------|
| **Tile (Classic)** | Master-stack arrangement | Create master+stack zone layout |
| **Monocle** | Single fullscreen, others hidden | Single-zone layout |
| **Three Column** | Left-center-right arrangement | 3-column zone layout |
| **Tall** | Large left window, stack on right | Left main + right stack zones |
| **Wide** | Large top window, stack below | Top main + bottom stack zones |
| **BSP** | Binary space partitioning | Recursive zone splitting |
| **Floating** | Tiling disabled | No layout active |
| **Spiral** | Fibonacci-style arrangement | No direct equivalent |
| **Quarter** | 4-quadrant division | 4-zone grid layout |

---

## Feature Comparison

### Features Both Have

| Feature | PlasmaZones | Bismuth |
|---------|-------------|---------|
| Multi-monitor support | ✅ Full support | ✅ Supported |
| Virtual desktop support | ✅ Per-desktop layouts | ✅ Per-desktop layouts |
| Activities support | ✅ Per-activity layouts | ✅ Per-activity layouts |
| Floating windows | ✅ Via exclusion list | ✅ Via window rules |
| Keyboard shortcuts | ✅ Layout switching | ✅ Full navigation + management |
| System theme integration | ✅ Kirigami/KDE | ✅ KDE native |
| Window gaps/gutters | ✅ Zone margins | ✅ Configurable gaps |

### Features We Have That Bismuth Didn't

| Feature | Notes |
|---------|-------|
| **WYSIWYG Zone Editor** | Bismuth used algorithm layouts only |
| **Arbitrary zone shapes** | Any geometry; Bismuth fixed to algorithms |
| **Zone overlay during drag** | Visual zone preview |
| **Multi-zone spanning** | Drag across multiple zones |
| **Window geometry memory** | Restore pre-snap size |
| **OSD layout messages** | Visual feedback on switch |
| **Edge trigger snapping** | Mouse-driven zone activation |
| **Zone selector popup** | Quick zone selection UI |
| **Configurable polling rate** | Performance tuning |
| **Active maintenance** | ✅ We're maintained |
| **Plasma 6 support** | ✅ Full support |

### Features Bismuth Had That We Could Consider

| Feature | Description | Priority |
|---------|-------------|----------|
| **Dedicated Settings Panel** | System Settings > Window Management > Window Tiling | Already have KCM |
| **Built-in Window Decoration** | Border-only window style | Could add as optional |
| **Krohnkite Shortcut Import** | Migration script | Not needed for us |
| **Focus Navigation** | Keyboard window focus movement | High - should add |
| **Window Movement Shortcuts** | Move windows between positions | High - already planned |
| **Per-Window Floating Toggle** | Quick float via shortcut | Medium |
| **BSP (Binary Space) Layout** | Auto-subdividing layout | Low - could be preset |

---

## Keyboard Shortcuts Comparison

### Bismuth Shortcuts (from System Settings > Shortcuts > Window Tiling)

| Category | Bismuth | PlasmaZones |
|----------|---------|-------------|
| **Focus Movement** | ✅ Up/Down/Left/Right | ❌ Not implemented |
| **Window Movement** | ✅ Move in direction | ⚠️ Detection ready, not implemented |
| **Layout Switching** | ✅ Cycle/Select layouts | ✅ Previous/Next + Quick (1-9) |
| **Float Toggle** | ✅ Per-window toggle | ❌ Uses exclusion list |
| **Master Resize** | ✅ Increase/Decrease | ❌ No dynamic resizing |
| **Rotation** | ✅ Rotate layout | ❌ Not applicable |

### Recommended Additions for PlasmaZones

Based on Bismuth's keyboard-centric approach:

1. **Focus navigation** - Meta + Arrow keys to move focus between zones
2. **Window movement** - Meta + Shift + Arrow keys to move window to adjacent zone
3. **Float toggle** - Meta + F to toggle window exclusion

---

## Configuration Comparison

| Aspect | PlasmaZones | Bismuth |
|--------|-------------|---------|
| **Settings Location** | KCM plugin | System Settings > Window Management > Window Tiling |
| **Settings UI** | Full GUI | Full GUI |
| **Layout Editor** | ✅ WYSIWYG visual | ❌ Preset selection only |
| **Window Rules** | Exclusion patterns | Float/ignore per window class |
| **Appearance Options** | Theme integration | Gaps + optional border-only decoration |
| **Apply Changes** | Immediate | Immediate |

---

## Why Bismuth Failed (Lessons for PlasmaZones)

### Technical Issues at Archive Time

From the 142 open issues:

| Issue Type | Examples |
|------------|----------|
| **Settings UI failures** | "Window Tiling" section not appearing (#489, #504) |
| **Tiling malfunctions** | Windows stop tiling randomly (#499) |
| **Ghost windows** | Phantom window artifacts (#498) |
| **Build failures** | TypeScript compilation errors with newer toolchains (#506) |
| **App compatibility** | Wine application issues (#496) |

### Root Causes

1. **KDE API changes** - Plasma 5.27 broke compatibility
2. **TypeScript/KWin Script architecture** - Difficult to maintain
3. **Single maintainer burnout** - Project became too complex
4. **No Plasma 6 migration path** - Would require complete rewrite

### Lessons for PlasmaZones

1. **Stay close to KDE APIs** - Use stable, documented interfaces
2. **C++/Qt architecture** - More maintainable than scripts
3. **Modular design** - Easier to adapt to KDE changes
4. **Active community** - Distribute maintenance burden

---

## Market Opportunity

With Bismuth archived and no maintained alternative:

| Solution | Status | Plasma 6 |
|----------|--------|----------|
| **Bismuth** | Archived June 2026 | ❌ No |
| **Krohnkite** | Unmaintained since 2022 | ❌ No |
| **Krohnkite Fork** | Archived Dec 2025 | ✅ Yes (KWin 6 only) |
| **KZones** | Active | ✅ Yes |
| **MouseTiler** | Active | ✅ Yes (Plasma 6+ only) |
| **PlasmaZones** | Active | ✅ Yes |

**PlasmaZones occupies a unique position:**
- Only actively maintained zone-based tiler with full Plasma 5+6 support
- WYSIWYG editor (unique among all solutions)
- Comprehensive feature set
- No dynamic tiling ecosystem competitor for Plasma 6

---

## Potential Enhancements from Bismuth

### High Priority

#### 1. Focus Navigation Shortcuts
Bismuth's keyboard-first navigation was highly valued by users.

**Implementation:**
- Meta + Up/Down/Left/Right to move focus between zones
- Use existing `getAdjacentZone()` for zone detection
- Activate window in target zone

---

#### 2. Window Movement Shortcuts
**Status:** Already identified in KZones comparison, adjacent zone detection exists.

---

### Medium Priority

#### 3. Border-Only Window Decoration Option
Bismuth included a minimal window decoration style.

**Consideration:**
- Could provide a companion Aurorae theme
- Or document how to configure existing KDE decorations

---

#### 4. Configurable Window Gaps/Gutters
**Current State:** We have zone margins in the editor.

**Enhancement:**
- Add global gap setting in addition to per-zone margins
- Allow runtime gap adjustment

---

### Lower Priority

#### 5. BSP Layout Preset
Binary Space Partitioning as a ready-made layout.

**Implementation:**
- Add as preset layout in layout manager
- Recursively subdivided zones

---

## Conclusion

Bismuth was the most polished dynamic tiling solution for KDE, but its archival due to Plasma compatibility issues left a significant gap. PlasmaZones benefits from:

1. **Different architecture** - Zone-based rather than algorithm-based, simpler to maintain
2. **C++/Qt foundation** - More robust than KWin Script approach
3. **Active maintenance** - Continued development
4. **Plasma 5+6 support** - Broader compatibility
5. **WYSIWYG editor** - Unique feature no competitor has

The keyboard navigation features from Bismuth (focus movement, window movement shortcuts) would make PlasmaZones more appealing to former Bismuth users seeking a maintained solution.

---

## References

- Bismuth Repository: https://github.com/Bismuth-Forge/bismuth
- Bismuth Website: https://bismuth-forge.github.io/bismuth/
- Bismuth Issues: https://github.com/Bismuth-Forge/bismuth/issues
- Archive Discussion: https://github.com/Bismuth-Forge/bismuth/issues/471
- Krohnkite (predecessor): https://github.com/esjeon/krohnkite
