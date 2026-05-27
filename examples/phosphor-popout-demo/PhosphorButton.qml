// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Demo-only Button skin. Wraps QtQuick.Controls Button with chrome
// driven by Phosphor.Theme tokens so the demo doesn't look like
// stock Qt against the rest of the Phosphor surface. A real shell
// would consume a PhosphorButton from a Phosphor.UI library; this
// inline copy exists so the popout demo stays self-contained.

import Phosphor.Theme
import QtQuick
import QtQuick.Controls

Button {
    id: root

    // Optional accent override. When set, the button fills with the
    // accent color and adapts its label color to the accent's `on_*`
    // pair. When null, the button uses the surface ramp like a
    // regular tonal button.
    property color accentColor: "transparent"
    property color onAccentColor: Theme.on_surface
    readonly property bool _accented: accentColor.a > 0
    readonly property color _fill: {
        if (root.down)
            return root._accented ? root.accentColor : Theme.surface_container_high;

        if (root.hovered)
            return root._accented ? root.accentColor : Theme.surface_container_high;

        return root._accented ? root.accentColor : Theme.surface_container;
    }
    readonly property color _label: root._accented ? root.onAccentColor : Theme.on_surface

    padding: Tokens.spacing_m
    leftPadding: Tokens.spacing_l
    rightPadding: Tokens.spacing_l

    background: Rectangle {
        implicitHeight: Tokens.spacing_xxl
        radius: Tokens.radius_full
        color: root._fill
        border.color: root._accented ? root.accentColor : Theme.outline_variant
        border.width: 1

        Behavior on color {
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
    }

}
