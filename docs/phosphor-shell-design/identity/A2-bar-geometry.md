<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# A2 — Bar as Placement Map, Spectrum Rail, Engine-Placed Popouts

Status: design spec, read-only research against the shell-design worktree (2026-09-04).
Scope: the bar surface (`libs/phosphor-shell-bar`), its placement widget, its popouts.

Seed (as corrected): three things and nothing else.

1. **The Phosphor spectrum, used structurally.** Cyan `#22D3EE` → blue `#3B82F6` →
   purple `#A855F7` → rose `#F43F5E` on navy. In this spec the gradient is an *axis*:
   hue encodes where a window is on the screen, or along the strip. Colour is never
   decoration and never a mood.
2. **The look the packs already share** (`data/surface/phosphor-glass`,
   `phosphor-motes`, `border-phosphor`; `data/overlays/phosphor-flux`, `prismata`,
   `spectrum-bloom`): dark navy glass over a real blur, the four-stop gradient flowing
   along frames, a gleam travelling the band, labels lit against navy.
3. **Owning the placement engine.** The bar can draw the engine's geometry and can ask
   the engine to place its own popouts. No other shell can.

Motion vocabulary: asymmetric envelopes, named plainly: **enter** (fast, 100–200 ms,
decelerated), **settle** (the last 20 % of an enter, no overshoot), **hold**,
**release** (slow, 400–800 ms, decelerated fade), **follow** (bound 1:1 to a pointer or
to engine geometry, no easing). Shared-element chip-to-drawer morphs and springs are
deliberately not used as signatures.

Everything below names real engine/D-Bus surfaces. **[NEW]** marked a surface that did
not exist when this document was written, against the interface it belongs on. All of
them have since been built and shipped in phases 1 to 6, so the marker is now a record
of what this document asked for rather than a list of work outstanding.

---

## 0. Thesis

The bar is a **spectrum rail**: a 2 px line at the screen edge carrying the brand gradient
end to end, so the rail is the screen's x-axis painted in the spectrum. Below it hangs a
26 px strip of navy glass with no outline and no corners. The workspace dots are replaced
by a **placement map**: a live, aspect-true miniature of the engine's real geometry for
this screen, where every cell takes its hue from the rail directly above the window it
stands for. Popouts are **panes the engine places** (a zone, a tile, or a column), tied
back to their chip by a 2 px tether that carries the rail's hue at that x.

Colour tells you where a window is. The map tells you how the screen is cut. The engine
tells you both, because it is the same engine that cut it.

---

## 1. The Placement Map

### 1.1 Silhouette and size

| Context | Map height | Map width | Notes |
|---|---|---|---|
| Rest (band 26 px) | 18 px | `18 × screenAspect`, clamped to [24, 56] px | 16:9 → 32 px. 21:9 → 42 px. 32:9 clamps at 56 and letterboxes. |
| Hover / keyboard focus | 18 → 22 px | scales with it | Enter over 150 ms decelerated. Height comes out of the band, never out of the exclusive zone. |
| Expanded (long-press, or `Meta+Tab` held) | 96 px | `96 × aspect`, clamped 320 px | Becomes an engine-placed pane (§4) with window titles and the desktop filmstrip (§1.5). |
| Vertical bar | 24 × 14 px | | Always drawn in the screen's real orientation; a vertical bar stacks a small landscape map. |

The map has no frame. Its outer edge is the screen edge, drawn as a 1 px line at 14 %
`Theme.on_surface`. The work area (`org.plasmazones.Screen.getAvailableGeometry`) is what
is subdivided; the bar's own exclusive strip shows as a 1 px dead band at the top of the
map, so the map honestly contains itself.

Desktop ticks: beneath the map, one 2 × 1 px tick per virtual desktop
(`Shell.Workspaces.model`), 2 px apart, centered. The current desktop's tick is white at
90 %; others at 25 %. This is the only trace of the old dots, demoted to a ruler.

### 1.2 The hue axis (what colour means)

The rail (§3) paints the gradient across the bar's full length. The map is drawn in the
same colour space:

- **Snapping and tiling**: a cell's hue is sampled from the gradient at the cell's
  horizontal centre in work-area space (0 = cyan, 1 = rose). A left-half zone is cyan,
  a right-half zone is rose-purple, a centre column is blue. The rail above the *real*
  window on screen is the same hue, so the bar and the window agree.
- **Scrolling**: the strip is the axis. Hue is `stripPosPx / stripExtentPx`, so the
  first column is cyan and the last is rose regardless of what is currently visible.
  The lens shows a window into that gradient, and the rail (§3.3) shows the same window
  at full width. Panning slides the hue under the lens. A column you cannot see still
  has a colour, and the overflow gutters show it.
- **Vertical strips**: the axis is y; the rail runs along the vertical bar edge.

Two neutral signals sit outside the hue axis so they can never be confused with
position: **white** for focus and **white pulse plus rail thickening** for urgency.

### 1.3 What is drawn, per mode

The mode comes from `org.plasmazones.LayoutRegistry.getScreenStates()` (per-screen
`mode` 0/1/2 with the resolved `layoutId` / `algorithmId` / `scrollingTemplateId`),
refreshed on `screenLayoutChanged`, `activeLayoutForScreenChanged` and
`assignmentChangesApplied`. Assignments resolve per (screen, desktop, activity), so a
desktop switch re-reads the same call. There is no separate "mode changed" signal.

#### Snapping

- Geometry: `LayoutRegistry.getLayoutForScreen(screenId)` → `getLayout(uuid)` JSON;
  each zone's `relativeGeometry` (0..1 of the work area) becomes a cell rect. Fixed-mode
  zones (`geometryMode`, `fixedGeometry`) are scaled by the map's px-per-screen-px.
  Overlapping zones draw the later zone's outline over the earlier zone's fill at 50 %.
- Occupancy: `org.plasmazones.WindowTracking.getAllWindowStates()` seeded once, then
  `windowStateChanged(windowId, WindowStateEntry{zoneId, zoneIds, isFloating, ...})`.
  A zone with ≥ 1 non-floating window is filled. Multi-zone spans (`zoneIds` with more
  than one entry) fill every listed zone and merge them into one shape (shared edges
  dropped); the merged shape's hue is sampled at the merged centre.
- Cell label (expanded height only): app glyph of the topmost window. Titles and appIds
  come from `Shell.Toplevels` (`ForeignToplevel.appId`, `title`, `activated`).
  **Gap**: `WindowStateEntry` carries the KWin `windowId`; `ForeignToplevel` carries no
  KWin id. **[NEW]** add `appId` and `title` to `WindowStateEntry` (the daemon has them
  via `setWindowMetadata`). Rest height needs no titles and works today.
- Empty zone: outline only, in its hue at 30 %.
- Floating windows are not drawn. Float is per-mode state and the map is the engine's
  view. A "+N" tabular superscript at the map's top-right at expanded height only.

#### Tiling

- Geometry: the last `org.plasmazones.Tiling.windowsTileRequested(TileRequestList)`
  batch naming this `screenId`; absolute `x,y,width,height` normalised against
  `Screen.getScreenGeometry(screenId)`. `floating=true` entries dropped;
  `monocle=true` entries share the full rect and draw as one cell with a stack count.
- **[NEW]** `Tiling.currentTilesJson(screenId)`: replay of the last batch, the twin of
  `Scrolling.visibleStripJson`, so a shell starting after the last retile does not draw
  an empty map until the next `tilingChanged`.
- Wake-up: `Tiling.tilingChanged(screenId)` → re-read.
- Every cell is occupied by definition. The tree is not drawn as a tree; the rects are
  the tree, and hue along x makes the master/stack split read without a label.
- Algorithm identity is not on the map. It lives in the right-click menu and the
  expanded header.

#### Scrolling

- Geometry: a horizontal band the full map width. Columns are vertical slivers
  proportional to resolved width; a **lens** rectangle (1 px white outline at 70 %)
  marks the viewport. The map allots 70 % of its width to the lens and 15 % to each
  overflow gutter; columns beyond an edge pack into their gutter at 1 px each, in their
  own hue, so the gutter reads as a compressed slice of the gradient (§7 for 40
  columns). Vertical strips rotate the drawing 90°.
- Data today: `org.plasmazones.Scrolling.visibleStripJson(screenId)` gives visible
  tiles only (0..1 rects with `zoneNumber`). Enough for the lens, not for overflow or
  for the hue axis (which needs `stripExtentPx`).
- **[NEW]** `Scrolling.stripModelJson(screenId)` →
  `{axis, viewOffsetPx, viewportPx, stripExtentPx, activeColumn, columns:[{stripPosPx,
  extentPx, display, activeTile, maximized, tiles:[{windowId, crossPx, minimized}]}]}`.
  A straight serialisation of `ScrollStrip::columns()`, `columnStripPos()`,
  `stripExtentPx()`, `viewOffsetFor()`, `activeColumnIndex()`. Nothing new is computed.
- Wake-ups: `Scrolling.stripChanged(screenId)` (treated as "re-read soon" per its
  docstring) and `stripContextChanged(screenId, epoch)` (identity changed; drives the
  desktop-switch transition in §2.4, not the mode morph). The map keeps the last epoch
  per screen and compares for equality only.
- Tabbed columns (`display == 1`): one sliver with `tiles.length` hairline ticks along
  its top edge; the active tab's tick is white.
- Minimized tiles: absent, matching `visibleStripJson`.
- Blueprint progress (`blueprintProgressJson`): at expanded height, `total - used`
  unfilled template slots draw as dotted ghost columns after the last real one, in the
  hue they would take.

### 1.4 Cell states

Hue is always the cell's position hue (§1.2). Only lightness, fill and the white
signals change.

| Cell state | Fill | Edge | Extra |
|---|---|---|---|
| Empty (snapping) | none | 1 px hue @ 30 % | |
| Occupied | hue @ 40 % | hue @ 80 % | |
| Holds the focused window | hue @ 55 % | **white** 1 px @ 90 % | 2 px inner white core line along the top edge |
| Hovered | +10 % lightness | edge @ 100 % | cursor is a hand |
| Urgent | hue @ 40 % | white 1 px, pulsing 0.4 → 1.0 at 1.2 s | rail thickens above the cell (§3.3) |
| Disabled (context disabled / `none`) | none | `on_surface` @ 8 %, dashed 2/2 | |
| Occupied by a pane (§4) | hue @ 30 % | white 1 px @ 60 %, dashed 1/1 | |

Urgency source: `ForeignToplevel` exposes title/appId/state only. **[NEW]**
`ForeignToplevel.demandsAttention` (the compositor already resolves urgency for the tab
pills). Until it exists urgent never fires.

Focus source: `IPlacementEngine::managedFocusedWindow(screenId)` exists on every
engine; surface it as **[NEW]** `Tiling.managedFocusedWindow(screenId)` plus
`focusedWindowChanged(screenId, windowId)`. Mode-agnostic and already tracked.

### 1.5 Interactions

Every action routes through the daemon so it behaves exactly as the keyboard verbs do.
Nothing on the map bypasses the mode router.

| Gesture | Snapping | Tiling | Scrolling |
|---|---|---|---|
| **Click occupied cell** | Activate the topmost window in that zone. A second click on the same zone cycles: `Snap.cycleWindowsInZone`. | Activate that window. | Focus that column. **[NEW]** `Scrolling.focusColumnAt(screenId, index)` (`ScrollStrip::focusColumn(int)` exists; the adaptor only exposes ±1). |
| **Click empty cell** | Snap the focused window there: `Snap.moveWindowToZone(focusedId, zoneId)`. | n/a | Click in an overflow gutter → `scrollView(screenId, ±1)` toward that side. |
| **Double-click cell** | `LayoutRegistry.openEditorForLayoutOnScreen`. | `Autotile.promoteToMaster`. | Focus, then `Scrolling.toggleMaximizeColumn(screenId)`. |
| **Drag cell → cell** | `Snap.swapWindowsById` if the target is occupied, else `Snap.moveWindowToZone`. The dragged cell follows the pointer as a 60 % copy in its hue; the target cell's edge goes white. | `Autotile.swapWindows(a, b)`. | **[NEW]** `Scrolling.moveColumnTo(screenId, from, to)` (`moveActiveColumnTo` exists on the strip). Drop *between* columns inserts; drop *onto* a column with Shift consumes as a tab (`consumeWindowIntoColumn` path). |
| **Drag a real window onto the map** | The map is a **drop proxy** for the compositor's own drag. **[NEW]** `WindowDrag.registerDropProxy(screenId, rect)`: the shell registers the map's absolute rect; the daemon already receives `updateDragCursor`; when the cursor is inside a proxy it maps to the cell under it and highlights the *real* zone on screen through `Overlay.highlightZone`, so the user points at the miniature and sees the full-size target light up. `endDrag` inside the proxy commits. | Same, via `computeDragInsertTargetAtPoint` in map space. | Same, including insertion between *off-lens* columns: you can drop into a column you cannot see. |
| **Wheel over the map** | `Snap.focusAdjacentZone` right/left in reading order. | `Autotile.focusNext` / `focusPrevious`. | **Pans the real strip**: `Scrolling.scrollView(screenId, ±1)` per notch (the engine's detached-view pan; focus stays). Shift+wheel: `focusColumn(screenId, ±1)`. |
| **Drag the lens** | | | Press inside the lens and drag; the view follows 1:1 in strip space. **[NEW]** `Scrolling.scrollViewByPx(screenId, px)` (`ScrollStrip::scrollViewBy(int)` exists). Deltas coalesce to one call per frame. On release the view stays (the engine's view-detached latch). Overscroll past the strip end moves the *lens* only, up to 24 px, and releases back over 300 ms decelerated with no overshoot. |
| **Ctrl+wheel, or wheel on the ticks** | Desktop switch: `Shell.Workspaces.switchTo(id)`. | ↑ | ↑ |
| **Middle-click cell** | `Snap.toggleFloatForWindow`. | `WindowTracking.setWindowFloatingForScreen`. | Same. |
| **Right-click** | Menu: layout list (`getLayoutPreviewList`, previews drawn by the same map painter in the same hue axis), Edit layout, Assign for this desktop only (`assignLayoutToScreenDesktop`), Mode submenu, Snap all windows. | Algorithm list (`availableAlgorithms` + `algorithmInfo`), master ratio and count steppers, Retile. | Template list (`getScrollingTemplates`), Center column, Reset strip (`resetStripToDefaults`), width presets (`presetVocabularyJson`). |
| **Long-press / `Meta+Tab` held** | Expanded pane (§1.1). | ↑ | ↑ |

Keyboard: the bar's keyboard layer is dead today (`keyboardFocus: PanelWindow.None`).
When a shortcut gives the bar focus (`Meta+B`), the map is the first focus stop. `←→↑↓`
move a cursor cell (in scrolling, `←→` cross columns including off-lens ones and the lens
follows via `scrollViewByPx`), `Enter` = click, `Space` = float toggle, `Shift+arrows` =
drag-move, `Ctrl+←→` = desktop switch, `Esc` returns focus to the WM. Every cell carries
`Accessible.name`: "Zone 3: Firefox", "Column 2 of 7: Terminal, Editor (tabbed)".

### 1.6 Desktops and monitors

- **One map per bar per screen**, showing only that screen. No composite of all
  monitors on the bar; the control center gets the full-topology page.
- **The map is per desktop.** It shows the current (screen, desktop, activity) context
  and re-reads on `screenLayoutChanged` / `stripContextChanged` / `Workspaces.activeChanged`.
  It does not scroll through desktops itself: desktops are not spatial neighbours in
  this WM (one screen may run scrolling on desktop 1 and snapping on 2), so a desktop
  carousel would lie about adjacency. The ticks are the desktop control.
- At expanded height the pane grows a **desktop filmstrip**: one small map per desktop,
  rendered from `getLayoutForScreenDesktopActivity` plus that desktop's windows, the
  current desktop at full size. Click switches. Dragging a cell from one desktop's map
  to another moves the window there (**[NEW]** `WindowTracking.moveWindowToDesktop`;
  today `windowDesktopMoveRequested` is signal-only).
- Sticky-pinned screens resolve to the *pinned* desktop (`stickyPinnedDesktopForScreen`).
  The map draws the pinned desktop's geometry and rings its tick, never the live
  desktop's. The `stripContextChanged` docstring is explicit that a consumer must never
  derive this from the compositor's current desktop.

---

## 2. The Mode Morph

The map is one shape. When the screen's resolved mode changes (assignment edit, a desktop
switch onto a differently-moded context, a rule), the shape morphs. The envelope is
asymmetric: the new geometry **enters** fast, the old geometry **releases** slowly, and
edges the two share are handed over without a break.

### 2.1 Phases (800 ms total, interactive from 0 ms)

| t | Phase | What happens |
|---|---|---|
| 0 ms | **Enter (new)** | New geometry is computed and is the hit target immediately. New cells scale 0.7 → 1.0 from their own centres, 180 ms decelerated, staggered 15 ms per cell in reading order (stagger capped at 150 ms). Fill and edge come in with them. Hue is sampled at the *final* position from frame 0, so colour never slides. |
| 0–80 ms | **Hold (old)** | Old cells freeze in place. Fill drops to 0 over 80 ms accelerated; edges stay. |
| 180–330 ms | **Settle** | New cells reach 1.0. No overshoot. |
| 80–800 ms | **Release (old)** | Old edges fade 80 → 0 % over 720 ms decelerated. **Matched edges are exempt**: an old edge within 6 % of map width of a new edge of the same orientation is kept and re-parented to the new geometry, so a 50/50 split present in both the snapping layout and the tiling result never blinks. This is what makes the morph read as one shape rather than a crossfade. |
| 800 ms | Done | Only the new geometry remains. |

### 2.2 Per-transition specifics

- **Snap grid → tile tree**: near-identical shapes; matched edges dominate. The visible
  event is empty outlines becoming filled cells.
- **Tile tree → strip**: the lens outline enters first (120 ms, from the map's centre),
  then columns, then the overflow gutters last (400–800 ms), so the user reads "your
  windows now continue past the edge". The rail's gradient window (§3.3) re-binds to the
  strip at 0 ms and slides to its new position over the same 800 ms.
- **Strip → snap grid**: the lens releases first (200 ms), columns hold, zones enter.
  Overflow gutters release fastest. The rail's gradient un-binds and settles to the
  static full-width gradient over 800 ms.
- **Anything → disabled/none**: everything releases to the dashed 8 % outline over
  600 ms; nothing enters.

### 2.3 Interruption

A mode change mid-morph does not queue. The current rendered state (partly entered new
cells, partly released old edges) becomes the outgoing generation for the next morph.
At most two generations are ever drawn, the live one and the releasing one. A third
arrival drops the oldest instantly.

### 2.4 Desktop switch within the same mode

Not a mode morph. 200 ms: old fill to 0 over 60 ms, new cells fade in without scale,
matched edges kept. Scrolling uses the `stripContextChanged` epoch comparison; snapping
and tiling use the re-read after `Workspaces.activeChanged`. If the new context's
geometry is byte-identical, nothing animates.

### 2.5 Occupancy and focus changes

Not a morph. A cell becoming occupied enters its fill over 150 ms decelerated. A cell
emptying releases its fill over 400 ms. Focus moving between cells is a hand-over: the
new cell's white edge enters over 100 ms, the old cell's white edge releases over 400 ms,
so for a moment both are marked and the eye reads direction. In scrolling, a focus
change that moves the lens moves it with the engine's own view slide (`viewDelta` on the
tile batch is the truth; the map follows it rather than easing on its own).

---

## 3. Bar Silhouette: the Spectrum Rail

### 3.1 Decision

**The bar is a spectrum rail on navy glass.** A 2 px line at the screen edge carrying the
brand gradient end to end, and a 26 px band of `phosphor-glass` material below it with no
outline, no corner radius, no inset and no fill colour of its own beyond the glass.

Against the DMS slab: a full-width opaque bar is a container, and a popout growing out of
it through a concave corner is DMS's signature. It also says the bar is a solid the
windows live under. Here the engines own the space and the bar is a reservation in it.

Against the Noctalia island: an inset floating capsule is a window, and this shell's
windows are placed by engines. A capsule 24 px in from the edge is a window the engine
did not place. Pill-inside-pill chips are also the most cloned silhouette in the field.

Why the rail is ownable: it is the brand gradient the packs already put on every window
border (`border-phosphor`) and every zone frame (`phosphor-flux`), drawn once more along
the screen's own edge, and here it is *load-bearing*: it is the axis every map cell, every
chip and every popout tether samples its colour from. Nobody else's bar has a colour axis
because nobody else's bar knows where the windows are.

### 3.2 Geometry

| Property | Value |
|---|---|
| `PanelWindow.edge` | Top by default. Any edge; the rail is always on the screen-edge side. |
| `thickness` (exclusive zone) | **28 px** = 2 px rail + 26 px band. Not 44. |
| `screenInset` | **0**. The rail touches the screen edge. The engines' per-side outer gap (`outerGapTop`) separates windows from the band; recommended bundled default 8, so windows sit 36 px from the edge. The band is visually part of the gutter. |
| `exclusiveZone` | 28, `exclusiveZoneEnabled: true`, never changed at runtime (the `PanelWindow` doc is explicit that widening it shoves tiled windows). |
| `shadowSize` | 0. Popouts are separate surfaces (§4), so the bar reserves nothing. This retires `socketReserve`, `_socketDepth`, the `pocket` item and `BarCanvas.sockets` in `BarHost.qml`. |
| `interactiveThickness` | 0 (follows thickness). |
| `cornerCarveRadius` | **0**. No carve and no radius anywhere on the bar. |
| `panelLayer` | Top. |
| Band material | `phosphor-glass` as the packs define it: real backdrop blur (radius 24), navy tint (`Glass tint`) at 55 %, content opacity 1. The glow-on-bright-backdrop and the sweep are **off** for the bar (`Glow strength 0`, `Sweep speed 0`): the band is a quiet pane so the rail and the map carry the colour. |
| Rail | 2 px, full length, the four-stop gradient with `border-phosphor`'s slow flow (`Flow speed` 0.02/s) and its gleam (`Gleam strength` 0.35, one pass per 9 s). The flow is the only idle motion on the bar. |

### 3.3 The rail as an axis (and as data)

- **Static axis (snapping, tiling)**: the gradient spans the bar's length once, cyan at
  the left screen edge, rose at the right. A window at x = 0.3 of the screen has a blue
  hue in the map, on the rail above it, and (if the `border-phosphor` pack is active) on
  its own frame. Three surfaces, one axis.
- **Bound axis (scrolling)**: the rail shows the *same window into the strip's gradient*
  as the lens. With `stripModelJson.viewOffsetPx` and `stripExtentPx`, the rail's
  visible hue range is `[view/extent, (view+viewport)/extent]`. When the strip fits, the
  full gradient is visible. When 30 columns exist and 3 are visible, the rail shows a
  narrow slice (say purple to purple-rose), and the user knows from the bar alone that
  they are near the end of a long strip. Panning is a **follow**: the rail slides with
  `viewDelta` from the tile batch, no easing of its own.
- **Overflow ends (scrolling)**: the rail's outer 48 px on a side with columns beyond it
  brightens to 100 % and thickens 2 → 3 px, in the hue of the *next* off-screen column.
  Both ends at rest are 70 %.
- **Hover**: the rail segment above a hovered chip brightens to 100 % over 100 ms and
  the chip's label takes the rail's hue at that x for its underline (1 px). Chips never
  paint their own hover background.
- **Urgency**: the rail thickens 2 → 4 px over the chip or map cell that owns the urgent
  thing, in white, entering over 200 ms and pulsing with the cell. Thickness grows into
  the band, never into the screen.
- **Open pane**: the rail segment above the source chip stays at 100 % for the pane's
  life and the tether hangs from it (§4.3).
- **Mode** is not encoded on the rail. Mode is what the map's shape is.

### 3.4 Content band

- Chips are bare content on the glass: glyph + label in `Theme.on_surface`, 13 px
  tabular figures for anything numeric, no chip backgrounds. Groups are separated by
  1 × 12 px vertical hairlines at 25 % white. `Slot.qml`'s
  `Rectangle { radius: height/2; color: surface_variant }` goes away; the `leftGroups` /
  `centerGroups` / `rightGroups` arrays stay and mean "hairline-separated group".
- Layout: left = map, focused window; center = clock; right = the rest. Same arrays as
  today.
- Chip height 20 px, band padding 3 px top and bottom, glyph 16 px.
- Vertical bar: 28 px wide, rail on the outer edge running the full height (cyan at the
  top), chips stacked, labels dropped except the clock (two stacked two-digit numerals).
  Map 24 × 14 px landscape.

### 3.5 Per-mode behaviour

| Mode on this screen | Bar difference |
|---|---|
| Snapping | Static axis. |
| Tiling | Static axis. |
| Scrolling | Bound axis and overflow ends (§3.3). Horizontal wheel anywhere on the band pans the strip, because on a scrolling screen the whole bar edge is the strip's axis. |
| Disabled / `none` | Rail desaturated to 30 %; map dashed. |
| Fullscreen window on this screen | Band unmaps; the rail stays as a 1 px line at 30 % so the edge keeps its axis. Hover at the edge re-shows the band over 150 ms. |

---

## 4. Popout Geometry: Engine-Placed Panes

### 4.1 Decision

A popout is **a real toplevel that the engine places**. The shell opens a surface with
`appId = org.phosphor.shell.pane.<name>` (control-center, notifications, calendar, media,
network, power, map-expanded) and lets the daemon place it the way the screen's mode
places windows. The bundled rule set carries one window rule per pane. The popout is
therefore not a `PopupWindow` anchored to a chip; it is a window with a rule, and the only
thing tying it to the bar is a tether.

No concave corner (DMS). No detached floating card (Noctalia, HyprPanel). No chip-to-drawer
morph (Caelestia). The pane is a tile. Its surface is `phosphor-glass` with the window's
normal corner radius from the decoration pack, so it belongs to the screen, not to the
bar.

### 4.2 Placement per mode

| Mode | Where the pane lands | Mechanism (existing) |
|---|---|---|
| Snapping | The zone nearest the source chip along the top edge, or the zone the rule names. The occupant stays underneath; the pane sits on top of the zone's stack. Close pops it and the occupant is untouched. | Rule `SnapToZone` by zone number, or `Snap.snapToEmptyZone` when the rule prefers empty. |
| Tiling | Inserted as a tile. Control center: as master (`Autotile.promoteToMaster` after open). Small panes: inserted last, with `minWidth/minHeight` from `windowOpened` keeping the slot sane. | Rule-driven insert; `focusNewWindows`. |
| Scrolling | A **new column immediately after the focused column** at a preset width (control center: 1/3 preset; small panes: fixed 360 px). The strip's own insert slide carries it in. Its hue on the rail is its strip position, like any column. | `ScrollOpenParams` via rule; `insertPosition` per-screen key. |
| Floating / disabled / `none` | Floating surface, 0 inset under the source chip, width 380, no shadow; the tether (§4.3) is the entire connection. | `PopupWindow`, `popupEdge = Top`, `gap = 0`. |

A pane can be **pinned** (header button, `Ctrl+P`): it stops being a pane and is an
ordinary window from then on. Un-pinning closes it.

### 4.3 The tether (anchoring to the source control)

The chip does not morph into the pane. The two live on different surfaces, and the
morph is another shell's signature anyway. Instead:

1. Press: the chip's label underline enters (1 px, rail hue at that x, 100 ms), and the
   rail segment above it goes to 100 %.
2. A **tether**, 2 px wide, in the rail's hue at the chip's x, drops from the rail to
   the pane's top edge. The bar surface draws the part inside the band; the pane's
   surface draws the part inside its outer gap. Same colour, same width, so the seam is
   invisible. If the pane's top edge does not span the chip's x (tiling placed it
   elsewhere), the tether runs down from the chip, *along the rail* to the pane's x
   (taking on the rail's hue as it travels), then down. The rail is the wire.
3. The pane's top edge carries a 2 px band of the rail's gradient for the pane's whole
   life, sampled over the pane's own x-range: the `border-phosphor` band, top edge only,
   driven by a decoration param the shell sets on its own surface. So the pane's edge
   matches the rail directly above it, pixel for pixel in hue.
4. Close: the pane releases first, then the tether retracts up into the rail over 250 ms
   accelerated, then the rail segment and the underline release over 500 ms.

### 4.4 Open / close choreography

| t | Open | Close |
|---|---|---|
| 0 ms | Chip underline and rail segment enter (100 ms). Surface requested. | Pane content releases 100 → 0 % over 120 ms accelerated. |
| 0–140 ms | Engine places the surface. Neighbours move under the engine's own animation; the shell does not animate placement. | Surface unmaps at 120 ms; the engine reflows neighbours. |
| 60 ms | Tether drops rail → pane top, 180 ms decelerated. | Tether retracts pane → rail, 250 ms accelerated. |
| 140–380 ms | Pane content **enters**: opacity 0 → 1 and a 4 px downward slide, 240 ms decelerated, header first, body +60 ms. No scale, no overshoot. | Rail segment and underline release over 500 ms. |
| 380 ms | Settled; the pane holds focus. | 750 ms: done; focus goes wherever the engine sends it after any window close. |

The pane is interactive from the moment it maps (~140 ms).

### 4.5 Gesture reveal

- **Pointer drag from the rail**: press within the top 6 px of the band and drag toward
  the screen. The rail under the pointer stretches into a tether that **follows** the
  pointer (displacement = 0.6 × drag distance, capped 64 px). Past **48 px** of raw drag
  the tether goes to full width and the pane under the nearest chip opens on release.
  Below threshold, release retracts the tether over 300 ms decelerated, no overshoot.
- **Touch**: same from the screen edge, threshold 64 px.
- **Which pane**: the chip nearest the press x. Over the map: the expanded map pane.
  Over the clock: calendar. Over an empty band region: the control center.
- **Cancel**: drag back above the rail, or `Esc`.

### 4.6 Interruption

- Close during open: the pane's enter reverses from its current progress; the tether
  retracts from its current length.
- Open A while B is open: B closes with the full choreography and A opens 60 ms later,
  so two tethers never share the rail.
- Mode morph while a pane is open: the pane is a window and the engine hands it off like
  any other. The tether re-routes to the pane's new top edge on the frame after the
  shell's own surface configure lands, and its hue re-samples at the new x.
- Engine refuses placement (no zone, strip disabled): the floating fallback from §4.2 is
  used immediately; no retry.

### 4.7 Scrim, modality, arbitration

- **No scrim, ever.** Panes are windows in a tiling WM; darkening the desktop would
  contradict the placement story. Modal dialogs are not panes.
- **Two classes.** `pane` (control center, notifications, calendar, media, expanded map):
  engine-placed, persists until closed, `Esc`, or another pane opens. `transient` (tray
  menus, the map's right-click menu, power confirmation): `PopupWindow` anchored to the
  chip with `gap = 0`, closes on outside click or focus loss, never engine-placed, has a
  tether but only a 120 ms opacity enter and release.
- **Arbitration**: at most one `pane` and one `transient` per screen. A `transient`
  opened over a `pane` stacks above it. A second `pane` replaces the first. Screens are
  independent. The design README's `PopoutService` owns this table and gains the class
  and the per-screen slot.
- **Focus**: a `pane` takes keyboard focus as a window (it is an xdg toplevel, not a
  layer surface). `Esc` closes. Clicking another window does not close a pane: it is a
  tile, and tiles do not vanish when you look elsewhere.

---

## 5. Widget catalog

All chips: no background, on the glass, 13 px tabular figures where numeric. Where a
chip needs a colour it samples the rail at its own x, so the bar reads as one gradient
with content on it rather than a row of differently coloured icons.

| Widget | Treatment |
|---|---|
| **Placement map** | §1. First on the left. |
| **Focused window** | App glyph + title, max 32 chars, middle-elided, with a 1 px underline in the hue of the focused window's *position* (the same hue as its map cell), so the chip points at the window without an arrow. Title change: 120 ms crossfade. |
| **Clock** | `HH:MM`, 14 px tabular, white 90 %. A digit change enters over 150 ms (new digit slides up 4 px) and the old releases over 300 ms. Hover shows `:SS` and the date, entering from the right over 150 ms. Click: calendar pane. |
| **System metrics** | Three 2 × 16 px vertical bars (cpu, mem, gpu), no labels, each in the rail's hue at its own x. Height is the value. Above 90 % the bar turns white. A 1 px peak-hold mark releases over 4 s. Hover expands to tabular percentages. |
| **Media** | Glyph + title. Playback progress is a 1 px line on the rail above the chip, white at 60 %: the rail is the timeline. Paused freezes it. Click: media pane. |
| **Tray** | Glyphs only, 16 px, 60 % at rest, 100 % on hover. Click: the item's own menu as a `transient`. |
| **Network / Bluetooth / Audio / Battery** | One group of four glyphs. State is brightness: on/connected/charging 90 %, off 35 %. Battery ≤ 15 %: the glyph goes white and pulses under 5 %. Wheel on audio: a rail-line meter that holds 800 ms then releases. Click on any: control center pane scrolled to that tile. |
| **Notifications** | Bell glyph with a tabular superscript count. New notification: rail thickens 2 → 4 px over the chip in white and releases over 3 s; the count enters over 100 ms. Click: notifications pane; its top band is the rail gradient like every pane. |
| **Control center** | A single 8 px dot in the rail's hue at its x, 70 %. White while any tile inside has a live change (VPN connecting, recording). Click or rail-drag: control center pane. |
| **Power** | Glyph at 45 %. Click: `transient` confirmation. Hold 700 ms: lock. |

---

## 6. States

| State | Map cell | Bar chip |
|---|---|---|
| Idle, empty | 1 px hue outline @ 30 %, no fill | text 60 % (tray, power lower per §5) |
| Idle, occupied / has value | hue fill @ 40 %, edge @ 80 % | text 90 % |
| Hover | +10 % lightness, edge 100 % | text 100 %, 1 px underline in rail hue, rail segment 100 % |
| Press | fill @ 55 % for the press, scale 0.94 instantly, 120 ms decelerated release | text 100 %, scale 0.96, rail segment 3 px for the press |
| Active (focused / pane open) | white edge @ 90 %, white core line | text 100 %, underline held, rail segment 100 %, tether attached |
| Urgent | white edge pulsing 0.4–1.0 at 1.2 s | rail 4 px white over the chip, count enters, same pulse |
| Disabled | dashed outline 8 %, no fill, no hit | text 25 %, no hover, no underline |
| Releasing (leaving any of the above) | fill releases over 400 ms, white edge over 400 ms | underline and rail segment release over 500 ms |

Reduced motion: enters keep opacity and drop translation/scale; releases halve; pulses
become a single 300 ms fade to steady; the rail's idle flow and gleam stop; matched-edge
logic unchanged.

---

## 7. Edge cases

| Case | Behaviour |
|---|---|
| **No windows** | Snapping: all zones as hue outlines. Tiling: the screen outline only, with the algorithm's glyph centred at 14 %. Scrolling: the lens over an empty band; the rail shows the full gradient because a strip with no extent binds to nothing. Empty is a state, not an error. |
| **40 windows in a strip** | The lens shows the visible 2–4 columns. Each overflow gutter (~5 px at rest) packs its columns at 1 px in their own hues, so the gutter is a compressed slice of gradient; at ≥ 12 columns a side the gutter becomes a solid gradient bar and a tabular count ("18") is drawn on the rail end above the map. The rail's bound axis shows a narrow hue slice, which is the honest signal. At expanded height the strip is drawn at true proportion with a scrollbar that *is* the lens. `stripModelJson` at 40 columns is ~4 KB; the map diff-patches on `stripChanged`. |
| **1 layout with 12 zones** | ~8 × 6 px cells at rest. Below 5 px on either axis a cell drops its outline and draws fill only (occupied) or a 1 px centre dot (empty). Hit targets are found by nearest-cell testing with an 8 × 8 px floor. Expanded height draws all 12 properly. |
| **Ultrawide (32:9)** | Map width clamps at 56 px and letterboxes with 1 px bands so the aspect stays true. The rail's gradient stretches across the full width, so hue resolution per window is higher, not lower. In scrolling an ultrawide viewport usually fits the whole strip: the lens covers the map and the gutters collapse to 0 (elastic, not reserved). |
| **Vertical bar** | Rail runs the full height on the outer edge, cyan at the top. Map 24 × 14 px landscape; its hue axis stays horizontal (screen x), which now disagrees with the rail's vertical axis. Resolution: on a vertical bar the map samples hue from screen *y* instead, so cell colour and rail colour agree at the window's height. Tethers run horizontally into the pane's side edge and route along the rail to the pane's y. |
| **Laptop 13"** | 28 px is already small; nothing shrinks. Under width pressure chips hide from least to most protected: metrics, media, quad, tray, focused window, map, clock, notifications, control center, power. At 2x the rail stays 2 logical px. |
| **Two bars (top and bottom)** | Only the top bar hosts the map by default. Both rails carry the axis; on scrolling screens both bind to the strip. |
| **Screen removed with a pane open** | The pane is a window; the engine hands it off with everything else. The tether re-routes to the pane's new screen's bar, or, if that bar has no source chip for it, the pane loses its tether and becomes pinned. |
| **Daemon restart** | Map draws dashed and the rail desaturates until `LayoutRegistry.daemonReady`, then a full mode morph runs from the dashed state. `stripContextChanged` is not seeded at bring-up, so the first epoch is recorded, not compared. |
| **Pane rule missing** | The pane opens under the engine's default placement for a new window. Still tethered. |

---

## 8. Build scope summary

Enough for a first cut with what exists today: map painting in QML from `getScreenStates`
+ `getLayout` + `getAllWindowStates` / `windowStateChanged` + `windowsTileRequested` +
`visibleStripJson` / `stripChanged` / `stripContextChanged`; the rail bar from
`PanelWindow` with `thickness 28`, `screenInset 0`, `cornerCarveRadius 0`, `shadowSize 0`;
the band and pane materials from the existing `phosphor-glass` and `border-phosphor`
packs; engine-placed panes purely via window rules on the shell's own `appId`s;
`PopupWindow` for transients. Without `stripModelJson` the scrolling hue axis falls back
to the visible cut (cyan at the lens's left edge), which is wrong but degrades cleanly.

**[NEW]** surfaces, in priority order:

1. `Scrolling.stripModelJson(screenId)` (hue axis, overflow gutters, lens, the
   40-window case).
2. `Tiling.currentTilesJson(screenId)` (replay so the map is never blank).
3. `Scrolling.focusColumnAt`, `moveColumnTo`, `scrollViewByPx` (click, drag, lens).
4. `WindowStateEntry` + `appId`/`title`; `ForeignToplevel.demandsAttention`.
5. `Tiling.managedFocusedWindow` + `focusedWindowChanged` (mode-agnostic focus cell).
6. `WindowDrag.registerDropProxy` (real drag onto the miniature; biggest win, most
   engine-coupled).
7. `WindowTracking.moveWindowToDesktop` (filmstrip drag).

Retired by this spec: `BarHost.qml`'s `socketContent` / `socketOpen` / `socketReserve` /
`_socketDepth` / the `pocket` item and `BarCanvas.sockets`; the chip `Rectangle` in
`Slot.qml`; `barThickness: 44` and `screenInset: Tokens.spacing_xl`.
