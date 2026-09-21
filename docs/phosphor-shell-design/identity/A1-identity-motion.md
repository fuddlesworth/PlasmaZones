<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# A1. Phosphor shell: identity and motion language

Sources read: `libs/phosphor-theme/qml/Phosphor/Theme/Motion.qml`, `Tokens.qml`, `StateLayer.qml`; `libs/phosphor-theme/src/defaultpalette.cpp`; `data/curves/*.json` (six curves, all `typeId: cubic-bezier`); `data/schemas/curve.schema.json`; `libs/phosphor-animation/src/curveregistry.cpp` (factories `cubic-bezier`, named elastic/bounce, and `spring` with `omega`/`zeta`); `PhosphorMotionAnimation.h` (QML `Behavior` driven by a named profile, spring settle computed analytically). Packs: `data/surface/{phosphor-glass,phosphor-motes,border-phosphor,border-audio,border-pulse,border-marching,border-sweep,glow,focus-fade,rain-glass,fireflies}/metadata.json`, `data/surface/phosphor-glass/effect.frag`, `data/overlays/phosphor-flux/{metadata.json,effect.frag}`, `data/overlays/{prismata,spectrum-bloom,spectrum-pulse,liquid-canvas,magnetic-field,pulse-flow}/metadata.json`.

## 0. What the packs already agree on

Reading the phosphor-named packs and the overlays side by side, the same five devices recur. They are the look; the shell has not been using any of them.

1. **One shared gradient frame.** `phosphor-flux/effect.frag` line 85: "Shared screen-diagonal gradient coordinate (0 top-left, 1 bottom-right). All zones sample this same axis, forming one gradient frame together." `phosphor-glass` derives its hue from the same `diag = (fuv.x + fuv.y) * 0.5`. `border-phosphor` carries the four stops around the frame. The gradient is never per-element; it is a single field the screen sits in, and every stroke samples its own location in that field.
2. **Thin strokes, not fills.** Every phosphor pack draws 1 to 3 px gradient lines (borders, `shellStrokes` containment insets, the node graph edges) on a deep navy fill (`backgroundColor` / `colorTint` = `#0B1730`). The fill is dark and quiet; the colour is in the line.
3. **A gleam that travels the perimeter.** `border-phosphor` ("a soft gleam orbiting the band"), `border-sweep`, `phosphor-flux` ("a gleam orbiting each zone"). Brightness is a moving highlight on the stroke, not a static glow.
4. **The ramp as a scalar readout.** `phosphor-motes`: "born cyan at the frame and fading out rose at the end of its drift". `phosphor-flux` embers: "hue climbing the gradient with altitude"; nested shells: "each shell sits a step further along the brand gradient". Position, age, and depth are all rendered as a position on cyan→rose. The packs already treat `t` as a value, not a decoration.
5. **Light dust in the margin.** `phosphor-motes`, `fireflies`, `phosphor-flux` embers, `pulse-flow` sparks: small luminous particles in the clear space around a frame, dimming when focus leaves.

Everything below extends those five devices to chrome. Nothing is invented that the packs do not already do.

## 1. Identity statement

Phosphor is a spectrum laid over a deep navy field. Cyan, blue, purple and rose form one continuous axis, and the whole screen shares it: every stroke, indicator and value samples the ramp at the point that describes it, so colour is always a coordinate (where on the strip, how deep in the tree, how far along, how urgent) and never a fill. Chrome is drawn with the same thin gradient strokes, quiet navy ground and travelling gleam as the window frames, because the same engine paints both and the same engine places both. Motion enters fast and leaves slowly along a shaped tail, so change is quick to see and easy to follow. (109 words)

### Hard rules

| # | Rule | Reason |
|---|------|--------|
| R1 | **The gradient is one screen-wide field. No element owns a private gradient.** Strokes sample `t` from a shared axis (section 4). | This is the single device every phosphor pack already uses (`phosphor-flux` line 85, `phosphor-glass` `diag`). It is what makes a bar, a popout and a window frame read as one object. Per-element gradients are what every "gradient border" Hyprland rice does. |
| R2 | **Colour lives in strokes and indicators, never in fills or text.** Ground is navy, text is `OnSurface`, the ramp appears as 1 to 2 px lines, 2 to 4 px bars, and dots. | Matches the pack aesthetic (rule 2 above) and keeps the shell readable all day. Filled accent slabs are the M3 clone silhouette. |
| R3 | **No drop shadows on chrome. Depth is a stroke and a ground step.** `Tokens.elevation_n` keeps `tint`; `y/blur/opacity` are 0 on shell surfaces. | The packs draw no shadows anywhere; a shadowed popout next to an unshadowed window frame would break the one-material claim (section 2.4). Shadows are also the M3 tell. |
| R4 | **Corners are small and fixed: 6 px chrome, 10 px containers, no pills.** | Every `border-*` pack defaults `cornerRadius: 8`. Chrome within one step of that reads as the same family. A thin gradient stroke stays a crisp line at 6 px and becomes a smear at 24 px. Pills are the shared silhouette of DankMaterialShell, Noctalia and HyprPanel. |
| R5 | **Blur only where a window is behind the surface. Never blur the wallpaper.** | `phosphor-glass` blurs backdrop windows and "sinks them toward navy"; that is the material. Wallpaper blur is glassmorphism and looks the same on every desktop. |
| R6 | **Enter fast, leave slow, with a shaped tail. Never mirror an entrance with its own reverse.** In-envelopes are under 120 ms; out-envelopes are 2 to 4 times longer. | The competitor audit shows all three reference shells use symmetric M3 `standard`/`emphasized` in both directions (`Motion.qml` today does too). An asymmetric envelope is cheap and immediately felt. |
| R7 | **Motion retargets, never restarts.** A new target continues from the current value and velocity. | `Curve.h`: stateful springs "derive continuity from value/velocity". Restart-from-zero on a `Behavior` flickers and is the tell of untuned `NumberAnimation`. |
| R8 | **Numbers are tabular and monospaced, and a changed digit is marked.** | The one typographic signature (section 5). |

## 2. Material language

### 2.1 Layers, bottom to top

1. **Void** (`Background` `#050916`). Bar ground, lock ground.
2. **Ground** (`SurfaceContainer` `#070F22`, alpha 0.88 on chrome over windows, opaque elsewhere). Popouts, OSD, launcher, notifications.
3. **Stroke.** A 1 px inset line at the surface boundary sampling the shared gradient field at the surface's screen position. Resting opacity 0.35, active 1.0. This is the only outline any chrome surface has and it replaces both `Outline` and the drop shadow.
4. **Gleam.** A short bright segment (about 12% of the perimeter, alpha 0.6 peak) that travels the stroke. Speed is a state signal (section 3, `follow`). On a resting surface it is stationary at the anchor corner.
5. **Margin dust** (optional, off by default on bar/popout, on for lock and launcher): the `phosphor-motes` / `fireflies` particle band in the clear margin outside the stroke.
6. **Content.** Icons and text in `OnSurface` / `OnSurfaceVariant`. Indicators (progress bars, pips, sliders) sample the ramp per R2.

### 2.2 Corner language

| Token | Value | Used on |
|-------|-------|---------|
| `radius_edge` | 6 px | Bar capsule, workspace pips, buttons, OSD, tray items, any chrome carrying a stroke |
| `radius_container` | 10 px | Popouts, control-centre tiles, launcher, notifications |
| `radius_tile` | inherits the decoration pack's `cornerRadius` (8 px default) | Windows |
| `radius_full` | circular controls only (toggles, avatar) | |

`radius_l`/`radius_xl`/`radius_xxl` in `Tokens.qml` remain for settings pages and are forbidden on shell chrome. Why: a gradient stroke is the identity carrier (R2), and a 1 px stroke around a 24 px corner loses about a quarter of its visible length to the arc, where the gradient sample changes fastest and the line looks blurry. At 6 to 10 px the stroke reads as a drawn line and matches the frames the packs already draw.

### 2.3 Where blur is and is not

- **Is**: any chrome overlapping a window (popout, OSD, launcher, control centre, lock over a session). The backdrop pass of `phosphor-glass` at `bufferScale: 0.25`.
- **Is not**: the bar (on the wallpaper strip), the lock ground (opaque navy, wallpaper desaturated and darkened through `focus-fade` parameters instead), workspace pips, or anything under 120 px on a side.

### 2.4 One engine, one material: packs on chrome

Every chrome surface is a decoration host, exactly like a window frame, and the engine that positions windows also positions chrome (the bar reserves the strut, the popout anchors to its bar widget, the OSD is placed by the same layer logic that places overlays). A surface declares a slot; the theme maps a pack to it.

| Slot | Default pack | What it does there |
|------|--------------|--------------------|
| Bar capsule | `border-phosphor` (width 1, gleam on, flow speed low) | The bar's stroke is the same flowing band as a focused window's. With audio visualisation on, the user may switch to `border-audio` and the bar pulses with the windows. |
| Popout, control centre, launcher | `phosphor-glass` (blurRadius 24, tintStrength 0.55) plus `border-phosphor` | Blurs the windows behind and sinks them to navy. The pane's own hue follows the shared `diag` axis, so a popout on the top-left is cyan-leaning and one on the bottom-right is rose-leaning, exactly as zones are under `phosphor-flux`. |
| OSD | `border-phosphor` with `glow` (intensity bound to the OSD value) | Volume at 100% is a full halo; at 10% a faint one. |
| Notification | `border-pulse` for `urgency=critical`, `border-phosphor` otherwise | Critical breathes, normal sits. |
| Lock screen | `phosphor-motes` on the clock container; wallpaper through `focus-fade` desaturation | The only theatrical surface. Dust drifts from the clock frame, cyan at birth, rose at the end. |
| Workspace pips, tray items | none (pure QML stroke sampling the field) | Too small for a pack pass. |

The packs already dim "when the surface loses focus", so the shell feeds them a focus signal: popout focused while open, OSD focused while its timer runs, bar never.

### 2.5 Light theme

The light theme is the same spectrum on a bright field, using the palette page's light ramp (sky `#0EA5E9`, blue `#3B82F6`, violet `#7C3AED`, rose `#E11D48`) on `#F6F9FF` / `#EEF3FF` / `#E8EEFF`, text `#0B1730`.

- Stroke resting opacity rises to 0.55 (a coloured hairline on white needs more ink).
- The gleam becomes a *darker* segment of the stroke (multiply, same hue) rather than a brighter one, since a highlight on white is invisible. This is written down so nobody reaches for a shadow instead.
- `phosphor-glass` `colorTint` becomes `#E8EEFF`, `tintStrength` 0.35, so backdrops fog rather than darken.
- Motion is identical in both themes.

## 3. Motion language

Curve homes: existing `data/curves/*.json` where they fit; new files in the same schema (`typeId: cubic-bezier` with `x1..y2`, or `typeId: spring` with `omega`, `zeta`, as `curveregistry.cpp` accepts). In QML every primitive is `Behavior on <prop> { PhosphorMotionAnimation { profile: "phosphor.<name>" } }`. In SVG SMIL it is `<animate calcMode="spline" keySplines="x1 y1 x2 y2">`, or two chained `<animate>` elements where a spring is approximated.

Interruptibility (R7): every primitive retargets. Bezier primitives restart only the *out* phase from the current value; spring primitives retarget natively. Nothing snaps.

### 3.1 Primitives

| # | Primitive | Trigger | Curve, duration | Drives | Example |
|---|-----------|---------|-----------------|--------|---------|
| M1 | **enter** | Anything becomes true: hover, press, focus, value change, surface appears | `osd-pop` (`0.34, 1.20, 0.64, 1.00`), 90 ms, small overshoot | `strokeOpacity` 0.35→1, `scale` 0.98→1.0 (2%, not the M3 10%), `opacity` 0→1 | Hovering a bar widget: stroke goes full within 100 ms. |
| M2 | **hold** | While the condition remains true | No animation | Nothing | A focused popout keeps its stroke at 1.0. |
| M3 | **release** | The condition stops being true | New `phosphor-release.json`: `cubic-bezier 0.05, 0.60, 0.15, 1.00`, 360 ms. Steep first 60 ms, long tail | `strokeOpacity` →0.35, `opacity` →0 on dismissal | Mouse leaves: 90 ms up, 360 ms down. The 1:4 ratio is the feel. |
| M4 | **settle** | Any positional or dimensional change (popout height, slider knob, strip pan, bar reflow) | New `phosphor-settle.json`: `spring` `omega: 22, zeta: 0.85`, one 3% overshoot, settled in about 250 ms | `x`, `y`, `width`, `height`, `value` | A slider knob follows the pointer on a spring, so drags retarget cleanly. |
| M5 | **follow** | Focus or active-workspace moves from A to B | B runs **enter**; A runs **release**; the *gleam* on both strokes runs from A's position toward B's over 220 ms (`widget-out`) | `gleamPhase` on each stroke, `strokeOpacity` | Switching workspace 2→3: pip 3 lights, pip 2 releases, and the gleam on the bar stroke slides right, so the eye reads the direction. |
| M6 | **reveal** | A surface opens | `widget-out` (`0.33, 1.00, 0.68, 1.00`), 220 ms driving a gradient-mask `x` from the anchor edge across the surface, layered with **enter** on the stroke. Fill opacity uses a fast 120 ms `cubic-out` | mask position, `opacity` | The popout is not scaled in. It is drawn in from the widget it belongs to, so its origin is legible. |
| M7 | **dismiss** | A surface closes | `osd-in` (`0.32, 0.00, 0.67, 0.00`), 140 ms on `opacity`, no scale, then **release** on the stroke | `opacity`, `strokeOpacity` | Close is shorter than open. The visible mass leaves fast, the stroke lingers a little. |
| M8 | **breathe** | Only while a *hot* state persists (critical notification, battery critical, recording) | A `SequentialAnimation` of two `phosphor-release` halves. The undamped spring this originally proposed is not expressible as a curve — see below | `strokeOpacity` between 0.55 and 1.0 | The only looping primitive. Everything else is event-driven. |
| M9 | **tick** | A discrete value step (scroll notch on the OSD, clock minute, download %) | `widget-pop` (`0.34, 1.56, 0.64, 1.00`), 60 ms on enter, then **release** | `strokeOpacity`, `borderWidth` +1 px, changed-digit `opacity` | Each volume notch flashes the stroke, so repeated notches read as separate events, not a smooth fade. |
| M10 | **pulse** | Audio bass when a `border-audio`/`spectrum-*` pack is active | Pack-driven; the shell only passes the audio uniform through | Pack uniforms | The bar, OSD and window frames pulse together because they share the pack. |

### 3.2 Envelope rules

- Every visible change is **enter → hold → release**. Composite primitives (follow, reveal, dismiss, tick) are named combinations, not new curves.
- Release opacity never dips below the resting 0.35 on a stroke, so a surface never loses its outline.
- Reduced motion: release duration 180 ms, enter loses its overshoot (`y1` clamped to 1.0), reveal becomes a 120 ms opacity enter, breathe stops. No primitive is removed, because they carry state feedback, not decoration.

### 3.3 Proposed curve files

```json
{ "name": "phosphor-release", "displayName": "Phosphor release (fast head, long tail)", "typeId": "cubic-bezier", "parameters": { "x1": 0.05, "y1": 0.60, "x2": 0.15, "y2": 1.00 } }
{ "name": "phosphor-settle",  "displayName": "Phosphor settle (spring, one overshoot)",  "typeId": "spring",       "parameters": { "omega": 22,  "zeta": 0.85 } }
```

`phosphor-breathe` was checked and dropped. The curve registry models a
*settling* spring: `Spring::settleTime()` divides by `max(1e-3, zeta*omega)`,
so `zeta: 0` yields about 5298 s and clamps to `Spring::MaxSettleSeconds`
(30 s). It passes validation and is useless — a 30-second animation, not a
1.2 s loop. An undamped oscillator never settles, so it cannot be a curve at
all. Breathe uses the M8 fallback: two `phosphor-release` halves in a
`SequentialAnimation`.

### 3.4 Motion.qml consequences

Retire `easing_standard` and `easing_emphasized` from shell use (settings pages keep them). Add `Motion.enter`, `Motion.release`, `Motion.settle` as pre-built easing objects, and `duration_enter = 90`, `duration_release = 360`, `duration_reveal = 220`, `duration_dismiss = 140`. `StateLayer.qml` (hover/press) becomes the enter/release pair on `strokeOpacity` instead of an opacity crossfade of a filled overlay.

## 4. Semantic ramp

The four `BrandStop*` tokens form an axis `t ∈ [0, 1]`, cyan→blue→purple→rose. `t` is always a coordinate. Three axes are defined and each element uses exactly one:

### 4.1 Position axis (the default, from the packs)

The shared screen-diagonal field, as `phosphor-flux` and `phosphor-glass` already compute it: `t = (x + y) / (screenW + screenH)`. A stroke at the top-left is cyan, at the bottom-right rose. The bar, every popout, every window frame under `border-phosphor` and every zone under `phosphor-flux` sample this. Consequence: the screen is a single gradient frame and chrome is provably part of it.

### 4.2 Structure axis (the window manager's own)

Where the engine has a structure, `t` encodes position *in that structure*, which nobody else can do because nobody else has the structure:

- **Scrolling mode**: `t` = column index along the strip, cyan at the left end, rose at the right, so the workspace pip strip and the tab indicators show where on the strip the focused column is. Scrolling the strip slides the hue.
- **Tiling mode**: `t` = depth in the tile tree (root split cyan, leaves toward purple). Nested containers get the `shellStrokes` inset treatment from `phosphor-flux` ("each shell sits a step further along the brand gradient"), so a deeply nested tile visibly is.
- **Snapping mode**: `t` = zone index in reading order, matching the zone overlay's own frame under `phosphor-flux`.

The bar's workspace widget uses the structure axis; the rest of the bar uses the position axis.

### 4.3 State axis (indicators only)

For indicators that carry a level or urgency, `t` is that level. Continuous values map directly (volume 100% is rose, 20% is cyan; CPU and temperature the same). Discrete states use the four stops only:

| t | Stop | Meaning | Where |
|---|------|---------|-------|
| 0.00 | cyan `#22D3EE` | Information, resting | Info OSD, unfocused frames, inactive pips, `Info` role |
| 0.33 | blue `#3B82F6` | Active, yours | Focused frame, active pip, slider fill, `Primary` role |
| 0.66 | purple `#A855F7` | Transient, happening | Incoming notification, download, pending network, drop target, `Secondary` role |
| 1.00 | rose `#F43F5E` | Hot, needs you | Critical notification, battery under 10%, recording, destructive confirm, `Error` role |

This is the mapping `defaultpalette.cpp` already encodes (`Tertiary`/`Info` cyan, `Primary` blue, `Secondary` purple, `Error` rose); it is made legible rather than invented. `Success` green and `Warning` amber stay outside the ramp on purpose: the ramp is a coordinate, not an outcome.

Enter/release change opacity, never `t`. Colour says *what*; opacity says *when*.

### 4.4 Brand-fixed versus wallpaper-derived (matugen)

| Fixed to brand | Wallpaper-derived |
|----------------|-------------------|
| `BrandStop0..3`, `Error*`, `Info*`, `Success*`, `Warning*` | `Background`, `Surface`, `SurfaceContainer*`, `SurfaceVariant`, `OnSurface*` |
| All strokes, gleams, indicator fills (they sample the ramp) | `Primary`, `PrimaryContainer`, `Secondary`, `SecondaryContainer`, `Tertiary*` for settings pages, links, and QQC2 controls |

A wallpaper retints the field the spectrum sits on, never the spectrum. That is the inverse of DankMaterialShell and Noctalia, where the accent is wallpaper-derived. If matugen produces a dark-mode ground above 0.25 luminance the theme clamps it back into the navy family; the ground is never grey.

## 5. Typography

Inter is the field default (DankMaterialShell, Noctalia, HyprPanel all ship it or the system font) and it reads as "any 2025 shell". The decision is a geometric display face with spectrum-friendly wide counters, and a mono that shares its skeleton.

**Display and UI: Manrope.** Geometric, open apertures, slightly wide, with a light weight that sits well over a dark navy field and a semibold that is not heavy. Google-Fonts-available. **Values: JetBrains Mono** (already the common developer default on the target audience's systems, and its round dots and open zero pair with Manrope's geometry). Fallback stacks: `Manrope, "Noto Sans", sans-serif` and `"JetBrains Mono", "Noto Sans Mono", monospace`. `Tokens.font_family` stops deferring to `Qt.application.font.family` for shell chrome; settings pages keep the system font.

| Role | Face | Size / weight | Where |
|------|------|---------------|-------|
| Bar label | Manrope | 13 / Medium, letter-spacing 0.2 px | Widget captions, window title |
| Bar value | JetBrains Mono | 13 / Medium, `font.features: { "tnum": 1 }` | Clock, CPU %, battery %, volume |
| Popout title | Manrope | 16 / SemiBold | Popout and tile headers |
| Popout body | Manrope | 13 / Regular | Lists, descriptions |
| OSD value | JetBrains Mono | 24 / Medium | The figure beside the OSD bar |
| Launcher input | Manrope | 18 / Regular | Search field |
| Lock clock | Manrope | 96 / ExtraLight, tabular, letter-spacing -2 px | Time |
| Lock date | Manrope | 16 / Regular, uppercase, letter-spacing 1.5 px | Date line |

**The signature**: every number in the shell is monospaced and tabular, and a changed digit gets a 2 px gradient underline that enters and releases (M9). The clock's minute digit underlines once a minute; the volume figure underlines on each notch. Widths never change because figures are tabular, so the underline lands under the same glyph every time. It is cheap (a `Rectangle` under the `Text` with `opacity` bound to the tick) and the underline samples the same ramp axis as the surface's stroke, so it is brand, not decoration.

## 6. The across-the-room test

You cannot read the text. What you see is a deep navy screen, not black and not grey, crossed by thin lines that shade from cyan at the top-left to rose at the bottom-right, one line under the bar and one around each window, all of them clearly parts of the same gradient. One frame is brighter than the rest and a small highlight is drifting around it. Somebody presses a key: a panel draws itself out from the bar, quick, with no zoom and no shadow, and when it goes the outline fades a beat after the body. The numbers on the bar are all the same width and a little coloured dash just flashed under one of them. If you see a pill, a soft grey shadow, a filled coloured card, or an accent that came from the wallpaper, it is not Phosphor.

## 7. Rejected

1. **Glassmorphism (frosted panels over blurred wallpaper, white hairline).** end-4, Caelestia and every macOS-alike own it. Retained only as `phosphor-glass`'s backdrop pass, which blurs windows and tints to navy; the wallpaper is never blurred (R5).
2. **CRT metaphor (afterglow, scanlines, persistence, excitation).** Considered as the naming layer for the motion system and dropped: the packs never use it as a visual device, it explains nothing the spectrum does not already explain, and it drags the design toward retro pastiche that Cool Retro Term already owns. Asymmetric envelopes are kept on their own merits (R6, the audit); the lore is not.
3. **Neon / synthwave (magenta-cyan on black, glow on everything).** Hyprland rice culture owns it and it is exhausting at 8 hours a day. Colour is confined to strokes (R2) and rose is confined to hot states (4.3), so the screen stays mostly cool.
4. **Wallpaper-derived accent (matugen all the way, as DankMaterialShell and Noctalia).** The identity would change with every wallpaper and be unrecognisable in a screenshot. The spectrum is the brand; the wallpaper may tint only the field (4.4).
5. **Big pills and 24 px cards with drop shadows (the current parity build).** The M3 default and the exact silhouette of HyprPanel and Noctalia. Rejected on R3 and R4; a thin stroke around a 6 px corner is the visible difference at thumbnail size.
6. **Per-element gradients (every button its own cyan-to-rose sweep).** The common "gradient border" look. It fails because it makes the ramp decoration; sampling one shared field (R1) makes it a coordinate and makes chrome and windows demonstrably one object.
7. **Spring-everything (Apple-style physics on opacity as well as position).** Springs on opacity overshoot past 1.0 and clip, and symmetric spring-in/spring-out loses the 1:4 enter/release ratio. Springs are reserved for settle (M4) and breathe (M8).
