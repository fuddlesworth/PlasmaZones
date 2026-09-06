<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Mockups v2 (spectrum identity)

Drawn from [`../05-visual-identity.md`](../05-visual-identity.md). The v1 set was
removed in phase 6.

## Conventions

- `viewBox="0 0 1280 720"`, SMIL-animated (2–8 s loops), annotations inline so each file is
  self-contained. Render with `rsvg-convert -w 1600 file.svg -o file.png` to check.
- Every file starts from `_template.svg`: same `<defs>` ids, same fonts
  (`Manrope` for UI, `JetBrains Mono` for values, with `Noto Sans` fallbacks), same
  annotation callout style.
- The spectrum gradient is defined **once per file** as `#rail` (horizontal, x 0→1280) and
  everything that encodes screen position samples it by `x`. Nothing gets its own gradient.
  Use `stroke="url(#rail)"` on a full-width or full-height element, or pick the sampled hex
  for a small element with the helper table below.
- Hue samples along the rail (`t` = x/1280): 0 `#22D3EE`, 0.17 `#2FB0F2`, 0.33 `#3B82F6`,
  0.5 `#6F6CF7`, 0.67 `#A855F7`, 0.83 `#CE4BA8`, 1.0 `#F43F5E`.
- Ground colours: void `#050916`, abyss `#070F22`, navy `#0B1730`. Text `#E6EDFF` /
  `#94A3B8`. Focus and urgency white `#FFFFFF`. Success `#10B981`, warning `#FBBF24`.
- No drop shadows, no filled accent buttons, no pills, radii 6 / 8 / 10 / 3 only.
- Motion in SMIL: enter = `keySplines="0.34 1 0.64 1"` over 0.09–0.14 s of the loop;
  release = `keySplines="0.05 0.6 0.15 1"` over 0.36–0.72 s. Use `calcMode="spline"`.
  SMIL control points must stay within 0..1 (a value like `1.2` makes browsers drop the
  whole `<animate>`), so the enter overshoot is an explicit keyframe, never in the curve.
- `rsvg-convert` ignores SMIL, so set each file's base attributes to a representative
  "hold" frame; browsers still play the loop from its first keyframe.
- Callouts: `<g class="note">` with a 1 px `#1E293B` stroke box on `#070F22` at 0.92, title
  in the spectrum hue of the thing it points at, body in `#94A3B8` 12 px. Keep them outside
  the desktop area where possible.

## Files

| File | Surface |
|------|---------|
| `design-system.svg` | Spectrum axes, stroke/gleam layers, motion envelopes, type scale |
| `bar-modes.svg` | The rail + placement map on one screen morphing snapping → tiling → scrolling |
| `bar-widgets.svg` | Full bar at rest with every chip, hover and urgency states |
| `control-center.svg` | Engine-placed pane in each mode with the tether |
| `launcher.svg` | Viewfinder launcher: Windows and Apps providers |
| `notifications.svg` | Edge-band toast, drag-to-rect, notification-center column |
| `osd.svg` | Edge bands: volume, brightness, caps, media, coexisting and travelling |
| `power-menu.svg` | Dimmed desktop, word column, spectrum wipe commit |
| `lockscreen.svg` | Outline map, clock in the empty region, unlock fill |
| `dashboard.svg` | Overview grid of every desktop's map plus the three cells |
| `wallpaper-theme-picker.svg` | Bottom strip, live retint on hover |
| `polkit-cheatsheet.svg` | Polkit card on the requesting window; cheatsheet chords on rects |
