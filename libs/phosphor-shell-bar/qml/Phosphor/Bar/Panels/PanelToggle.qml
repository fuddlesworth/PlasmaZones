// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PanelToggle, the on/off switch in a panel's header.
//
// A word with a 2 px underline: lit on the state axis when on, dim when
// off. The same device as every other value in the shell, so a panel's
// master switch reads as part of the system rather than as a control
// borrowed from somewhere else.
//
// This replaced a `PhosphorButton { variant: Filled }`, which drew a
// filled accent PILL. R1 puts colour in strokes, bands and underlines and
// never in a button background; R3 allows 6 / 8 / 10 px radii and says
// "no pills". It was two rule violations in the most prominent control on
// the surface, and it made the panel look like a different toolkit's.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    /// Whether the thing this switches is on.
    property bool checked: false
    /// What the switch governs, for assistive tech ("Wi-Fi", "Bluetooth").
    property string subject: ""
    /// False when there is nothing to switch — no adapter, networking off
    /// wholesale — so the control goes inert rather than offering a write
    /// the daemon will refuse.
    property bool available: true

    signal toggled

    implicitWidth: Math.max(34, label.implicitWidth)
    implicitHeight: label.implicitHeight + 6

    opacity: root.available ? 1 : StateLayer.disabled_content

    Accessible.role: Accessible.Button
    Accessible.name: root.checked ? qsTr("Turn %1 off").arg(root.subject) : qsTr("Turn %1 on").arg(root.subject)
    Accessible.onPressAction: root._activate()

    function _activate() {
        if (root.available)
            root.toggled();
    }

    HoverHandler {
        id: hover

        enabled: root.available
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.available
        onTapped: root._activate()
    }

    Text {
        id: label

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        text: root.checked ? qsTr("On") : qsTr("Off")
        color: Theme.on_surface
        // Brightness carries the state, exactly as it does on the bar's own
        // chips: on reads at full, off sits back.
        opacity: root.checked ? 1 : (hover.hovered ? 0.8 : 0.55)
        font.pixelSize: Tokens.font_size_label_l
        font.family: Tokens.font_family_ui
        font.weight: Tokens.font_weight_medium

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: root.checked ? Spectrum.at(0.33) : Theme.on_surface
        opacity: root.checked ? 1 : (hover.hovered ? 0.45 : 0.22)

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
    }
}
