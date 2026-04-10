# Animation Settings Pages — UI Previews

Visual mockups of each new settings page. All pages use existing PlasmaZones
settings components (SettingsCard, SettingsRow, SettingsSlider, etc.).

---

## Sidebar Navigation

```
┌─────────────────────────┐
│  [Search settings...]   │
├─────────────────────────┤
│  Overview               │
│  ─────────────────────  │
│  Layouts                │
│  Snapping            >  │
│  Tiling              >  │
│  ─────────────────────  │
│ *Animations          >* │  <-- NEW top-level drilldown
│  ─────────────────────  │
│  Exclusions             │
│  Editor                 │
│  General                │
│  About                  │
└─────────────────────────┘

After clicking "Animations":

┌─────────────────────────┐
│  < Animations           │
├─────────────────────────┤
│  General                │
│  ─────────────────────  │
│  Zone Snap              │
│  Fades                  │
│  Layout Switch          │
│  Border                 │
│  Preview                │
│  ─────────────────────  │
│  Shaders                │
└─────────────────────────┘
```

---

## 1. Animations > General

This page absorbs everything currently in GeneralPage's Animations card,
plus new global controls.

```
╔══════════════════════════════════════════════════════════════════════╗
║  Animations                                               [ON/OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ┌────────────────────────────────────────────────────────────┐     ║
║  │                   [EasingPreview widget]                   │     ║
║  │                                                            │     ║
║  │         ·───────────·                                      │     ║
║  │        /             \                                     │     ║
║  │       ·               ·────────────────────────────·       │     ║
║  │      /                                              \      │     ║
║  │  ···                                                  ···  │     ║
║  │                                                            │     ║
║  │  [ ○ ····· drag handles ····· ○ ]                         │     ║
║  │                                                            │     ║
║  │  [  ■ ─────────────────────────────────────── ■  ]  box   │     ║
║  └────────────────────────────────────────────────────────────┘     ║
║  ─────────────────────────────────────────────────────────────────  ║
║                                                                     ║
║  Default timing mode                                                ║
║  Timing used when events don't override        [ Easing      ▾ ]   ║
║  ─────────────────────────────────────────────────────────────────  ║
║                                                                     ║
║  Style                                                              ║
║  Animation curve style                          [ Standard   ▾ ]   ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Direction                                                          ║
║  Ease In / Ease Out / In-Out                    [ Ease Out   ▾ ]   ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Duration                                                           ║
║  Total animation time in milliseconds     ──●────────── 300 ms     ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Multiple windows                                                   ║
║  Animate several windows at once                [ All at once ▾ ]  ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Minimum distance                                                   ║
║  Skip animation below this threshold            [  0  px  ]        ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

When "Default timing mode" is changed to **Spring**:

```
╔══════════════════════════════════════════════════════════════════════╗
║  Animations                                               [ON/OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ┌────────────────────────────────────────────────────────────┐     ║
║  │                  [SpringPreview widget]                    │     ║
║  │                                                            │     ║
║  │        ╭─╮                                                 │     ║
║  │        │●│──/\/\/\/──┤   ←  ball on spring                │     ║
║  │        ╰─╯           │      oscillating to target          │     ║
║  │                      │                                     │     ║
║  │  ─────────────────────────────── time →                   │     ║
║  │  amplitude graph showing damped oscillation                │     ║
║  └────────────────────────────────────────────────────────────┘     ║
║  ─────────────────────────────────────────────────────────────────  ║
║                                                                     ║
║  Default timing mode                                                ║
║  Timing used when events don't override         [ Spring     ▾ ]   ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Preset                                                             ║
║  Quick-select spring behavior                   [ Snappy     ▾ ]   ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Damping ratio                                                      ║
║  < 1 bouncy, = 1 critical, > 1 overdamped  ────●──────── 0.80     ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Stiffness                                                          ║
║  Higher = faster spring                     ──────●──────  800     ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Epsilon                                                            ║
║  Convergence threshold (lower = longer)     ─●───────── 0.001     ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Multiple windows                                                   ║
║  Animate several windows at once                [ All at once ▾ ]  ║
║  ─────────────────────────────────────────────────────────────────  ║
║  Minimum distance                                                   ║
║  Skip animation below this threshold            [  0  px  ]        ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 2. Animations > Zone Snap

Per-event overrides for zone snapping. Each event is a compact card.
Unchecked toggle = "inherit from parent". Checked = compact controls
with mini curve thumbnail and "Customize Curve..." popup button.

```
╔══════════════════════════════════════════════════════════════════════╗
║  Zone Snap In                                      [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ⓘ  Inheriting from: Zone Snap → Global                           ║
║      Current: Easing, 300ms, Standard (Ease Out), Morph            ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  Zone Snap Out                                      [Override: ON] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ┌──────────┐  Timing mode          [ Easing     ▾ ]              ║
║  │  ╭────╮  │  ──────────────────────────────────────              ║
║  │  │·──·│  │  Animation style      [ Pop In     ▾ ]              ║
║  │  │/  \│  │  ──────────────────────────────────────              ║
║  │  │·  ·│  │  Scale start      ────────●───── 87%                ║
║  │  ╰────╯  │  ──────────────────────────────────────              ║
║  │ mini crv │  Duration         ──●────────── 200 ms              ║
║  └──────────┘  ──────────────────────────────────────              ║
║                Curve preset         [ Snappy     ▾ ]               ║
║                                                                     ║
║                [ Customize Curve... ]                               ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  Zone Snap Resize                                  [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ⓘ  Inheriting from: Zone Snap → Global                           ║
║      Current: Easing, 300ms, Standard (Ease Out), Morph            ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

**"Customize Curve..." opens a popup dialog (CurveEditorDialog):**

```
┌─────────────────────────────────────────────────────────────────┐
│  Customize Curve — Zone Snap Out                          [ X ] │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                [Full EasingPreview widget]                │   │
│  │                                                          │   │
│  │         ·───────────·                                    │   │
│  │        /             \                                   │   │
│  │       ·               ·──────────────────────────·       │   │
│  │      /                                            \      │   │
│  │  ···                                                ···  │   │
│  │                                                          │   │
│  │  [ ○ ····· drag handles ····· ○ ]                       │   │
│  │                                                          │   │
│  │  [  ■ ─────────────────────────────────── ■  ]  box     │   │
│  └──────────────────────────────────────────────────────────┘   │
│  ──────────────────────────────────────────────────────────────  │
│  Style                              [ Snappy (Quart) ▾ ]       │
│  ──────────────────────────────────────────────────────────────  │
│  Direction                          [ Ease Out        ▾ ]       │
│  ──────────────────────────────────────────────────────────────  │
│  Amplitude (if elastic/bounce)      ───●────────── 1.0          │
│  Period (if elastic)                ──●─────────── 0.30         │
│  Bounces (if bounce)                ─●──────────── 3            │
│                                                                  │
│                               [ Apply ]  [ Cancel ]              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Same dialog in Spring mode:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Customize Curve — Zone Snap In                           [ X ] │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                [Full SpringPreview widget]                │   │
│  │        ╭─╮                                                │   │
│  │        │●│──/\/\/\/──┤   ball on spring                  │   │
│  │        ╰─╯           │                                    │   │
│  │  ──────────────────────── time →                         │   │
│  │  amplitude graph showing damped oscillation               │   │
│  └──────────────────────────────────────────────────────────┘   │
│  ──────────────────────────────────────────────────────────────  │
│  Preset                             [ Bouncy         ▾ ]       │
│  ──────────────────────────────────────────────────────────────  │
│  Damping ratio                      ──●──────────── 0.50       │
│  ──────────────────────────────────────────────────────────────  │
│  Stiffness                          ────●────────── 600        │
│  ──────────────────────────────────────────────────────────────  │
│  Epsilon                            ─●───────────── 0.001      │
│                                                                  │
│                               [ Apply ]  [ Cancel ]              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Animations > Fades

Same per-event card pattern, but for fade animations.

```
╔══════════════════════════════════════════════════════════════════════╗
║  Zone Highlight Fade                               [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Fade → Global                                ║
║      Current: Easing, 200ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  OSD Fade                                          [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Fade → Global                                ║
║      Current: Easing, 200ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  Dim Fade                                          [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Fade → Global                                ║
║      Current: Easing, 200ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  Layout Switch Fade                                [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Fade → Global                                ║
║      Current: Easing, 200ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 4. Animations > Layout Switch

```
╔══════════════════════════════════════════════════════════════════════╗
║  Layout Switch In                                  [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Layout Switch → Global                       ║
║      Current: Easing, 300ms, Standard (Ease Out), Morph            ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║  Layout Switch Out                                 [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Layout Switch → Global                       ║
║      Current: Easing, 300ms, Standard (Ease Out), Morph            ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 5. Animations > Border

Single card (no sub-events).

```
╔══════════════════════════════════════════════════════════════════════╗
║  Border Animation                                  [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Global                                       ║
║      Current: Easing, 300ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝
```

When overridden:

```
╔══════════════════════════════════════════════════════════════════════╗
║  Border Animation                                   [Override: ON] ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ┌──────────┐  Timing mode          [ Easing     ▾ ]              ║
║  │  ╭────╮  │  ──────────────────────────────────────              ║
║  │  │····│  │  Duration         ────●──────── 150 ms              ║
║  │  │/  \│  │  ──────────────────────────────────────              ║
║  │  │·  ·│  │  Curve preset         [ Gentle     ▾ ]              ║
║  │  ╰────╯  │                                                      ║
║  │ mini crv │  [ Customize Curve... ]                              ║
║  └──────────┘                                                      ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 6. Animations > Preview

Single card for snap-assist preview animations.

```
╔══════════════════════════════════════════════════════════════════════╗
║  Preview Animation                                 [Override: OFF] ║
╠══════════════════════════════════════════════════════════════════════╣
║  ⓘ  Inheriting from: Global                                       ║
║      Current: Easing, 300ms, Standard (Ease Out)                   ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 7. Animations > Shaders (Phase 2 Stub)

Placeholder page that will be populated when the shader editor lands.

```
╔══════════════════════════════════════════════════════════════════════╗
║  Animation Shaders                                                  ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                     ║
║  ⓘ  Custom GLSL shaders for animation events will be available     ║
║     in a future update. This will allow writing fragment shaders    ║
║     that control how windows visually transition during snap,       ║
║     layout switch, and other animation events.                      ║
║                                                                     ║
║     Planned shader styles:                                          ║
║     • Slide — translate window texture by direction vector          ║
║     • Pop In — scale up from center with opacity fade               ║
║     • Slide Fade — partial translate with alpha blend               ║
║     • Custom — user-provided GLSL fragment shader                   ║
║                                                                     ║
║     The shader system will reuse the existing zone overlay shader   ║
║     pipeline (ZoneShaderNodeRHI) with animation-specific uniforms:  ║
║     pz_progress, pz_window_tex, pz_source_rect, pz_target_rect.   ║
║                                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## Reusable Component: AnimationEventCard

Each per-event card follows the same pattern. The `AnimationEventCard.qml`
component encapsulates:

```
Properties:
  - eventName: string        ("zoneSnapIn")
  - eventLabel: string       ("Zone Snap In")
  - parentChain: string      ("Zone Snap → Global")
  - resolvedProfile: object  (from settingscontroller)
  - showStyleSelector: bool  (true for snap/layout events, false for fades)

States:
  - Override OFF: shows inheritance info only (collapsed, ~88px tall)
  - Override ON: compact controls with mini curve thumbnail (~200px tall)

Override ON layout:
  - Left column: CurveThumbnail (80x50px, read-only, clickable)
  - Right column: timing mode combo, style combo, duration slider, curve preset combo
  - Bottom: "Customize Curve..." button → opens CurveEditorDialog
```

---

## Reusable Component: CurveEditorDialog

```
Properties:
  - eventName: string       (for dialog title)
  - timingMode: int         (0=easing, 1=spring)
  - easingCurve: string     (current bezier/named curve)
  - spring: object          (dampingRatio, stiffness, epsilon)

Visual:
  - Kirigami.Dialog with title "Customize Curve — {eventLabel}"
  - Content switches based on timingMode:
    Easing: full EasingPreview (drag handles) + EasingSettings (style/direction/amplitude)
    Spring: full SpringPreview (ball-on-spring + graph) + spring sliders
  - Footer: Apply + Cancel buttons
  - On Apply: emits curveChanged signal, updates the card's CurveThumbnail

Size: ~500x520px dialog
```

---

## Reusable Component: CurveThumbnail

```
Properties:
  - curve: string           (bezier "x1,y1,x2,y2" or named curve)
  - timingMode: int         (0=easing, 1=spring)
  - spring: object          (for spring mode rendering)

Visual:
  - 80x50px read-only Canvas rendering of the current curve
  - Easing mode: draws the bezier curve path (no handles, no grid)
  - Spring mode: draws the damped oscillation waveform
  - Subtle border, rounded corners, dark background
  - Hover effect (slight brighten) indicating it's clickable
  - Click opens CurveEditorDialog
```

---

## Reusable Component: SpringPreview

```
Properties:
  - dampingRatio: real
  - stiffness: real
  - epsilon: real
  - previewEnabled: bool

Visual:
  - Top: animated ball-on-spring (horizontal, ball oscillates to target)
  - Bottom: damped oscillation graph (amplitude vs time)
  - Graph shows: overshoot for underdamped, no overshoot for critical/overdamped
  - Preset selector: Snappy / Smooth / Bouncy / Custom

Size: same dimensions as EasingPreview (maxWidth: 28 gridUnits)
Used in: AnimationsGeneralPage (inline) and CurveEditorDialog (popup)
```
