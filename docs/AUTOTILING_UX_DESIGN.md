# Autotiling UX Design for PlasmaZones

## Document Information
- **Version:** 1.0
- **Date:** 2026-01-24
- **Status:** Design Specification
- **Target Users:** PlasmaZones users, Bismuth/Krohnkite refugees

---

## 1. Executive Summary

This document defines the user experience for adding autotiling capabilities to PlasmaZones, bridging the gap between manual zone-based tiling and the automatic tiling that Bismuth/Krohnkite users expect.

### Design Goals

1. **Seamless coexistence** - Manual zones and autotiling work together, not against each other
2. **Progressive disclosure** - Simple for new users, powerful for power users
3. **Bismuth familiarity** - Keyboard shortcuts and behaviors familiar to refugees
4. **PlasmaZones identity** - Maintains the visual zone-first philosophy

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Autotiling as layout type | Fits existing layout system architecture |
| Meta+H/J/K/L navigation | Bismuth compatibility, vim familiarity |
| Per-monitor mode | Matches existing multi-monitor behavior |
| OSD mode indicator | Visual clarity without intrusion |

---

## 2. User Personas

### Persona A: PlasmaZones Enthusiast (Sarah)
- **Background:** Uses PlasmaZones for 6 months, loves visual zone editor
- **Workflow:** Mouse-driven, creates custom layouts for different tasks
- **Need:** Wants keyboard shortcuts without losing visual control
- **Concern:** "Will autotiling break my custom layouts?"

### Persona B: Bismuth Refugee (Marcus)
- **Background:** Power user who relied on Bismuth, devastated by archive
- **Workflow:** Keyboard-only, expects windows to auto-arrange on open
- **Need:** Automatic tiling with vim-style navigation
- **Concern:** "Can I get my workflow back without learning everything new?"

### Persona C: New User (Alex)
- **Background:** Heard about tiling, wants to try it
- **Workflow:** Mixed mouse/keyboard, not sure what they want
- **Need:** Easy onboarding, sensible defaults
- **Concern:** "This sounds complicated, will it just work?"

---

## 3. Settings Panel Design

### 3.1 Settings Navigation Structure

The autotiling settings integrate into the existing KCM tabbed interface:

```
PlasmaZones Settings
├── Layouts (existing)
├── Appearance (existing)
├── Behavior (existing)
│   └── [NEW] Autotiling section
├── Exclusions (existing)
├── Shortcuts (existing)
│   └── [NEW] Autotiling shortcuts
└── Editor (existing)
```

### 3.2 Behavior Tab: Autotiling Section

**Location:** Behavior tab, new collapsible section after existing behavior options

```
┌─────────────────────────────────────────────────────────────────┐
│ ▼ Autotiling                                                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Enable autotiling                              [Toggle: OFF]   │
│  ─────────────────────────────────────────────────────────      │
│  Automatically arrange windows using tiling algorithms          │
│                                                                 │
│  ┌─ When Enabled ─────────────────────────────────────────────┐ │
│  │                                                             │ │
│  │  Algorithm                                                  │ │
│  │  ┌──────────────────────────────────────────────────────┐  │ │
│  │  │ ● Master + Stack (Bismuth default)                   │  │ │
│  │  │ ○ BSP (Binary Space Partitioning)                    │  │ │
│  │  │ ○ Spiral (Fibonacci-like)                            │  │ │
│  │  │ ○ Columns (Equal width)                              │  │ │
│  │  │ ○ Rows (Equal height)                                │  │ │
│  │  │ ○ Monocle (Fullscreen stack)                         │  │ │
│  │  └──────────────────────────────────────────────────────┘  │ │
│  │                                                             │ │
│  │  Master ratio                                               │ │
│  │  ├────────────●───────────────┤ 55%                        │ │
│  │  30%                        70%                             │ │
│  │                                                             │ │
│  │  ☑ Show algorithm preview          [Preview Button]        │ │
│  │                                                             │ │
│  │  Window gaps                                                │ │
│  │  Inner gap: [8    ] px    Outer gap: [8    ] px            │ │
│  │                                                             │ │
│  │  New window behavior                                        │ │
│  │  ┌────────────────────────────────────────────┐            │ │
│  │  │ Add to master area                      ▼ │            │ │
│  │  └────────────────────────────────────────────┘            │ │
│  │  • Add to master area                                       │ │
│  │  • Add to stack area                                        │ │
│  │  • Insert at cursor position                                │ │
│  │                                                             │ │
│  │  ☑ Apply autotiling to new windows automatically           │ │
│  │  ☑ Exclude floating windows from autotiling                │ │
│  │  ☐ Respect minimum window size                             │ │
│  │                                                             │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                 │
│  Mode coexistence                                               │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ How autotiling interacts with manual zones:                │ │
│  │                                                            │ │
│  │ ● Autotiling replaces manual layout                        │ │
│  │   When enabled, autotiling algorithm controls all windows  │ │
│  │                                                            │ │
│  │ ○ Autotiling within zones (Hybrid)                         │ │
│  │   Autotile windows within each manual zone independently   │ │
│  │                                                            │ │
│  │ ○ Quick toggle (keyboard switches modes)                   │ │
│  │   Meta+T toggles between current layout and autotiling     │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.3 Algorithm Preview Panel

When "Show algorithm preview" is checked or [Preview Button] clicked:

```
┌─────────────────────────────────────────────────────────────────┐
│  Algorithm Preview: Master + Stack                              │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  ┌───────────────────┐ ┌──────────┐                     │   │
│  │  │                   │ │    2     │                     │   │
│  │  │                   │ ├──────────┤                     │   │
│  │  │        1          │ │    3     │                     │   │
│  │  │     (Master)      │ ├──────────┤                     │   │
│  │  │                   │ │    4     │                     │   │
│  │  │                   │ ├──────────┤                     │   │
│  │  │                   │ │    5     │                     │   │
│  │  └───────────────────┘ └──────────┘                     │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  Simulated windows: [1─●────5]  Drag to see layout behavior    │
│                                                                 │
│  Description:                                                   │
│  Classic tiling layout with one large master window on the      │
│  left and remaining windows stacked vertically on the right.    │
│  Most focused work happens in master; references in stack.      │
└─────────────────────────────────────────────────────────────────┘
```

### 3.4 Configuration Schema Addition

Add to `plasmazones.kcfg`:

```xml
<!-- Autotiling Settings -->
<group name="Autotiling">
  <entry name="Enabled" type="Bool">
    <label>Enable autotiling mode</label>
    <default>false</default>
  </entry>
  <entry name="Algorithm" type="Int">
    <label>Autotiling algorithm (0=MasterStack,1=BSP,2=Spiral,3=Columns,4=Rows,5=Monocle)</label>
    <default>0</default>
    <min>0</min>
    <max>5</max>
  </entry>
  <entry name="MasterRatio" type="Double">
    <label>Master area ratio (0.3-0.7)</label>
    <default>0.55</default>
    <min>0.3</min>
    <max>0.7</max>
  </entry>
  <entry name="InnerGap" type="Int">
    <label>Gap between windows in pixels</label>
    <default>8</default>
    <min>0</min>
    <max>50</max>
  </entry>
  <entry name="OuterGap" type="Int">
    <label>Gap from screen edges in pixels</label>
    <default>8</default>
    <min>0</min>
    <max>50</max>
  </entry>
  <entry name="NewWindowBehavior" type="Int">
    <label>Where new windows appear (0=Master,1=Stack,2=Cursor)</label>
    <default>1</default>
    <min>0</min>
    <max>2</max>
  </entry>
  <entry name="AutotileNewWindows" type="Bool">
    <label>Automatically tile new windows</label>
    <default>true</default>
  </entry>
  <entry name="ExcludeFloating" type="Bool">
    <label>Exclude floating windows from autotiling</label>
    <default>true</default>
  </entry>
  <entry name="RespectMinSize" type="Bool">
    <label>Respect window minimum size constraints</label>
    <default>false</default>
  </entry>
  <entry name="CoexistenceMode" type="Int">
    <label>How autotiling coexists with zones (0=Replace,1=Hybrid,2=Toggle)</label>
    <default>0</default>
    <min>0</min>
    <max>2</max>
  </entry>
</group>
```

---

## 4. Keyboard Shortcut Scheme

### 4.1 Design Philosophy

**Primary goal:** Bismuth users should feel at home while PlasmaZones users can opt-in gradually.

**Shortcut design principles:**
1. Meta+key for primary actions (navigation)
2. Meta+Shift+key for secondary actions (movement)
3. Meta+Ctrl+key for tertiary actions (resize/rotate)
4. Consistent direction keys across all categories

### 4.2 Complete Shortcut Table

#### Autotiling Mode Shortcuts

| Action | Default Shortcut | Bismuth Equivalent | Notes |
|--------|------------------|-------------------|-------|
| **Toggle autotiling** | `Meta+T` | `Meta+T` | Enable/disable autotiling |
| **Cycle algorithm** | `Meta+Shift+T` | `Meta+\` | Next: Master->BSP->Spiral->... |
| **Increase master ratio** | `Meta+Ctrl+L` | `Meta+L` | +5% master area |
| **Decrease master ratio** | `Meta+Ctrl+H` | `Meta+H` | -5% master area |
| **Increase master count** | `Meta+Ctrl+I` | `Meta+I` | More windows in master |
| **Decrease master count** | `Meta+Ctrl+D` | `Meta+D` | Fewer windows in master |
| **Rotate layout** | `Meta+R` | `Meta+R` | 90-degree rotation |
| **Mirror layout** | `Meta+Shift+R` | - | Flip horizontal |

#### Window Navigation (vim-style, Bismuth compatible)

| Action | Default Shortcut | Bismuth | Arrow Alternative |
|--------|------------------|---------|-------------------|
| **Focus left** | `Meta+H` | `Meta+H` | `Meta+Left` |
| **Focus down** | `Meta+J` | `Meta+J` | `Meta+Down` |
| **Focus up** | `Meta+K` | `Meta+K` | `Meta+Up` |
| **Focus right** | `Meta+L` | `Meta+L` | `Meta+Right` |
| **Focus master** | `Meta+M` | `Meta+Return` | - |
| **Focus next** | `Meta+Tab` | - | `Meta+Tab` |
| **Focus previous** | `Meta+Shift+Tab` | - | `Meta+Shift+Tab` |

#### Window Movement (vim-style with Shift)

| Action | Default Shortcut | Bismuth | Arrow Alternative |
|--------|------------------|---------|-------------------|
| **Move left** | `Meta+Shift+H` | `Meta+Shift+H` | `Meta+Shift+Left` |
| **Move down** | `Meta+Shift+J` | `Meta+Shift+J` | `Meta+Shift+Down` |
| **Move up** | `Meta+Shift+K` | `Meta+Shift+K` | `Meta+Shift+Up` |
| **Move right** | `Meta+Shift+L` | `Meta+Shift+L` | `Meta+Shift+Right` |
| **Swap with master** | `Meta+Return` | `Meta+Return` | - |
| **Move to master** | `Meta+Shift+Return` | - | - |

#### Window State

| Action | Default Shortcut | Bismuth | Notes |
|--------|------------------|---------|-------|
| **Toggle float** | `Meta+F` | `Meta+F` | Remove from tiling |
| **Toggle fullscreen** | `Meta+Shift+F` | `Meta+Shift+F` | Monocle single window |
| **Push to empty zone** | `Meta+P` | - | PlasmaZones feature |
| **Restore size** | `Meta+Escape` | - | Return to pre-snap size |

#### Layout Quick-Switch (existing PlasmaZones)

| Action | Shortcut | Notes |
|--------|----------|-------|
| **Layout 1-9** | `Meta+Ctrl+Alt+1-9` | Existing behavior |
| **Previous layout** | `Meta+Ctrl+Alt+Left` | Existing behavior |
| **Next layout** | `Meta+Ctrl+Alt+Right` | Existing behavior |
| **Open editor** | `Meta+Shift+E` | Existing behavior |

### 4.3 Shortcuts Settings UI Addition

Add to the Shortcuts tab:

```
┌─────────────────────────────────────────────────────────────────┐
│ ▼ Autotiling Shortcuts                                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Navigation Style                                               │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ ● Vim-style (H/J/K/L) - Recommended for Bismuth users      │ │
│  │ ○ Arrow keys only                                          │ │
│  │ ○ Both vim and arrow keys                                  │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                 │
│  Mode Control                                                   │
│  Toggle autotiling         [Meta+T          ] [Clear] [Reset]  │
│  Cycle algorithm           [Meta+Shift+T    ] [Clear] [Reset]  │
│                                                                 │
│  Window Navigation                                              │
│  Focus left                [Meta+H          ] [Clear] [Reset]  │
│  Focus down                [Meta+J          ] [Clear] [Reset]  │
│  Focus up                  [Meta+K          ] [Clear] [Reset]  │
│  Focus right               [Meta+L          ] [Clear] [Reset]  │
│  Focus master              [Meta+M          ] [Clear] [Reset]  │
│                                                                 │
│  Window Movement                                                │
│  Move left                 [Meta+Shift+H    ] [Clear] [Reset]  │
│  Move down                 [Meta+Shift+J    ] [Clear] [Reset]  │
│  Move up                   [Meta+Shift+K    ] [Clear] [Reset]  │
│  Move right                [Meta+Shift+L    ] [Clear] [Reset]  │
│  Swap with master          [Meta+Return     ] [Clear] [Reset]  │
│                                                                 │
│  Layout Adjustment                                              │
│  Increase master ratio     [Meta+Ctrl+L     ] [Clear] [Reset]  │
│  Decrease master ratio     [Meta+Ctrl+H     ] [Clear] [Reset]  │
│  Rotate layout             [Meta+R          ] [Clear] [Reset]  │
│                                                                 │
│  [Import Bismuth Shortcuts]   [Reset All to Defaults]          │
└─────────────────────────────────────────────────────────────────┘
```

### 4.4 Shortcut Configuration Schema

Add to `plasmazones.kcfg`:

```xml
<!-- Autotiling Global Shortcuts -->
<group name="AutotilingShortcuts">
  <entry name="ToggleAutotiling" type="String">
    <label>Toggle autotiling mode</label>
    <default>Meta+T</default>
  </entry>
  <entry name="CycleAlgorithm" type="String">
    <label>Cycle through algorithms</label>
    <default>Meta+Shift+T</default>
  </entry>
  <entry name="NavigationStyle" type="Int">
    <label>Navigation shortcut style (0=Vim,1=Arrow,2=Both)</label>
    <default>0</default>
    <min>0</min>
    <max>2</max>
  </entry>
  <entry name="FocusLeftShortcut" type="String">
    <label>Focus window to the left</label>
    <default>Meta+H</default>
  </entry>
  <entry name="FocusDownShortcut" type="String">
    <label>Focus window below</label>
    <default>Meta+J</default>
  </entry>
  <entry name="FocusUpShortcut" type="String">
    <label>Focus window above</label>
    <default>Meta+K</default>
  </entry>
  <entry name="FocusRightShortcut" type="String">
    <label>Focus window to the right</label>
    <default>Meta+L</default>
  </entry>
  <entry name="FocusMasterShortcut" type="String">
    <label>Focus master window</label>
    <default>Meta+M</default>
  </entry>
  <entry name="MoveLeftShortcut" type="String">
    <label>Move window left</label>
    <default>Meta+Shift+H</default>
  </entry>
  <entry name="MoveDownShortcut" type="String">
    <label>Move window down</label>
    <default>Meta+Shift+J</default>
  </entry>
  <entry name="MoveUpShortcut" type="String">
    <label>Move window up</label>
    <default>Meta+Shift+K</default>
  </entry>
  <entry name="MoveRightShortcut" type="String">
    <label>Move window right</label>
    <default>Meta+Shift+L</default>
  </entry>
  <entry name="SwapMasterShortcut" type="String">
    <label>Swap with master window</label>
    <default>Meta+Return</default>
  </entry>
  <entry name="IncreaseMasterRatioShortcut" type="String">
    <label>Increase master area ratio</label>
    <default>Meta+Ctrl+L</default>
  </entry>
  <entry name="DecreaseMasterRatioShortcut" type="String">
    <label>Decrease master area ratio</label>
    <default>Meta+Ctrl+H</default>
  </entry>
  <entry name="RotateLayoutShortcut" type="String">
    <label>Rotate layout 90 degrees</label>
    <default>Meta+R</default>
  </entry>
</group>
```

---

## 5. Visual Feedback Design

### 5.1 OSD (On-Screen Display) for Mode Changes

**Trigger conditions:**
- Autotiling enabled/disabled
- Algorithm changed
- Master ratio adjusted significantly
- Layout rotated

**OSD Design:**

```
┌──────────────────────────────────────────┐
│                                          │
│   ┌────────────────────────────────┐    │
│   │    [Layout Preview Graphic]     │    │
│   │    ┌─────────┐ ┌────┐          │    │
│   │    │         │ │    │          │    │
│   │    │  Master │ │    │          │    │
│   │    │         │ │    │          │    │
│   │    └─────────┘ └────┘          │    │
│   └────────────────────────────────┘    │
│                                          │
│         Autotiling: Master + Stack       │
│              Master: 55%                 │
│                                          │
└──────────────────────────────────────────┘
```

**OSD Properties:**
- Position: Center of active monitor (configurable)
- Duration: 1.5 seconds (configurable)
- Opacity: 90%
- Blur background: Matches existing PlasmaZones OSD
- Animation: Fade in (200ms), hold, fade out (300ms)

**Alternative OSD positions:**
- Top center
- Bottom center
- Near system tray
- Corner (configurable)

### 5.2 Mode Indicator (Persistent)

Small persistent indicator showing current mode:

```
Option A: Panel Widget
┌──────────────────────────────────────────────────┐
│ System Tray Area                    [M+S] [Time] │
└──────────────────────────────────────────────────┘
                                       ↑
                                  Mode indicator
                                  M+S = Master+Stack
                                  BSP = BSP mode
                                  ZON = Manual zones
```

```
Option B: Window Title Bar Badge
┌──────────────────────────────────────────────────┐
│ [App Icon] Window Title            [M] [-][□][×] │
└──────────────────────────────────────────────────┘
                                      ↑
                                 Master window indicator
```

```
Option C: Screen Edge Indicator
    ┌─────────────────────────────────────────┐
    │                                         │
────│                                         │
 ●  │              Desktop                    │
────│                                         │
    │                                         │
    └─────────────────────────────────────────┘
 ↑
Glowing dot on active monitor edge
Color indicates mode:
- Blue: Manual zones active
- Green: Autotiling active
- Orange: Hybrid mode
```

### 5.3 Preview Animations During Tiling

When a window is about to be tiled, show preview:

**Window Insertion Preview:**
```
Before insertion:                After insertion preview:
┌──────────┐ ┌──────────┐       ┌──────────┐ ┌────┐ ┌────┐
│          │ │          │       │          │ │    │ │NEW │
│    1     │ │    2     │  →    │    1     │ │  2 │ │    │
│          │ │          │       │          │ │    │ │    │
└──────────┘ └──────────┘       └──────────┘ └────┘ └────┘
```

**Animation properties:**
- Duration: 150ms
- Easing: OutQuad
- Ghost outline shows where new window will appear
- Existing windows animate to new positions

### 5.4 Focus Indication

When navigating with keyboard, highlight focused zone:

```
┌───────────────────┐ ┌──────────┐
│                   │ │          │
│                   │ │          │
│    ◆ FOCUSED ◆    │ │    2     │
│                   │ │          │
│                   │ │          │
└───────────────────┘ └──────────┘

◆ = Subtle animated corner markers
Or: Slightly thicker/brighter border on focused zone
```

**Focus indication properties:**
- Border color: System highlight color
- Border width: +2px when focused
- Animation: Subtle pulse (0.5s period)
- Corner markers: Small triangles in corners

### 5.5 Master Window Indication

In Master+Stack layouts, indicate which window is master:

```
┌────────────────────────┐ ┌──────────┐
│  ★                     │ │          │
│                        │ │    2     │
│      1 (Master)        │ ├──────────┤
│                        │ │    3     │
│                        │ ├──────────┤
│                   [M]  │ │    4     │
└────────────────────────┘ └──────────┘

★ = Small star icon in title bar (optional)
[M] = Badge in corner (optional)
```

---

## 6. Mode Coexistence Design

### 6.1 The Three Coexistence Modes

#### Mode 1: Replace (Default)

Autotiling completely takes over window management:

```
Manual Layout Active:          Autotiling Enabled:
┌─────┐ ┌─────────────┐       ┌────────────┐ ┌─────┐
│     │ │             │       │            │ │     │
│  1  │ │      2      │  →    │   Master   │ │  2  │
│     │ ├─────────────┤       │            │ ├─────┤
│     │ │      3      │       │            │ │  3  │
└─────┘ └─────────────┘       └────────────┘ └─────┘
                              Algorithm takes control
```

**Behavior:**
- Original layout is preserved in memory
- Disabling autotiling restores previous layout
- Zone editor unavailable while autotiling active

#### Mode 2: Hybrid (Advanced)

Autotiling operates within each manual zone:

```
Manual Layout:                 Hybrid Active:
┌───────────────┐ ┌─────┐     ┌───────────────┐ ┌─────┐
│               │ │     │     │  ┌─────┬────┐ │ │     │
│   Zone A      │ │  B  │  →  │  │ A1  │ A2 │ │ │  B  │
│               │ │     │     │  ├─────┼────┤ │ │     │
│               │ │     │     │  │ A3  │ A4 │ │ │     │
└───────────────┘ └─────┘     │  └─────┴────┘ │ └─────┘
                              └───────────────┘
                              Zone A has 4 windows auto-tiled
                              Zone B unaffected (1 window)
```

**Behavior:**
- Each zone becomes a mini-tiling container
- Zones with 1 window behave normally
- Zones with 2+ windows use selected algorithm
- Zone boundaries remain fixed
- Dragging between zones moves to new container

#### Mode 3: Toggle (Quick Switch)

Single shortcut switches between last manual layout and autotiling:

```
State A (Manual):             Meta+T:              State B (Autotiling):
┌─────┐ ┌───────────┐        ────────→            ┌────────┐ ┌────┐
│     │ │           │                             │        │ │    │
│  1  │ │     2     │        ←────────            │   M    │ │    │
│     │ ├───────────┤         Meta+T              │        │ ├────┤
│     │ │     3     │                             │        │ │    │
└─────┘ └───────────┘                             └────────┘ └────┘
```

**Behavior:**
- Instant switch between modes
- Both states are preserved
- Window positions animate smoothly between modes
- OSD shows which mode is now active

### 6.2 Visual Mode Differentiation

| Element | Manual Zones | Autotiling | Hybrid |
|---------|--------------|------------|--------|
| OSD Text | "Layout: [name]" | "Autotiling: [algo]" | "Hybrid: [layout] + [algo]" |
| Border Style | Solid | Dashed | Solid outer, dashed inner |
| Indicator Color | Blue | Green | Orange |
| Zone Numbers | User-defined | Auto (1,2,3...) | Zone letters, auto numbers |

---

## 7. First-Time User Experience

### 7.1 Onboarding Flow

**Trigger:** First launch after autotiling feature is available

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│                 Welcome to PlasmaZones Autotiling               │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                                                         │   │
│  │    [Animated preview of autotiling in action]          │   │
│  │                                                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  PlasmaZones now supports automatic window tiling!              │
│                                                                 │
│  Choose your experience:                                        │
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  ○ Keep using manual zones (current behavior)              ││
│  │    You control exactly where windows go                    ││
│  │                                                            ││
│  │  ○ Try autotiling                                          ││
│  │    Windows arrange automatically as you work               ││
│  │                                                            ││
│  │  ○ I'm a Bismuth/Krohnkite user                           ││
│  │    Set up familiar keyboard shortcuts                      ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  [Skip]                                           [Continue →] │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 Bismuth Migration Path

If user selects "I'm a Bismuth/Krohnkite user":

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│                    Bismuth Compatibility Mode                   │
│                                                                 │
│  We've detected you're coming from Bismuth. Let's set up       │
│  PlasmaZones to feel familiar.                                  │
│                                                                 │
│  ☑ Use vim-style navigation (Meta+H/J/K/L)                     │
│  ☑ Enable autotiling by default                                │
│  ☑ Use Master+Stack as default algorithm                       │
│  ☑ Set master ratio to 55% (Bismuth default)                   │
│  ☐ Import Bismuth shortcuts from system (if found)             │
│                                                                 │
│  Preview of your shortcuts:                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Focus: Meta + H/J/K/L                                  │   │
│  │  Move:  Meta + Shift + H/J/K/L                          │   │
│  │  Float: Meta + F                                        │   │
│  │  Swap:  Meta + Return                                   │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  [← Back]                                        [Apply Setup] │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 7.3 Interactive Tutorial

Optional tutorial available from Help menu:

```
Step 1 of 5: Basic Navigation
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  ┌──────────────┐ ┌─────────┐                           │   │
│  │  │              │ │         │  Try it: Press Meta+L     │   │
│  │  │   ◆ You're   │ │   →     │  to focus the window      │   │
│  │  │     here     │ │         │  on the right             │   │
│  │  │              │ │         │                           │   │
│  │  └──────────────┘ └─────────┘                           │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  Navigation lets you move focus between tiled windows using    │
│  the keyboard. No mouse needed!                                 │
│                                                                 │
│  ○ ○ ○ ○ ○                                                     │
│  ↑ Current step                                                │
│                                                                 │
│  [Skip Tutorial]                              [Next: Movement] │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 8. User Flows

### 8.1 Enable Autotiling Flow

```
                    ┌──────────────────┐
                    │   User opens     │
                    │   Settings       │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │  Navigate to     │
                    │  Behavior tab    │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │  Expand          │
                    │  Autotiling      │
                    │  section         │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │  Toggle          │
                    │  "Enable         │
                    │  autotiling"     │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
              ▼                             ▼
    ┌──────────────────┐          ┌──────────────────┐
    │  Settings panel  │          │  Windows on      │
    │  expands to show │          │  desktop         │
    │  algorithm       │          │  rearrange       │
    │  options         │          │  immediately     │
    └──────────────────┘          └─────────┬────────┘
                                            │
                                            ▼
                                  ┌──────────────────┐
                                  │  OSD shows       │
                                  │  "Autotiling:    │
                                  │   Master+Stack"  │
                                  └──────────────────┘
```

### 8.2 Keyboard Toggle Flow

```
                    ┌──────────────────┐
                    │  User presses    │
                    │  Meta+T          │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
    Currently Manual                  Currently Autotiling
              │                             │
              ▼                             ▼
    ┌──────────────────┐          ┌──────────────────┐
    │  Save current    │          │  Restore saved   │
    │  window          │          │  layout state    │
    │  positions       │          │                  │
    └────────┬─────────┘          └────────┬─────────┘
             │                             │
             ▼                             ▼
    ┌──────────────────┐          ┌──────────────────┐
    │  Calculate       │          │  Apply manual    │
    │  autotile        │          │  zone geometry   │
    │  positions       │          │  to windows      │
    └────────┬─────────┘          └────────┬─────────┘
             │                             │
             ▼                             ▼
    ┌──────────────────┐          ┌──────────────────┐
    │  Animate         │          │  Animate         │
    │  windows to      │          │  windows to      │
    │  new positions   │          │  zone positions  │
    │  (150ms)         │          │  (150ms)         │
    └────────┬─────────┘          └────────┬─────────┘
             │                             │
             └──────────────┬──────────────┘
                            │
                            ▼
                  ┌──────────────────┐
                  │  Show OSD with   │
                  │  current mode    │
                  └──────────────────┘
```

### 8.3 Window Navigation Flow

```
                    ┌──────────────────┐
                    │  User presses    │
                    │  Meta+L (right)  │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │  Get current     │
                    │  focused window  │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │  Find adjacent   │
                    │  window/zone in  │
                    │  direction       │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
        Found window                   No window found
              │                             │
              ▼                             ▼
    ┌──────────────────┐          ┌──────────────────┐
    │  Show brief      │          │  Show subtle     │
    │  highlight on    │          │  "bump" animation│
    │  target          │          │  at edge         │
    │  (100ms)         │          │                  │
    └────────┬─────────┘          └──────────────────┘
             │
             ▼
    ┌──────────────────┐
    │  Activate        │
    │  target window   │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │  Update focus    │
    │  indicators      │
    └──────────────────┘
```

---

## 9. Bismuth Migration Guide

### 9.1 Feature Mapping

| Bismuth Feature | PlasmaZones Equivalent | Notes |
|-----------------|------------------------|-------|
| Tile layout | Master + Stack algorithm | Same behavior |
| Three Column | Columns algorithm or 3-zone layout | Choice available |
| BSP layout | BSP algorithm | Same behavior |
| Spiral layout | Spiral algorithm | Same behavior |
| Monocle | Monocle algorithm | Same behavior |
| Floating windows | Meta+F toggle or exclusion list | Same UX |
| Master resize | Meta+Ctrl+H/L | Same shortcuts available |
| Window gaps | Inner/Outer gap settings | More control |
| Per-screen layouts | Per-monitor layouts | Existing feature |
| Window rules | Exclusion patterns | Similar capability |

### 9.2 Shortcut Comparison

| Action | Bismuth | PlasmaZones Default | PlasmaZones Bismuth Mode |
|--------|---------|---------------------|--------------------------|
| Focus left | Meta+H | Meta+Ctrl+Left* | Meta+H |
| Focus right | Meta+L | Meta+Ctrl+Right* | Meta+L |
| Focus up | Meta+K | Meta+Ctrl+Up* | Meta+K |
| Focus down | Meta+J | Meta+Ctrl+Down* | Meta+J |
| Move left | Meta+Shift+H | Meta+Alt+Left* | Meta+Shift+H |
| Move right | Meta+Shift+L | Meta+Alt+Right* | Meta+Shift+L |
| Swap master | Meta+Return | Meta+Return | Meta+Return |
| Toggle float | Meta+F | Meta+Alt+F* | Meta+F |
| Toggle tiling | Meta+T | Meta+T | Meta+T |
| Increase master | Meta+L | Meta+Ctrl+L | Meta+Ctrl+L |
| Decrease master | Meta+H | Meta+Ctrl+H | Meta+Ctrl+H |
| Cycle layout | Meta+\ | Meta+Shift+T | Meta+Shift+T |

*Current Phase 1 shortcuts - will be updated to match Bismuth in Bismuth mode

### 9.3 Migration Checklist for Users

```markdown
# Migrating from Bismuth to PlasmaZones

## Before You Start
- [ ] Note your preferred Bismuth layout (Tile, Three Column, BSP, etc.)
- [ ] Note your master ratio (default was 55%)
- [ ] Note your window gap settings
- [ ] Export any custom Bismuth shortcuts

## Installation
- [ ] Install PlasmaZones
- [ ] Remove Bismuth (if still installed)

## Initial Setup
- [ ] Open PlasmaZones settings (System Settings > Window Management > PlasmaZones)
- [ ] Navigate to Behavior tab > Autotiling
- [ ] Enable autotiling
- [ ] Select algorithm matching your Bismuth layout
- [ ] Set master ratio
- [ ] Configure window gaps

## Keyboard Shortcuts
- [ ] Go to Shortcuts tab > Autotiling Shortcuts
- [ ] Select "Vim-style (H/J/K/L)" navigation
- [ ] Or click "Import Bismuth Shortcuts"
- [ ] Test navigation: Meta+H/J/K/L
- [ ] Test movement: Meta+Shift+H/J/K/L
- [ ] Test float toggle: Meta+F

## Advanced Configuration
- [ ] Set up exclusions for floating apps
- [ ] Configure per-monitor settings if needed
- [ ] Try Hybrid mode if you want zone + autotiling

## What's Different
- PlasmaZones uses visual zones that you can also edit graphically
- Toggle Meta+T switches between autotiling and your custom layouts
- Zone editor available via Meta+Shift+E
- More visual feedback with OSD and zone previews

## Getting Help
- Documentation: [link]
- Community: [link]
- Issues: [GitHub issues link]
```

---

## 10. Technical Implementation Notes

### 10.1 Architecture Integration Points

**Existing components to extend:**

| Component | Location | Modifications |
|-----------|----------|---------------|
| Settings | `src/config/settings.h/cpp` | Add autotiling settings |
| kcfg | `src/config/plasmazones.kcfg` | Add config schema |
| KCM | `kcm/ui/main.qml` | Add UI sections |
| ShortcutManager | `src/daemon/shortcutmanager.h/cpp` | Add autotiling shortcuts |
| LayoutManager | `src/core/layoutmanager.h/cpp` | Handle autotiling layouts |
| ZoneOverlay | `src/ui/ZoneOverlay.qml` | Mode indicators |

**New components needed:**

| Component | Purpose |
|-----------|---------|
| `AutotileEngine` | Algorithm implementations |
| `AutotileLayout` | Dynamic layout generation |
| `AutotileOSD` | Mode-specific OSD |
| `MigrationHelper` | Bismuth settings import |

### 10.2 Algorithm Implementation Summary

Each algorithm generates zones dynamically based on window count:

```cpp
// Pseudocode for Master+Stack
QList<QRectF> calculateMasterStack(int windowCount, double masterRatio) {
    QList<QRectF> zones;
    if (windowCount == 0) return zones;

    // Master zone
    zones.append(QRectF(0, 0, masterRatio, 1.0));

    // Stack zones
    double stackWidth = 1.0 - masterRatio;
    double stackHeight = 1.0 / (windowCount - 1);
    for (int i = 1; i < windowCount; i++) {
        zones.append(QRectF(masterRatio, (i-1) * stackHeight,
                           stackWidth, stackHeight));
    }

    return zones;
}
```

### 10.3 OSD Integration

Extend existing OSD to support autotiling:

```qml
// In ZoneOverlay.qml or new AutotileOSD.qml
Item {
    id: autotileOsd

    property string mode: "autotiling"  // or "manual", "hybrid"
    property string algorithm: "Master + Stack"
    property int masterRatio: 55

    Rectangle {
        // OSD background
        color: Qt.rgba(0, 0, 0, 0.8)
        radius: 12

        Column {
            // Layout preview graphic
            LayoutPreview {
                algorithm: autotileOsd.algorithm
                windowCount: 4  // Show representative preview
            }

            // Mode text
            Text {
                text: autotileOsd.mode === "autotiling"
                    ? i18n("Autotiling: %1", autotileOsd.algorithm)
                    : i18n("Layout: %1", currentLayout.name)
            }

            // Master ratio (if applicable)
            Text {
                visible: autotileOsd.algorithm.includes("Master")
                text: i18n("Master: %1%", autotileOsd.masterRatio)
            }
        }
    }
}
```

---

## 11. Accessibility Considerations

### 11.1 Keyboard-Only Operation

All autotiling features must be fully operable via keyboard:

- [ ] All settings reachable via Tab navigation
- [ ] Mode toggle via Meta+T (no mouse required)
- [ ] Algorithm selection via keyboard in settings
- [ ] Preview visible without mouse interaction

### 11.2 Screen Reader Support

- [ ] OSD announcements via accessibility API
- [ ] Mode changes announced
- [ ] Focus changes announced
- [ ] All UI elements have proper labels

### 11.3 Visual Accessibility

- [ ] Mode indicators work with high contrast themes
- [ ] Color not sole indicator of mode (shapes/text also used)
- [ ] Animation can be disabled
- [ ] OSD respects reduced motion preferences

---

## 12. Future Considerations

### 12.1 Potential Enhancements (Post-MVP)

1. **Per-application algorithm rules** - Firefox always in monocle, terminals in columns
2. **Layout presets per activity** - Development activity uses BSP, Writing uses monocle
3. **Dynamic ratio based on content** - Detect video playback, adjust ratio
4. **Multi-monitor algorithms** - BSP across all monitors as one workspace
5. **Touch gesture support** - Swipe to change algorithm
6. **Neural layout prediction** - Learn user preferences over time

### 12.2 Integration Opportunities

- **KDE Activities** - Different autotile settings per activity
- **KRunner** - "tile windows bsp" commands
- **KDE Connect** - Change layout from phone
- **Plasma Widgets** - Quick toggle widget

---

## Appendix A: Algorithm Visual Reference

### Master + Stack
```
1 window:    2 windows:   3 windows:   4+ windows:
┌──────────┐ ┌──────┬───┐ ┌──────┬───┐ ┌──────┬───┐
│          │ │      │   │ │      │ 2 │ │      │ 2 │
│    1     │ │  1   │ 2 │ │  1   ├───┤ │  1   ├───┤
│          │ │      │   │ │      │ 3 │ │      │ 3 │
└──────────┘ └──────┴───┘ └──────┴───┘ │      ├───┤
                                       │      │ 4 │
                                       └──────┴───┘
```

### BSP (Binary Space Partitioning)
```
1 window:    2 windows:   3 windows:   4 windows:
┌──────────┐ ┌─────┬────┐ ┌─────┬────┐ ┌─────┬────┐
│          │ │     │    │ │     │  2 │ │  1  │  2 │
│    1     │ │  1  │  2 │ │  1  ├────┤ ├─────┼────┤
│          │ │     │    │ │     │  3 │ │  3  │  4 │
└──────────┘ └─────┴────┘ └─────┴────┘ └─────┴────┘
```

### Columns
```
1 window:    2 windows:   3 windows:   4 windows:
┌──────────┐ ┌─────┬────┐ ┌───┬───┬───┐ ┌──┬──┬──┬──┐
│          │ │     │    │ │   │   │   │ │  │  │  │  │
│    1     │ │  1  │  2 │ │ 1 │ 2 │ 3 │ │1 │2 │3 │4 │
│          │ │     │    │ │   │   │   │ │  │  │  │  │
└──────────┘ └─────┴────┘ └───┴───┴───┘ └──┴──┴──┴──┘
```

### Rows
```
1 window:    2 windows:   3 windows:   4 windows:
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│          │ │    1     │ │    1     │ │    1     │
│    1     │ ├──────────┤ ├──────────┤ ├──────────┤
│          │ │    2     │ │    2     │ │    2     │
└──────────┘ └──────────┘ ├──────────┤ ├──────────┤
                         │    3     │ │    3     │
                         └──────────┘ ├──────────┤
                                      │    4     │
                                      └──────────┘
```

### Spiral (Fibonacci)
```
1 window:    2 windows:   3 windows:   4 windows:   5 windows:
┌──────────┐ ┌─────┬────┐ ┌─────┬────┐ ┌─────┬────┐ ┌─────┬────┐
│          │ │     │    │ │     │    │ │     │  2 │ │     │  2 │
│    1     │ │  1  │  2 │ │  1  │  2 │ │  1  ├──┬─┤ │  1  ├────┤
│          │ │     │    │ │     ├────┤ │     │3 │4│ │     │3 ┌─┤
└──────────┘ └─────┴────┘ └─────┤  3 │ └─────┴──┴─┘ └─────┴──┤5│
                               └────┘                       └─┘
```

### Monocle
```
All windows fullscreen, stacked:
┌──────────────────────────────┐
│                              │
│       Active Window          │
│       (others hidden)        │
│                              │
│                          [●○○] ← window count indicator
└──────────────────────────────┘
```

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **Autotiling** | Automatic window arrangement using algorithms |
| **Manual zones** | User-defined screen regions for window snapping |
| **Master area** | Primary focus area in Master+Stack layout |
| **Stack area** | Secondary area for supporting windows |
| **BSP** | Binary Space Partitioning - recursive halving |
| **Monocle** | Single fullscreen window with others hidden |
| **Float** | Window exempt from tiling |
| **OSD** | On-Screen Display - temporary notification overlay |
| **KCM** | KDE Control Module - settings interface |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-24 | UX Designer | Initial design specification |

---

*This document is part of the PlasmaZones project documentation.*
