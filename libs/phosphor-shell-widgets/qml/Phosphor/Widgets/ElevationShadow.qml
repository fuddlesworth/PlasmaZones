// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.ElevationShadow, Material 3 elevation shadow.
//
// A MultiEffect tuned for the six M3 elevation levels (0..5 dp tiers).
// Use it as a layer effect on any rounded surface; the engine wires the
// layered item into the effect's `source` automatically, so the shadow
// tracks the surface geometry and corner radius without extra plumbing:
//
//   Rectangle {
//       radius: 16
//       layer.enabled: true
//       layer.effect: ElevationShadow { level: 2 }
//   }
//
// Levels map to M3's elevation ramp: 0 = flat (no shadow), 1 = resting
// cards, 2 = raised buttons, 3 = menus / popouts, 4 = nav drawers,
// 5 = modal dialogs. The per-tier offset/blur/opacity values live in the
// design system (Tokens.elevation_0..5); this is their single renderer, so
// a retune in Tokens.qml propagates to every elevated surface in the shell.

import QtQuick
import QtQuick.Effects
import Phosphor.Theme

MultiEffect {
    id: root

    // M3 elevation tier, clamped to the supported 0..5 range. 0 disables
    // the shadow entirely (the effect still renders the source, so a
    // level-0 ElevationShadow is a transparent pass-through).
    property int level: 1

    // Clamp once so the per-tier lookup never indexes out of bounds when a
    // caller passes a level outside 0..5.
    readonly property int _level: Math.max(0, Math.min(5, level))
    // The selected design-system elevation tier. The tier object also
    // carries a `tint` field (read by PhosphorCard, not here); this effect
    // uses only y / blur / opacity. Reads the Tokens.* properties so a
    // Tokens retune re-evaluates here.
    readonly property var _tier: [Tokens.elevation_0, Tokens.elevation_1, Tokens.elevation_2, Tokens.elevation_3, Tokens.elevation_4, Tokens.elevation_5][_level]

    // MultiEffect crops the shadow to the source rect unless padding is
    // auto-grown. Without this the blur is clipped at the surface edge.
    autoPaddingEnabled: true
    // Kernel ceiling; shadowBlur below is expressed as a fraction of it.
    blurMax: 64

    shadowEnabled: _level > 0
    // Opaque black; the visible strength comes from shadowOpacity so the
    // colour and the strength stay independently tunable.
    shadowColor: Qt.rgba(0, 0, 0, 1)
    shadowHorizontalOffset: 0
    // Tokens.elevation blur is in px; MultiEffect.shadowBlur is a 0..1
    // fraction of blurMax.
    shadowBlur: _tier.blur / blurMax
    shadowVerticalOffset: _tier.y
    shadowOpacity: _tier.opacity
}
