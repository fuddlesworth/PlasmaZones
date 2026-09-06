<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 05: Phosphor Shell Visual Identity (synthesis)

This is the one document a mock author or a QML author needs. It resolves the four
working specs in [`identity/`](identity/) into a single set of decisions:

- [`A1-identity-motion.md`](identity/A1-identity-motion.md), material, motion primitives, spectrum axes, typography
- [`A2-bar-geometry.md`](identity/A2-bar-geometry.md), the placement map, the spectrum rail, engine-placed panes
- [`A3-surfaces.md`](identity/A3-surfaces.md), every non-bar surface
- [`A4-taken-audit.md`](identity/A4-taken-audit.md), what the field already owns (cited)

Where those four disagree, this file wins. The mockups in [`mockups-v2/`](mockups-v2/)
are drawn from this file.

## 1. Why the previous design failed

The first design (`01`–`04`, `mockups/`) was a parity checklist against DankMaterialShell,
Noctalia and HyprPanel. It produced a competent clone: navy slab, 16–24 px pills, tile-grid
control center, Spotlight launcher, drop shadows, symmetric M3 fades. Its one named
differentiator, the connected-corner popout, is a DMS port. The live build had drifted
further toward the crowd (floating capsule with chip islands), the whole shell used a single
GPU effect (a drop shadow), and none of the project's own assets appeared in it.

The audit (A4) confirms two things: every silhouette in the old mockups is owned by someone,
and **no shell in the field draws the live window layout in the bar**, because no shell owns
the placement engine. That is the seam.

## 2. Identity

Phosphor is a spectrum laid over a deep navy field. Cyan, blue, purple and rose form one
continuous axis and the whole screen shares it. Colour is always a coordinate (where on the
screen, where on the strip, how far along, how urgent) and never a fill. Chrome is drawn
with the same thin gradient strokes, quiet navy ground and travelling gleam as the window
frames, because the same engine paints both and the same engine places both. Motion enters
fast and leaves slowly along a shaped tail.

### The three unclonable claims

1. **The bar is a live map of the placement engine.** Zones, tile rects or the scrolling
   strip, drawn from the engine's own model, with the viewport lens and off-screen columns
   visible. (A2 §1)
2. **Popouts are windows the engine places.** The control center is a zone, a tile or a
   strip column, with the user's gaps and surface pack, tied back to its bar chip by a
   2 px tether in the rail's hue. (A2 §4, A3 §2)
3. **Feedback lives on the window it concerns.** OSDs, toasts and polkit prompts are
   bands on the edge of the relevant window, not a centred pill or a top-right stack.
   (A3 §3, §4, §9)

### The five devices (from the packs, A1 §0)

Read from `phosphor-flux`, `phosphor-glass`, `border-phosphor`, `phosphor-motes`, `prismata`,
`spectrum-bloom`. These are the look and every surface uses them:

1. one shared gradient field, never a per-element gradient;
2. thin strokes on quiet navy, never filled accent slabs;
3. a gleam that travels the stroke;
4. the ramp as a scalar readout (position, depth, level);
5. light dust in the margin, opt-in, only on lock and dashboard.

## 3. Hard rules

| # | Rule |
|---|------|
| R1 | Colour lives in strokes, bands, underlines and indicators. Never in fills, never in text, never in a button background. |
| R2 | No drop shadows on any shell surface. Depth is a stroke and a ground step. |
| R3 | Radii: **6 px** chrome (OSD cards, fields, chips that need an outline), **8 px** engine-placed tiles (the surface pack's `cornerRadius`), **10 px** floating containers (launcher, toast, polkit). Nothing larger. No pills. Miniature rects 3 px. |
| R4 | Blur only where a window is behind the surface. The wallpaper is never blurred. |
| R5 | Enter fast, leave slow, shaped tail. Never mirror an entrance with its own reverse. Never scale a surface in. |
| R6 | Motion retargets from the current value; nothing restarts from zero. |
| R7 | Every number is tabular monospaced, and a changed digit gets a 2 px spectrum underline that enters and releases. |
| R8 | The spectrum is brand-fixed. Matugen may retint ground, containers and text, never the four stops or Error/Info/Success/Warning. |
| R9 | Focus and urgency are **white**, never a hue, so they cannot be confused with position. |

## 4. Colour

Dark: void `#050916`, abyss `#070F22`, navy `#0B1730`, outline `#1E293B`, text `#E6EDFF` /
`#94A3B8`. Spectrum: cyan `#22D3EE` → blue `#3B82F6` → purple `#A855F7` → rose `#F43F5E`.
Light: sky `#0EA5E9` / blue / violet `#7C3AED` / rose `#E11D48` on `#F6F9FF` / `#EEF3FF` /
`#E8EEFF`, text `#0B1730`; strokes at 0.55 resting opacity, gleam becomes a darker segment.

### 4.1 The axes (resolves A1 §4 vs A2 §1.2)

`t ∈ [0,1]` along the spectrum. Three axes; each element uses exactly one.

| Axis | `t` is | Used by |
|------|--------|---------|
| **Rail axis** (position) | `x / screenWidth` of the thing described, along the bar's edge. Vertical bars use `y / screenHeight`. | The rail, every placement-map cell, chip underlines, tethers, pane top bands, the focused-window underline. Full cyan→rose across the screen so hue resolution is maximal. |
| **Structure axis** | Scrolling: `stripPosPx / stripExtentPx` (first column cyan, last rose, regardless of what is visible; the rail re-binds to this on scrolling screens and shows the same slice as the lens). Tiling: depth in the tree, root cyan. Snapping: zone reading order. | Map cells and tab ticks in scrolling; nested-shell insets in tiling; zone overlays. |
| **Pack field** | The screen-diagonal `(x+y)/(W+H)` the packs already compute. | `phosphor-glass` tint response and `phosphor-flux` frames only. Chrome never samples this directly. |
| **State axis** | A level or urgency: continuous values map directly (volume 100% rose, 20% cyan); discrete states use the four stops: cyan resting/info, blue active/selected, purple pending/transient, rose hot/destructive/at-limit. | Indicators, OSD bands, toast bands, power-menu words, selection lines. |

Enter and release change opacity, never `t`. Colour says *what*; opacity says *when*.
Success green `#10B981` and warning amber `#FBBF24` stay off the ramp.

## 5. Material

Layers bottom to top: void/ground → 1 px inset stroke sampling its axis (0.35 resting,
1.0 active) → gleam (≈12% of the perimeter, alpha 0.6 peak, stationary at the anchor corner
when resting, one pass per 9 s idle on the rail) → optional margin dust → content.

| Surface | Ground | Pack (default) |
|---------|--------|----------------|
| Bar band | `phosphor-glass`, navy tint 0.55, blur 24, glow and sweep off | `border-phosphor` on the rail only (flow 0.02/s, gleam 0.35) |
| Engine-placed panes (control center, notification center, expanded map, calendar, media) | navy 0.96 over blur | the user's window border pack; `border-phosphor` if none |
| Floating cards (launcher, toast, polkit, transients) | abyss 0.92–0.96 over blur | `phosphor-glass` |
| Full-screen (dashboard, power, cheatsheet) | void 0.60–0.90; `focus-fade` dims the desktop | `phosphor-motes` on dashboard ground |
| Lock | void 1.0, wallpaper desaturated through `focus-fade` | `phosphor-motes` in empty regions; `spectrum-bloom` on the outlines |
| OSD bands | none (compositor-drawn on the window edge) | `border-audio` when the visualiser is on; `glow` bound to value |
| Critical notification band | | `border-pulse` |

`shadow` is off on every shell surface. Full hooks table: A3 "Surface-pack hooks".

## 6. Motion (resolves A1 §3, A2 motion vocabulary, A3 consistency table)

| Primitive | Duration | Curve | Notes |
|-----------|----------|-------|-------|
| **enter** | 90 ms (stroke/opacity), 140 ms (content) | `osd-pop` 0.34,1.20,0.64,1.00 for strokes (2% overshoot at most); `cubic-out` for content | Never scale beyond 0.98→1.0. Surfaces draw in from their source point (a gradient mask), they do not zoom. |
| **hold** | | | Resting. Only `breathe` loops. |
| **release** | 360 ms (small things), 720 ms (geometry, old map cells) | `phosphor-release` 0.05,0.60,0.15,1.00 (new curve) | Steep first 60 ms, long tail. Stroke never below 0.35. |
| **settle** | ≈250 ms | `phosphor-settle` spring ω 22 ζ 0.85 (new) | Positional and dimensional changes: knob, pane height, lens. One 3% overshoot. |
| **follow** | 0 ms | none | Bound 1:1 to a pointer, a value, or engine geometry (strip pan, `viewDelta`). |
| **reveal** | 220 ms | `widget-out` 0.33,1.00,0.68,1.00 | Surface open: mask sweeps from the source point; stroke enters. |
| **dismiss** | 140 ms | `osd-in` 0.32,0,0.67,0 | Surface close: opacity only, then the stroke releases. Close is shorter than open. |
| **tick** | 60 ms + release | `widget-pop` 0.34,1.56,0.64,1.00 | Discrete steps: volume notch, clock minute. Underline enters under the changed digit. |
| **retract** | 600 ms short, 1.4–2.5 s long | `phosphor-release` | Edge bands (toast, OSD, polkit) shrink back toward their source point. |
| **breathe** | 1.2 s period | `phosphor-breathe` spring ω 4.2 ζ 0 (fallback: two `phosphor-release` halves) | Only while a hot state persists. |
| **pulse** | 90 ms | settle | Live update on an already-open surface; rate-limited to one per 120 ms. |
| **dim** | 600 ms out, 350 ms back | `phosphor-release` | Compositor dims the desktop to 35% brightness / 40% saturation (power menu); polkit 20%. |
| **placement** | the mode's own | | Engine-placed panes are moved by the engine's window animation, never by the shell. |

Envelope ratio is the feel: enter : release ≈ 1 : 4. Stagger 6–30 ms per item spreading
outward from the focus point, never top-to-bottom for its own sake. Reduced motion: release
180 ms, enter loses overshoot, reveal becomes a 120 ms opacity enter, breathe stops.

Curve files to add under `data/curves/`: `phosphor-release.json`, `phosphor-settle.json`,
`phosphor-breathe.json` (A1 §3.3). `Motion.qml` gains `enter` / `release` / `settle` and the
four durations; `standard` / `emphasized` are retired from shell chrome.

## 7. Typography

Manrope (display and UI) with JetBrains Mono for every value. Fallbacks `"Noto Sans"` /
`"Noto Sans Mono"`. Inter is explicitly rejected as the field default.

| Role | Face | Size / weight |
|------|------|---------------|
| Bar label | Manrope | 13 Medium, +0.2 px tracking |
| Bar value, readouts | JetBrains Mono | 13 Medium, `tnum` |
| Popout title | Manrope | 16 SemiBold |
| Body | Manrope | 13 Regular |
| Secondary | Manrope | 13 Regular `#94A3B8` |
| Label (eyebrow) | Manrope | 12 uppercase, +0.08 em |
| OSD value | JetBrains Mono | 22–24 Medium |
| Power-menu words | Manrope | 24 Regular |
| Launcher input | Manrope | 18 Regular |
| Lock clock | Manrope | 96 ExtraLight, tabular, -2 px tracking |
| Lock date | Manrope | 16 Regular, uppercase, +1.5 px |

Signature: tabular figures everywhere, and a 2 px spectrum underline that ticks under a
changed digit (R7).

## 8. The surfaces, one line each

| Surface | The idea (full spec in A2/A3) |
|---------|-------------------------------|
| Bar | A 2 px spectrum rail on the screen edge + a 26 px navy-glass band, 0 inset, no radius. Left: the placement map (18 px aspect-true miniature) with desktop ticks under it, then the focused window. Centre: clock. Right: metrics as 2 px bars, media, tray, net/bt/audio/battery glyphs, notifications, control-center dot, power. Chips have no backgrounds; hairlines separate groups. |
| Mode morph | New geometry enters in 180 ms staggered; old releases over 720 ms; edges shared by both never blink. |
| Control center | An engine-placed tile (nearest zone / new leaf / 420 px column after the focused one). Content: a vertical list of 52 px "rails", each with a 2 px spectrum underline that IS the slider or the on/off light. No tile grid, no header. Tether to its chip. |
| Launcher | A viewfinder: query column on the left, live placement-map miniature on the right two-thirds. Windows results drawn in their real rects; Apps show where the launch will land; on launch the chosen rect grows into the real window. Providers are a tracked label + prefix characters, no pill row. |
| Toasts | A 2 px band (purple; rose if critical) on the top edge of the app's window with the card hanging 8 px inside it; screen-edge anchor only when the app has no window. Band retracts. Drag a toast into a rect to open the app there. |
| Notification center | A strip column / left leaf / left zone the engine places, holding history rows you can drag out into the map. |
| OSDs | Value bands on the edge that concerns them: volume/mic on the focused window's bottom edge, brightness on the screen's right edge, caps on the focused window's top edge, idle/power on the bar clock, media as a playback band. Readout rides the fill point. Bands coexist and travel when focus moves. |
| Power menu | The compositor dims the desktop; a column of words on the edge it was summoned from, coloured by destructiveness (lock cyan → shut down rose). Underlined first letters. Commit is a spectrum wipe. |
| Lockscreen | The layout you left, as static 1 px spectrum outlines with 8% fill and the app glyph centred, on void. Clock and auth in the largest empty region. Unlock fills the outlines with the real windows. |
| Dashboard | Every desktop's live placement map at once, a grid, the current one outlined blue; calendar, weather and media are three same-grammar cells in the last row. The shell's single scale transition. |
| Picker | A bottom-edge strip of candidates; hovering one retints the live desktop and every surface pack for as long as it is hovered (`prismata` carries the transition). |
| Polkit | A 360 px card hanging from the requesting window's top edge with a rose band on that edge. |
| Cheatsheet | Chord labels placed on the actual rects and gaps they act on; non-spatial chords in a right-edge column; a "press it and watch" live mode. |

## 9. The across-the-room test

A deep navy screen, not black and not grey. A thin line along the top edge that shades
cyan to rose. Below it, a miniature of the windows on that screen, each cell the colour of
the rail above the real window. Every window frame carries a thin gradient line, and a
small highlight drifts along one of them. Somebody presses a key: a panel is drawn in from
the bar as a real tile, the other windows make room for it, and a 2 px thread connects it to
the bar. When it closes the body goes first and the thread retracts into the rail. A volume
key is pressed: a band appears along the bottom edge of the focused window, not in the
middle of the screen. All the numbers are the same width and a dash just flashed under one.
If you see a pill, a soft shadow, a filled coloured card, a centred slider capsule, or an
accent that came from the wallpaper, it is not Phosphor.

## 10. Build notes

- First cut is buildable on existing D-Bus (A2 §8). Seven `[NEW]` daemon surfaces are listed
  there in priority order; `Scrolling.stripModelJson` and `Tiling.currentTilesJson` first.
- Retired: `BarHost.qml` socket/pocket machinery and `BarCanvas.sockets`, `Slot.qml` chip
  rectangles, `barThickness 44`, `screenInset spacing_xl`, `ElevationShadow` on chrome, the
  tile-grid `ControlCenter.qml`, the centred `OsdHost`, the top-right toast stack.
- Surface pack metadata needs `osd` / `popup` / `shell` surface-type declarations extended
  for `phosphor-glass`, `border-phosphor`, `border-pulse`, `glow`, `focus-fade`,
  `phosphor-motes`, `spectrum-bloom`, `phosphor-flux`, `prismata`.
- `PopoutService` gains two classes (`pane`, `transient`), one of each per screen, no scrim.
