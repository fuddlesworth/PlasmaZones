// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorCard, M3 elevated surface container.
//
// A rounded surface-container panel with an M3 elevation shadow and a
// padded content area. Children declared inside the card land in the
// padded area via the default property:
//
//   PhosphorCard {
//       elevation: 2
//       ColumnLayout { ... }
//   }
//
// Size follows the content's implicit size plus padding, so a card wraps
// its contents by default; give the card an explicit width / height to
// override. `elevation` maps to the M3 tiers in ElevationShadow.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    // Children land in the padded content area, not directly on the card
    // root, so the elevation surface stays behind them.
    default property alias content: contentArea.data

    // M3 elevation tier (0..5). Drives both halves of elevation: the
    // shadow (forwarded to ElevationShadow) and the surface tint overlay
    // (the colour shift below).
    property int elevation: 1
    property real radius: Tokens.radius_l
    property real padding: Tokens.spacing_l

    implicitWidth: contentArea.implicitWidth + padding * 2
    implicitHeight: contentArea.implicitHeight + padding * 2

    // Clamp once so the M3 tint ramp below never indexes out of range.
    readonly property int _level: Math.max(0, Math.min(5, elevation))

    // M3 surface tint overlay: an elevated surface is tinted with the
    // surface-tint colour at an elevation-dependent opacity, layered over
    // the base container colour. This is the colour half of elevation;
    // ElevationShadow is the shadow half. The per-tier tint opacity is the
    // `tint` field of the Tokens.elevation_* tiers (the same tier objects
    // ElevationShadow reads for its shadow), so the whole elevation ramp
    // lives in Tokens.qml. Theme.surface_tint defaults to the primary
    // accent (M3 default) but honours an explicit surface_tint palette
    // token when present.
    //
    // The binding reads Theme.surface_container and Theme.surface_tint as
    // property reads (not via a Theme function), so it stays tracked and
    // the card retints live on a palette change. Calling a Theme method
    // here would silently break that, see phosphor-theme's Theme.qml.
    readonly property color _tintedSurface: {
        const a = [Tokens.elevation_0, Tokens.elevation_1, Tokens.elevation_2, Tokens.elevation_3, Tokens.elevation_4, Tokens.elevation_5][_level].tint;
        return a > 0 ? Qt.tint(Theme.surface_container, Qt.rgba(Theme.surface_tint.r, Theme.surface_tint.g, Theme.surface_tint.b, a)) : Theme.surface_container;
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        radius: root.radius
        color: root._tintedSurface
        // Only pay for the layer + shadow pass when actually elevated; a
        // flat card (elevation 0) renders as a plain rounded surface.
        layer.enabled: root.elevation > 0
        layer.effect: ElevationShadow {
            level: root.elevation
        }

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }
    }

    Item {
        id: contentArea

        anchors.fill: parent
        anchors.margins: root.padding
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }
}
