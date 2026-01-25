# Immediate Feature Opportunities for PlasmaZones

**Analysis Date:** 2026-01-24
**Purpose:** Extract non-autotiling features from tiling WM research that can enhance PlasmaZones now
**Source Documents:** autotiling-research.md, AUTOTILING_DESIGN.md, AUTOTILING_UX_DESIGN.md

---

## Executive Summary

Many features researched for autotiling are actually **general window management improvements** that benefit the existing zone-based system. This document identifies features that:

1. Don't require autotiling algorithms
2. Can be implemented independently
3. Improve the existing PlasmaZones experience
4. Appeal to both manual zone users AND future autotiling users

---

## Feature Categories

### Category A: Keyboard Enhancements (High Value)
### Category B: Visual Feedback Improvements
### Category C: Configuration Options
### Category D: Window Management Operations
### Category E: Multi-Monitor Improvements
### Category F: UX Polish

---

## Category A: Keyboard Enhancements

### A1. Vim-Style Navigation (Meta+H/J/K/L)

**Current State:** Phase 1 uses Meta+Ctrl+Arrow keys
**Improvement:** Add vim-style H/J/K/L as alternative/default

| Action | Current | Proposed Addition |
|--------|---------|-------------------|
| Focus left | `Meta+Ctrl+Left` | `Meta+H` |
| Focus down | `Meta+Ctrl+Down` | `Meta+J` |
| Focus up | `Meta+Ctrl+Up` | `Meta+K` |
| Focus right | `Meta+Ctrl+Right` | `Meta+L` |

**Why This Matters:**
- Bismuth/i3/Sway users expect H/J/K/L
- Vim users are a significant portion of tiling WM users
- Faster than reaching for arrow keys
- No conflict with existing shortcuts (just additions)

**Implementation Effort:** Low (add alternative key bindings)

---

### A2. Window Movement Between Zones

**Current State:** Windows moved by drag only, or Meta+Alt+Arrows
**Improvement:** Add vim-style movement shortcuts

| Action | Proposed Shortcut |
|--------|-------------------|
| Move window left | `Meta+Shift+H` or `Meta+Shift+Left` |
| Move window down | `Meta+Shift+J` or `Meta+Shift+Down` |
| Move window up | `Meta+Shift+K` or `Meta+Shift+Up` |
| Move window right | `Meta+Shift+L` or `Meta+Shift+Right` |

**Behavior:**
- Window moves to adjacent zone in that direction
- If no zone in direction, window stays put (optional: wrap around)
- Animation shows window sliding to new zone

**Implementation Effort:** Low-Medium (leverage existing adjacency detection)

---

### A3. Window Swap Operations

**Current State:** No swap functionality
**Improvement:** Swap focused window with another

| Action | Proposed Shortcut | Description |
|--------|-------------------|-------------|
| Swap with next | `Meta+Shift+Tab` | Swap with next zone's window |
| Swap with previous | `Meta+Ctrl+Shift+Tab` | Swap with previous zone's window |
| Swap in direction | `Meta+Ctrl+Shift+H/J/K/L` | Swap with window in direction |

**Behavior:**
- Two windows exchange positions
- Both windows animate to swapped positions
- Works even with different-sized zones

**Implementation Effort:** Medium

---

### A4. Quick Layout Switching

**Current State:** Layout switching via system shortcuts
**Improvement:** Faster cycling and direct access

| Action | Proposed Shortcut | Description |
|--------|-------------------|-------------|
| Cycle layout forward | `Meta+Space` | Next layout in list |
| Cycle layout backward | `Meta+Shift+Space` | Previous layout |
| Layout 1-9 | `Meta+1` through `Meta+9` | Direct layout access |

**Implementation Effort:** Low (shortcuts already exist, just add alternatives)

---

### A5. Toggle Window Float

**Current State:** No quick float toggle
**Improvement:** Instantly remove/add window from zone management

| Action | Proposed Shortcut | Description |
|--------|-------------------|-------------|
| Toggle float | `Meta+F` | Remove window from zone, becomes floating |
| Float all | `Meta+Shift+F` | All windows become floating |
| Tile all | `Meta+Ctrl+F` | Force all windows into zones |

**Behavior:**
- Floating window returns to original size before snapping
- Remembers last zone so re-tiling puts it back
- OSD shows "Floating" or "Tiled" status

**Implementation Effort:** Medium

---

### A6. Focus Zone by Number

**Current State:** Not available
**Improvement:** Jump directly to zone by number

| Action | Shortcut | Description |
|--------|----------|-------------|
| Focus zone 1-9 | `Meta+Ctrl+1-9` | Focus window in zone N |

**Behavior:**
- Zone numbers displayed in overlay (optional)
- If zone empty, no action (or focus nearest)
- If zone has multiple windows (future), cycles through them

**Implementation Effort:** Low

---

## Category B: Visual Feedback Improvements

### B1. Focus Indication Enhancement

**Current State:** Standard KWin focus decoration
**Improvement:** Additional visual cues for zone focus

**Options:**
```
Option 1: Corner Markers
┌◆────────────────────◆┐
│                      │
│   Focused Window     │
│                      │
└◆────────────────────◆┘

Option 2: Thicker Border
╔══════════════════════╗
║                      ║
║   Focused Window     ║
║                      ║
╚══════════════════════╝

Option 3: Subtle Glow
┌──────────────────────┐
│ ░░                ░░ │
│ ░░ Focused Window ░░ │
│ ░░                ░░ │
└──────────────────────┘
```

**Configuration:**
- Enable/disable in settings
- Color follows system accent or custom
- Animation: subtle pulse (optional)

**Implementation Effort:** Medium (requires KWin effect enhancement)

---

### B2. Zone Number Overlay

**Current State:** Numbers only shown during drag
**Improvement:** Optional persistent zone numbers

```
┌─────────────────┐ ┌─────────┐
│ ①              │ │ ②      │
│                 │ │         │
│                 │ ├─────────┤
│                 │ │ ③      │
└─────────────────┘ └─────────┘
```

**Configuration:**
- Show: Always / During navigation / Never
- Position: Top-left corner / Center
- Style: Circle / Square / Badge

**Implementation Effort:** Low (overlay already exists)

---

### B3. Enhanced OSD Notifications

**Current State:** Basic layout change OSD
**Improvement:** Richer contextual feedback

**OSD Triggers:**
- Layout changed
- Window moved to zone
- Navigation at screen edge (no adjacent zone)
- Float toggled
- Shortcut executed

**OSD Content:**
```
┌────────────────────────────────┐
│  ┌─────────────────────────┐  │
│  │   [Layout Preview]      │  │
│  └─────────────────────────┘  │
│                                │
│     Layout: Priority Grid      │
│     Zones: 4                   │
│                                │
└────────────────────────────────┘
```

**Implementation Effort:** Low-Medium

---

### B4. Navigation Direction Indicator

**Current State:** No visual feedback during navigation
**Improvement:** Show brief directional indicator

When user presses Meta+H (focus left):
```
         ┌─────────────────┐
    ◀────│  Target Zone    │
         │                 │
         └─────────────────┘
```

**Behavior:**
- Arrow appears for 200ms showing navigation direction
- If no zone in direction, show "bump" animation at edge
- Subtle, non-intrusive

**Implementation Effort:** Medium

---

### B5. Window Count Badge

**Current State:** Not available
**Improvement:** Show how many windows are in each zone (for future multi-window zones)

```
┌─────────────────┐ ┌─────────┐
│                 │ │     [2] │
│     Window      │ │         │
│                 │ │         │
└─────────────────┘ └─────────┘
```

**Implementation Effort:** Low (preparation for future features)

---

## Category C: Configuration Options

### C1. Window Gaps (Inner/Outer)

**Current State:** Zone padding exists but limited
**Improvement:** Separate inner and outer gap controls

```
Outer gap (screen edge):
┌──────────────────────────────────┐
│  ╭────────────────╮  ╭────────╮  │
│  │                │  │        │  │
│  │    Zone 1      │  │ Zone 2 │  │
│  │                │  │        │  │
│  ╰────────────────╯  ╰────────╯  │
└──────────────────────────────────┘
        ↑ Inner gap (between zones)
```

**Configuration:**
- Inner gap: 0-50px (between zones)
- Outer gap: 0-50px (screen edge)
- Smart gaps: Hide when single window (like i3)
- Per-layout gap override

**Implementation Effort:** Low (extend existing zonePadding)

---

### C2. Smart Gaps and Borders

**Current State:** Gaps always visible
**Improvement:** Intelligent gap/border hiding

| Setting | Behavior |
|---------|----------|
| Smart gaps | Hide gaps when only one window on screen |
| Smart borders | Hide window borders when maximized/alone |
| Single window fullscreen | Auto-maximize single window |

**Implementation Effort:** Medium

---

### C3. Navigation Style Selection

**Current State:** Arrow keys only
**Improvement:** Let user choose navigation style

```
Settings > Shortcuts > Navigation Style
┌────────────────────────────────────────┐
│  ○ Arrow keys only (Meta+Ctrl+Arrows)  │
│  ○ Vim keys only (Meta+H/J/K/L)        │
│  ● Both vim and arrow keys             │
└────────────────────────────────────────┘
```

**Implementation Effort:** Low

---

### C4. Focus Follows Mouse (Optional)

**Current State:** Not available
**Improvement:** Optional focus-follows-mouse for zones

**Behavior:**
- When cursor enters a zone, that zone's window gets focus
- Optional delay before focus change (100-500ms)
- Can be disabled per-layout

**Configuration:**
- Enable/disable
- Delay (ms)
- Raise on focus (yes/no)

**Implementation Effort:** Medium

---

### C5. New Window Behavior

**Current State:** Windows snap to dragged zone
**Improvement:** Configure what happens to new windows

| Option | Behavior |
|--------|----------|
| Do nothing | Window opens at default position |
| Snap to empty zone | Auto-snap to first empty zone |
| Snap to focused zone | New window goes where focus is |
| Snap to largest zone | New window to zone with most space |
| Show zone picker | Overlay appears for zone selection |

**Implementation Effort:** Medium-High

---

### C6. Per-Application Zone Rules

**Current State:** Exclusion list only
**Improvement:** Rich per-app configuration

```
App Rules for "Firefox"
┌──────────────────────────────────────────┐
│  Window class: firefox                    │
│                                          │
│  Behavior:                               │
│  ○ Normal (follow zone rules)            │
│  ○ Always float                          │
│  ○ Force to specific zone: [Zone 1  ▼]   │
│  ○ Exclude from PlasmaZones              │
│                                          │
│  ☐ Remember last zone position           │
│  ☐ Open in same zone as last time        │
└──────────────────────────────────────────┘
```

**Implementation Effort:** Medium-High

---

## Category D: Window Management Operations

### D1. Push to Empty Zone

**Current State:** Manual drag required
**Improvement:** Keyboard shortcut to push window to nearest empty zone

| Action | Shortcut | Description |
|--------|----------|-------------|
| Push to empty | `Meta+P` | Move to nearest empty zone |
| Push to empty (direction) | `Meta+P` then `H/J/K/L` | Push in specific direction |

**Implementation Effort:** Low-Medium

---

### D2. Gather Windows

**Current State:** Not available
**Improvement:** Collect all windows into zones

| Action | Shortcut | Description |
|--------|----------|-------------|
| Gather all | `Meta+G` | Snap all floating windows to zones |
| Gather to screen | `Meta+Shift+G` | Gather windows from other screens |

**Implementation Effort:** Medium

---

### D3. Restore Window Size

**Current State:** Not available
**Improvement:** Return window to pre-snap dimensions

| Action | Shortcut | Description |
|--------|----------|-------------|
| Restore size | `Meta+Escape` | Unsnap and restore original size |

**Behavior:**
- Remember window size before snapping
- On restore, window becomes floating at original size
- Position: where it was, or center of zone

**Implementation Effort:** Low (track pre-snap geometry)

---

### D4. Minimize/Unminimize Zone

**Current State:** Standard minimize
**Improvement:** Quick minimize/restore for zone's window

| Action | Shortcut | Description |
|--------|----------|-------------|
| Minimize zone | `Meta+N` | Minimize window in focused zone |
| Restore zone | `Meta+Shift+N` | Restore last minimized |

**Implementation Effort:** Low

---

### D5. Close Window in Zone

**Current State:** Standard close (Alt+F4)
**Improvement:** Quick close aligned with zone shortcuts

| Action | Shortcut | Description |
|--------|----------|-------------|
| Close focused | `Meta+Q` | Close window in focused zone |
| Close in direction | `Meta+Shift+Q` + dir | Close window in direction |

**Implementation Effort:** Low

---

## Category E: Multi-Monitor Improvements

### E1. Move Window to Other Screen

**Current State:** May exist but not well integrated
**Improvement:** Consistent shortcuts for multi-monitor

| Action | Shortcut | Description |
|--------|----------|-------------|
| Move to next screen | `Meta+Shift+.` | Window to next monitor |
| Move to prev screen | `Meta+Shift+,` | Window to previous monitor |
| Move to screen N | `Meta+Shift+1-9` | Window to specific screen |

**Behavior:**
- Window moves to same relative zone on target screen
- If zone doesn't exist, use first zone
- Animation shows window crossing screens

**Implementation Effort:** Medium

---

### E2. Focus Other Screen

**Current State:** Not available
**Improvement:** Navigate focus between monitors

| Action | Shortcut | Description |
|--------|----------|-------------|
| Focus next screen | `Meta+.` | Focus shifts to next monitor |
| Focus prev screen | `Meta+,` | Focus shifts to previous monitor |

**Implementation Effort:** Low-Medium

---

### E3. Per-Monitor Layout Memory

**Current State:** Unknown
**Improvement:** Each monitor remembers its layout independently

**Behavior:**
- Each screen maintains its own active layout
- Switching layouts on one screen doesn't affect others
- Layout persists across sessions per-screen

**Implementation Effort:** Medium (may already exist)

---

## Category F: UX Polish

### F1. Drag Preview Enhancement

**Current State:** Zone highlights during drag
**Improvement:** Show window preview in target zone

```
During drag:
┌─────────────────┐ ┌─────────┐
│                 │ │ ╭─────╮ │
│   Existing      │ │ │Ghost│ │  ← Ghost preview of
│   Window        │ │ │     │ │    dragged window
│                 │ │ ╰─────╯ │
└─────────────────┘ └─────────┘
```

**Implementation Effort:** Medium

---

### F2. Zone Edge Resize

**Current State:** Resize in editor only
**Improvement:** Resize zones by dragging borders

**Behavior:**
- Hold modifier (Ctrl?) and drag zone border
- Adjacent zones resize proportionally
- Snaps to grid (optional)
- Changes are temporary or saveable

**Implementation Effort:** High

---

### F3. Quick Zone Editor Access

**Current State:** Via settings or shortcut
**Improvement:** More discoverable access

| Access Method | Shortcut/Action |
|---------------|-----------------|
| Keyboard | `Meta+Shift+E` (existing) |
| Right-click | Context menu on desktop |
| Panel widget | One-click access |
| Double-tap modifier | Double-tap Meta opens editor |

**Implementation Effort:** Low-Medium

---

### F4. Layout Preview Thumbnails

**Current State:** Text list of layouts
**Improvement:** Visual thumbnails in layout selector

```
Layout Selector:
┌─────────────────────────────────────────┐
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐       │
│  │█ ██ │ │██ █ │ │█████│ │█ █ █│       │
│  │█ ██ │ │██ █ │ │█████│ │█ █ █│       │
│  └─────┘ └─────┘ └─────┘ └─────┘       │
│  Priority  Focus   Rows   Columns      │
│      ↑ Currently selected              │
└─────────────────────────────────────────┘
```

**Implementation Effort:** Medium

---

### F5. Undo/Redo for Zone Operations

**Current State:** Not available
**Improvement:** Undo last zone operation

| Action | Shortcut | Description |
|--------|----------|-------------|
| Undo | `Meta+Z` | Undo last window movement |
| Redo | `Meta+Shift+Z` | Redo undone movement |

**Behavior:**
- Stack of last N operations
- Works for: move, swap, float toggle
- Clears on layout change

**Implementation Effort:** Medium-High

---

## Priority Matrix

### Immediate (Low Effort, High Value)

| Feature | Effort | Impact | Bismuth Users |
|---------|--------|--------|---------------|
| A1. Vim-style navigation | Low | High | ★★★ |
| A4. Quick layout cycling | Low | Medium | ★★ |
| B2. Zone number overlay | Low | Medium | ★★ |
| C1. Inner/outer gaps | Low | High | ★★★ |
| C3. Navigation style setting | Low | Medium | ★★★ |
| D3. Restore window size | Low | Medium | ★ |
| D4. Minimize zone shortcut | Low | Low | ★ |

### Short-Term (Medium Effort, High Value)

| Feature | Effort | Impact | Bismuth Users |
|---------|--------|--------|---------------|
| A2. Window movement shortcuts | Medium | High | ★★★ |
| A3. Window swap | Medium | High | ★★★ |
| A5. Toggle float | Medium | High | ★★★ |
| B1. Focus indication | Medium | Medium | ★★ |
| B3. Enhanced OSD | Medium | Medium | ★★ |
| C2. Smart gaps | Medium | Medium | ★★★ |
| E1. Move to other screen | Medium | High | ★★ |

### Medium-Term (Higher Effort)

| Feature | Effort | Impact | Bismuth Users |
|---------|--------|--------|---------------|
| C4. Focus follows mouse | Medium | Low | ★★ |
| C5. New window behavior | Medium-High | High | ★★ |
| C6. Per-app zone rules | Medium-High | High | ★★ |
| F1. Drag preview | Medium | Medium | ★ |
| F4. Layout thumbnails | Medium | Medium | ★ |

### Long-Term (High Effort)

| Feature | Effort | Impact |
|---------|--------|--------|
| F2. Zone edge resize | High | Medium |
| F5. Undo/redo | Medium-High | Low |

---

## Recommended Implementation Order

### Phase 2B: Keyboard Power-Up (1-2 weeks)

```
Week 1:
- A1. Vim-style navigation (Meta+H/J/K/L)
- A4. Quick layout cycling (Meta+Space)
- C3. Navigation style setting

Week 2:
- A2. Window movement (Meta+Shift+H/J/K/L)
- A5. Toggle float (Meta+F)
- D3. Restore window size (Meta+Escape)
```

**Deliverable:** Full keyboard control for existing zones

### Phase 2C: Visual Polish (1 week)

```
- C1. Inner/outer gap configuration
- B2. Optional zone number overlay
- B3. Enhanced OSD with layout preview
```

**Deliverable:** Configurable gaps, better feedback

### Phase 2D: Advanced Window Operations (1-2 weeks)

```
- A3. Window swap (Meta+Ctrl+Shift+H/J/K/L)
- E1. Move to other screen
- E2. Focus other screen
- C2. Smart gaps
```

**Deliverable:** Multi-monitor keyboard workflow

### Phase 2E: Per-App Rules (1 week)

```
- C6. Per-application zone rules
- C5. New window behavior
```

**Deliverable:** App-specific behaviors

---

## Keyboard Shortcut Summary (Non-Autotiling)

### Navigation
| Action | Primary | Alternative |
|--------|---------|-------------|
| Focus left | `Meta+H` | `Meta+Ctrl+Left` |
| Focus down | `Meta+J` | `Meta+Ctrl+Down` |
| Focus up | `Meta+K` | `Meta+Ctrl+Up` |
| Focus right | `Meta+L` | `Meta+Ctrl+Right` |
| Focus zone N | `Meta+Ctrl+1-9` | |

### Movement
| Action | Shortcut |
|--------|----------|
| Move left | `Meta+Shift+H` |
| Move down | `Meta+Shift+J` |
| Move up | `Meta+Shift+K` |
| Move right | `Meta+Shift+L` |
| Swap direction | `Meta+Ctrl+Shift+H/J/K/L` |

### Layout
| Action | Shortcut |
|--------|----------|
| Cycle layout | `Meta+Space` |
| Layout 1-9 | `Meta+1-9` |
| Open editor | `Meta+Shift+E` |

### Window State
| Action | Shortcut |
|--------|----------|
| Toggle float | `Meta+F` |
| Restore size | `Meta+Escape` |
| Minimize | `Meta+N` |
| Close | `Meta+Q` |

### Multi-Monitor
| Action | Shortcut |
|--------|----------|
| Move to next screen | `Meta+Shift+.` |
| Focus next screen | `Meta+.` |

---

## Conclusion

**26 features** identified that can enhance PlasmaZones independently of autotiling:

- **7 Keyboard enhancements** - Makes PlasmaZones keyboard-first
- **5 Visual improvements** - Better feedback and discoverability
- **6 Configuration options** - User control and preferences
- **5 Window operations** - Power user productivity
- **3 Multi-monitor features** - Better multi-screen workflow

**Quick wins (implementable in days):**
1. Vim-style H/J/K/L navigation
2. Meta+Space layout cycling
3. Inner/outer gap separation

**High-impact additions:**
1. Window movement shortcuts (Meta+Shift+H/J/K/L)
2. Toggle float (Meta+F)
3. Smart gaps

These features establish PlasmaZones as a **complete keyboard-driven window manager** even before autotiling, making it attractive to both:
- Existing PlasmaZones users wanting more control
- Bismuth refugees who want keyboard workflow immediately

---

*Analysis derived from autotiling research documents*
*Last updated: 2026-01-24*
