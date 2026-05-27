// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Demo-only Button skin. Wraps QtQuick.Controls Button with chrome
// driven by Phosphor.Theme tokens so the demo doesn't look like
// stock Qt against the rest of the Phosphor surface. A real shell
// would consume a PhosphorButton from a Phosphor.UI library. This
// inline copy exists so the popout demo stays self-contained.

import Phosphor.Theme
import QtQuick
import QtQuick.Controls

Button {
    // Optional accent override. When set, the button fills with the
    // accent color and adapts its label color to the accent's
    // contrast pair via `labelColor`. When unset, the button uses
    // the surface ramp like a regular tonal button.

    id: root

    // The label property is named `labelColor` rather than
    // `onAccentColor`. QML grammar reserves identifiers starting
    // with `on` for signal-handler shorthand, so a property called
    // `onAccentColor` is silently parsed as a handler for the
    // `accentColorChanged` signal at every call site.
    property color accentColor: "transparent"
    property color labelColor: Theme.on_surface
    // Explicit accent toggle. Earlier this derived from accentColor.a > 0,
    // which silently flipped to "accented" for any caller passing a
    // semi-transparent theme color. Naming the flag here lets callers
    // request "draw as accented" independently of the color's alpha.
    property bool accented: accentColor != "transparent"
    readonly property color _fill: {
        if (root.down)
            return root.accented ? Qt.darker(root.accentColor, 1.15) : Theme.surface_container_highest;

        if (root.hovered)
            return root.accented ? root.accentColor : Theme.surface_container_high;

        return root.accented ? root.accentColor : Theme.surface_container;
    }
    readonly property color _borderColor: root.accented ? root.accentColor : Theme.outline_variant
    readonly property color _label: root.accented ? root.labelColor : Theme.on_surface

    padding: Tokens.spacing_m
    leftPadding: Tokens.spacing_l
    rightPadding: Tokens.spacing_l

    background: Rectangle {
        implicitHeight: Tokens.spacing_xxl
        radius: Tokens.radius_full
        color: root._fill
        border.color: root._borderColor
        border.width: 1

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }

        }

        // Without a matching Behavior, the border snaps to the new
        // accent color on the same frame the fill starts animating.
        // The visible result is a one-frame outline flicker against
        // the older fill.
        Behavior on border.color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }

        }

    }

    contentItem: Text {
        text: root.text
        color: root._label
        font.pixelSize: Tokens.font_size_label_l
        font.family: Tokens.font_family
        font.weight: Tokens.font_weight_medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }

        }

    }

}
