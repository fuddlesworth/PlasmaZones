<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Phosphor Shell: Design

The design of the Phosphor shell: its identity, its surfaces, and the record
of how the identity was applied to the tree.

**Scope reminder:** Phosphor is its own standalone Wayland compositor + WM + shell. We implement the Wayland protocols (layer-shell, session-lock, foreign-toplevel, screencopy, and so on) ourselves in `phosphor-compositor`. The shell is Qt6/QML on the `phosphor-*` library tree. No other shell framework is consumed, extended, or targeted as a deployment surface, and no surface is a guest on another desktop.

## Start here

- [`05-visual-identity.md`](05-visual-identity.md), the identity: colour as a coordinate on the spectrum, thin strokes on navy, enter/hold/release motion, and the three claims no other shell can make because they need the window manager.
- [`identity/`](identity/), the four studies the identity synthesises: motion and material (A1), bar geometry (A2), every surface (A3), what the field has already taken (A4).
- [`mockups-v2/`](mockups-v2/), the SVG mockups of every surface, animated (SMIL). Their README carries the drawing conventions.

## The record

- [`01-feature-inventory.md`](01-feature-inventory.md), what the shell ships, surface by surface, and which claim each carries
- [`02-gap-analysis.md`](02-gap-analysis.md), what is still missing, ordered by how much identity it withholds
- [`03-component-map.md`](03-component-map.md), the modules and how data flows between the daemon, the shell process and the surfaces
- [`04-implementation-plan.md`](04-implementation-plan.md), the six phases, their commits, and what each proved live
- [`phosphor-ipc-followups.md`](phosphor-ipc-followups.md), open items on the IPC layer

## Conventions

- The palette is the canonical Phosphor theme at https://phosphor-works.github.io/palette/. Dark: background `#050916`, surface `#0B1730`, surface_container `#070F22`, on_surface `#E6EDFF`. The four brand stops, cyan `#22D3EE`, blue `#3B82F6`, purple `#A855F7`, rose `#F43F5E`, form the spectrum every stroke samples by position or by state. Light: the same spectrum on `#F6F9FF`.
- Tokens drive everything. Matugen replaces the palette at runtime from a wallpaper; the token names stay stable.
- Chrome radii are 3, 6, 8 and 10 px. No pills, no drop shadows.
- Faces: Manrope for UI, JetBrains Mono for values, resolved against the installed families with named fallbacks.
