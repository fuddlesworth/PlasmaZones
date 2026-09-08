<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# The Phosphor set: the reference for a cohesive cross-family theme

Read the actual sources before authoring. They are the style guide, not this summary.

## Inventory (what "a whole theme" covers)

| role | pack | family | class / category |
|---|---|---|---|
| open/close, universal | `phosphor-bloom`, `phosphor-scan`, `phosphor-ignite`, `phosphor-condense` | animations | appearance, Reveal/Particle |
| minimize | `phosphor-siphon` | animations | appearance, vert + grid 40 |
| geometry (placeIn/Out, layoutSwitch) | `phosphor-stream` | animations | geometry, vert + grid 40 |
| move (held drag) | `phosphor-vortex` | animations | move, fboExtent surface |
| desktop switch | `desktop-phosphor` | animations | desktop |
| desktop peek | `phosphor-peek` | animations | desktop |
| scrolling strip | `phosphor-gate` | animations | strip |
| tab switch | `phosphor-iris`, `phosphor-transfer` | animations | tab |
| zone overlay | `phosphor-flux` | overlays | Branded |
| window border | `border-phosphor` | surface | Borders, providesBorder |
| window glass | `phosphor-glass` | surface | Blur, multipass gaussian |
| ambience | `phosphor-motes` | surface | Ambience, paddingParam |
| curves | `phosphor-settle` (spring 22/0.85), `phosphor-release` (bezier .05,.6,.15,1) | curves | |
| decoration set | `~/.local/share/plasmazones/decorationsets/phosphor.json` | set | chains for window, shell.panel, osd, popups |

A minimum viable theme is one pack per row marked open/close, geometry, move, desktop switch,
overlay, border, glass, plus two curves and the sets. The full set adds minimize, peek, strip,
tab and ambience. The Phosphor set was grown one event class at a time, each addition audited.

## What makes it cohesive

1. **One palette, declared as identical `color` params in every pack.** Every pack ends its
   parameter list with the same four stops (`colorCyan #22D3EE`, `colorBlue #3B82F6`,
   `colorPurple #A855F7`, `colorRose #F43F5E`) plus a ground (`colorTint`/`backgroundColor #0B1730`).
   Same ids, same names, same defaults, across all three families.
2. **One signature helper, copied verbatim into every pack** (`fluxGradient(t)`, a four-stop
   mix). Families cannot share includes, so cohesion is by duplicated helper with identical
   param names, not by a shared header. Do the same: define the theme's helper once in the
   brief, paste it into every pack.
3. **One motif vocabulary** reused across packs: "luminous streams", "ember sparks shed in the
   wake", "dark navy silhouette", "the brand gradient from cyan to rose", "gleam orbiting the
   frame". Each pack reuses two or three motifs from the previous ones.
4. **One motion character** (identity doc `docs/phosphor-shell-design/identity/A1-identity-motion.md`):
   enter fast, leave slow (out envelopes 2 to 4 times longer), retarget never restart, colour in
   strokes never fills, gradient is one screen-wide field. The two curves encode that.
5. **Naming.** Window packs `phosphor-<verb-noun>`. Cross-family members follow the HOST
   family's prefix: `desktop-phosphor` (like `desktop-fade`), `border-phosphor` (like
   `border-pulse`). Overlay and non-border surface packs keep `phosphor-` first.
   Curves `phosphor-<primitive>`.
6. **Descriptions** are 2 to 4 declarative present-tense sentences, spell out the reverse leg
   in the last sentence, repeat the theme phrase ("in the Phosphor style"), and obey the
   CLAUDE.md prose rules. Examples:

   > The window fills with light along a diagonal sweep in the official Phosphor style. Ahead
   > of the front the surface is a dark navy silhouette, and the front itself carries the brand
   > gradient from cyan to rose with a soft shimmering glow.

   > Grab a window and a plasma vortex around the cursor inhales it, pulling the surface into
   > glowing streamlines that erode as they feed in. The plasma keeps orbiting for as long as
   > the drag is held, stretches into a comet along the path while you move, and releases the
   > window back when you let go.

7. **Fallback discipline.** Every colour param is read through a guard
   (`length(p_c.rgb) > 0.01 ? p_c.rgb : constant`) and every scalar through a
   `p_x >= 0.0 ? p_x : default` getter in overlays, so a pack still renders if the host
   uploads zeros.

## Files to read before writing each family

- animation, symmetric: `data/animations/phosphor-bloom/effect.frag` (108 lines, fully commented)
- animation with vert + grid: `data/animations/phosphor-siphon/`, `phosphor-stream/`
- move: `data/animations/phosphor-vortex/`
- desktop: `data/animations/desktop-phosphor/`
- overlay: `data/overlays/phosphor-flux/effect.frag` (573 lines, five layers, audio structural)
- surface: `data/surface/border-phosphor/effect.frag` (64 lines), `phosphor-glass/` (multipass blur), `phosphor-motes/` (padding ambience)
- identity: `docs/phosphor-shell-design/identity/A1-identity-motion.md`, `A3-surfaces.md`
