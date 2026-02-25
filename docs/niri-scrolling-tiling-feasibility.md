<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: CC0-1.0
-->
# Feasibility Analysis: Niri-Style Scrolling Auto-Tiling in PlasmaZones

**Date:** 2026-02-22
**Status:** Proposal
**Author:** AI-assisted analysis

---

## TL;DR

**Feasibility: High — architecturally natural fit, moderate implementation effort.**

PlasmaZones already has ~80% of the infrastructure needed. The scrolling model maps cleanly onto the existing autotile framework as a new algorithm + viewport layer. The main challenge isn't "can it be done" but rather working around KWin's assumptions about windows fitting within screen bounds.

---

## 1. What Niri's Scrolling Model Is

Niri is a Wayland compositor that arranges windows on an **infinite horizontal strip**. A viewport (like a camera) shows one screen-width slice. Two core invariants:

1. **Opening a new window never resizes existing windows** — it appends a new column
2. **The focused window never moves on its own** — the viewport shifts instead

### Data Model

```
Workspace
  ├── columns: Vec<Column>       // the horizontal strip
  ├── view_offset: f64           // horizontal scroll position (pixels)
  ├── active_column_idx: usize   // which column has focus
  │
  └── Column
      ├── tiles: Vec<Tile>       // vertically stacked windows
      ├── width: ColumnWidth     // preset fraction or fixed pixels
      └── Tile
          └── window: Window
```

Each **monitor** has its own independent set of **workspaces** arranged vertically. Each **workspace** contains the infinite horizontal strip of **columns**. Each **column** contains one or more vertically stacked **tiles** (windows).

### Key UX Behaviors

- **Window addition:** Creates a new column to the right of the focused column. Existing windows maintain their dimensions. Viewport scrolls to reveal the new column.
- **Window removal:** Column removed if empty. Focused window does not shift — viewport adjusts.
- **Window resizing:** Explicit and user-initiated only. No automatic resizing ever occurs. Users cycle through preset widths (1/3, 1/2, 2/3 of screen).
- **Viewport scrolling:** Triggered by keyboard navigation, touchpad gestures, or mouse wheel with modifier.
- **Centering modes:** `never` (scroll to nearest edge), `always` (focused column centered), `on-overflow` (center only if column doesn't fit alongside predecessor).

### How It Differs from Traditional Tiling

| Aspect | i3/Sway (Traditional) | Niri (Scrolling) |
|---|---|---|
| Window sizing on add | All siblings resize | Existing windows never change |
| Screen space | Fixed to monitor | Infinite horizontal strip |
| Layout tree | Binary split tree | Flat column list |
| Focus stability | Focused window may shift | Focused window never moves |
| Viewport | Shows all windows (they shrink) | Shows a window into the strip |
| New window placement | Splits focused container | Appends column next to focus |

---

## 2. What PlasmaZones Already Has

| Needed Component | PlasmaZones Status |
|---|---|
| Per-screen tiling state | `TilingState` — window order, master count, split ratio |
| Algorithm plugin system | `TilingAlgorithm` base class, 7 implementations |
| Per-screen autotile engine | `AutotileEngine` with window open/close/focus events |
| Window animation | `WindowAnimator` in the KWin effect |
| Geometry application | Effect plugin applies `setFrameGeometry()` via D-Bus |
| Gap system | Inner + per-side outer gaps, smart gap collapse |
| Session persistence | KConfig `[AutoTileState]` section |
| Keyboard shortcut framework | `ShortcutManager` with global shortcuts |
| Multi-monitor isolation | Per-screen independent state |

### Relevant Source Files

| File | Role |
|---|---|
| `src/autotile/TilingState.h/cpp` | Per-screen autotile state |
| `src/autotile/TilingAlgorithm.h/cpp` | Algorithm base class |
| `src/autotile/AutotileEngine.h/cpp` | Autotiling orchestration |
| `src/autotile/algorithms/` | 7 built-in algorithm implementations |
| `kwin-effect/plasmazoneseffect.h/cpp` | KWin effect (geometry application, animation) |
| `kwin-effect/WindowAnimator.h/cpp` | Smooth window transitions |
| `src/daemon/ShortcutManager.h/cpp` | Global keyboard shortcuts |
| `src/core/geometryutils.h/cpp` | Shared geometry calculations |

---

## 3. What Needs to Be Built

### 3.1 ScrollingAlgorithm (new TilingAlgorithm subclass)

**Effort: Medium**

Unlike current algorithms that return zones fitting *within* `screenGeometry`, this one:

- Maintains a list of columns with explicit widths (preset fractions or fixed px)
- Computes column positions along an unbounded X axis
- Returns zone rects that may extend **beyond screen bounds**
- Tracks `viewOffset` to determine the visible window

This is the most natural integration point — it plugs into `AutotileEngine` like any other algorithm.

```cpp
class ScrollingAlgorithm : public TilingAlgorithm {
public:
    QVector<QRect> calculateZones(const TilingParams &params) const override;

    // Scrolling-specific
    bool supportsViewport() const override { return true; }
    double viewOffset() const;
    int activeColumnIndex() const;

    // Column operations
    void setColumnWidth(int columnIndex, ColumnWidth width);
    void consumeWindowIntoColumn(int windowIndex, Direction dir);
    void expelWindowFromColumn(int windowIndex);
};
```

### 3.2 Viewport State in TilingState

**Effort: Low**

Extend `TilingState` with:

```cpp
// Scrolling viewport state
double viewOffset = 0.0;           // horizontal scroll position (pixels)
int activeColumnIndex = 0;         // focused column
QVector<ColumnInfo> columns;       // column widths + tile counts
CenterMode centerMode = CenterMode::Never;

struct ColumnInfo {
    double width;                  // absolute pixels or fraction
    bool isFraction;               // true = fraction of screen width
    QVector<int> windowIndices;    // windows stacked in this column
};
```

### 3.3 Viewport-Aware Geometry in KWin Effect

**Effort: Medium-High — this is the crux**

Currently the effect applies zone geometries directly. For scrolling, it must:

- Translate all window X positions by `-viewOffset`
- Clip or hide windows that fall entirely off-screen
- Animate `viewOffset` changes with spring physics

**Key concern:** How does KWin handle windows positioned off-screen?

- KWin allows `setFrameGeometry()` to place windows off-screen
- The compositor skips rendering off-screen windows (good for performance)
- Partially visible windows get clipped naturally
- KWin's "keep windows on screen" behavior may need to be suppressed — overridable via window rules or effect flags
- **Karousel (KWin script) already does this successfully, proving it works**

### 3.4 Spring Physics Animation

**Effort: Low-Medium**

PlasmaZones already has `WindowAnimator`. Add spring-physics for `viewOffset`:

```cpp
class SpringAnimation {
    double value, target, velocity;
    double stiffness, damping;

    void step(double dt) {
        double force = -stiffness * (value - target) - damping * velocity;
        velocity += force * dt;
        value += velocity * dt;
    }

    bool isFinished() const {
        return std::abs(value - target) < epsilon
            && std::abs(velocity) < epsilon;
    }
};
```

Spring animations are preferred over easing for scrolling because they incorporate gesture velocity for natural momentum.

### 3.5 Keyboard Actions

**Effort: Low**

New shortcuts via `ShortcutManager`:

| Action | Description |
|---|---|
| `FocusColumnLeft/Right` | Shift active column + animate viewport |
| `MoveColumnLeft/Right` | Reorder columns in the strip |
| `ConsumeWindowIntoColumn` | Move window into adjacent column (vertical stack) |
| `ExpelWindowFromColumn` | Move window out of column into its own |
| `SwitchPresetColumnWidth` | Cycle through 1/3, 1/2, 2/3 screen width |
| `CenterColumn` | Center focused column on screen |
| `FocusWindowUp/Down` | Navigate within a multi-window column |
| `MaximizeColumn` | Expand column to full screen width |

### 3.6 Touchpad Gesture Integration

**Effort: Medium-High**

As a C++ KWin effect, PlasmaZones can:

- Listen to gesture events from KWin's input layer
- Map 3-finger horizontal swipe to `viewOffset` changes with gesture velocity
- Spring animation handles momentum on finger release

KWin's gesture API for effects is limited — may need to register gesture recognizers or hook into `PointerInputRedirection`. **Not a blocker** — keyboard-only is a viable MVP.

### 3.7 KCM Settings Integration

**Effort: Low**

Add to the autotile settings page:

- Algorithm selection: "Scrolling" option alongside existing algorithms
- Default column width preset list (e.g., `[1/3, 1/2, 2/3]`)
- Center mode selector (Never / Always / On Overflow)
- Gap configuration (already exists, reuse)

---

## 4. Challenges & Risks

### KWin "Keep on Screen" Behavior — Risk: Medium

KWin has logic to prevent windows from being placed off-screen. Mitigations:
- `setFrameGeometry()` from an effect should bypass placement policies
- Window properties/rules can suppress this per-window
- Karousel already proves this works from KWin scripting

### Task Switcher / Overview Interaction — Risk: Low-Medium

KWin's overview and Alt+Tab show all windows regardless of position. This is expected and matches Niri's behavior — all windows visible in overview even if scrolled off the strip.

### Interaction with Existing PlasmaZones Modes — Risk: Low

Scrolling is one more autotile algorithm option. `ModeTracker` already handles switching between manual zones and autotile. Users select it from KCM like any other layout.

### Performance During Scroll — Risk: Low

KWin naturally skips rendering off-screen windows. During scroll animation, only visible windows need geometry updates. The existing D-Bus batch geometry signal (`windowsTileRequested`) handles multiple window moves atomically.

### Multi-Monitor — Risk: Low

PlasmaZones already has per-screen independent autotile state. Each monitor would have its own scrolling strip, matching Niri's model exactly.

---

## 5. Prior Art: Karousel

[Karousel](https://github.com/peterfajdiga/karousel) is a KWin *script* (TypeScript/JS) implementing scrollable tiling. It proves the concept works on KWin but has limitations:

- **No multi-monitor support**
- No touchpad gesture scrolling
- Limited animation control (JS scripting layer)
- No integration with zone/snap systems

PlasmaZones as a C++ effect has strictly more capability — direct access to input events, GPU-accelerated animation, and the full KWin effect API.

---

## 6. Implementation Plan

| Phase | Scope | Effort Estimate |
|---|---|---|
| **Phase 1: Core algorithm** | `ScrollingAlgorithm` + viewport state in `TilingState` + basic keyboard nav | 2-3 weeks |
| **Phase 2: Effect integration** | Viewport-aware geometry in KWin effect + spring animation | 2-3 weeks |
| **Phase 3: Column operations** | Consume/expel, width presets, centering modes | 1-2 weeks |
| **Phase 4: Gesture support** | Touchpad horizontal scroll + momentum | 1-2 weeks |
| **Phase 5: KCM integration** | Settings UI for scrolling mode, width presets, centering | 1 week |

**MVP (Phases 1-2): ~4-6 weeks** for a keyboard-driven scrolling tiler.

**Full feature (Phases 1-5): ~8-11 weeks** including gestures and polish.

---

## 7. Conclusion

Implementing Niri-style scrolling auto-tiling in PlasmaZones is **very feasible** and architecturally well-suited:

- The autotile plugin system was designed for exactly this kind of extension
- Being a C++ KWin effect gives capabilities Karousel can't match (gestures, smooth animation, multi-monitor)
- The viewport concept is a contained addition to the existing geometry pipeline
- Off-screen window placement is proven viable on KWin by Karousel
- It would be a genuinely differentiating feature — no other KDE tool combines zone snapping with scrolling auto-tiling

### References

- [Niri GitHub Repository](https://github.com/niri-wm/niri)
- [Niri Design Principles](https://github.com/niri-wm/niri/wiki/Development:-Design-Principles)
- [Niri Layout Configuration](https://github.com/niri-wm/niri/wiki/Configuration:-Layout)
- [Niri Animations Configuration](https://github.com/niri-wm/niri/wiki/Configuration:-Animations)
- [Karousel KWin Script](https://github.com/peterfajdiga/karousel)
- [PaperWM GNOME Extension](https://github.com/paperwm/PaperWM)
- [DeepWiki: Niri Window & Layout Management](https://deepwiki.com/YaLTeR/niri/2.1-window-and-layout-management)
