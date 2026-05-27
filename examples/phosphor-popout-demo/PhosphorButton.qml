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
    // Optional accent override. When `accented` is true the button
    // fills with `accentColor` and renders its label in `labelColor`
    // to keep contrast against the accent. When `accented` is false
    // the button uses the surface ramp like a regular tonal button
    // and the accentColor/labelColor values are ignored.
    // The label property is named `labelColor` rather than
    // `onAccentColor`. QML grammar reserves identifiers starting
    // with `on` for signal-handler shorthand, so a property called
    // `onAccentColor` is silently parsed as a handler for the
    // `accentColorChanged` signal at every call site.

    id: root

    // `accented` is an explicit boolean rather than something derived
    // from accentColor's alpha. Deriving it broke for callers passing
    // semi-transparent theme colors; deriving via != "transparent"
    // broke for callers passing Qt.rgba(r, g, b, 0). The explicit
    // flag is the only form that doesn't surprise either case.
    property bool accented: false
    property color accentColor: Theme.primary
    property color labelColor: Theme.on_primary
    readonly property color _fill: {
        // Phosphor.Theme exposes surface_container, surface_container_high.
        // The pressed-state needs a darker step than hover; Qt.darker on
        // the hover token approximates the M3 surface_container_highest
        // ramp without depending on a token the theme library does not
        // yet publish.
        if (root.down)
            return root.accented ? Qt.darker(root.accentColor, 1.15) : Qt.darker(Theme.surface_container_high, 1.1);

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
