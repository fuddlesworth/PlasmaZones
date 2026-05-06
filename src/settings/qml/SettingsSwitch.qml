// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation

Item {
    id: root

    property bool checked: false
    property string accessibleName: ""

    signal toggled(bool newValue)

    implicitWidth: Kirigami.Units.gridUnit * 2
    implicitHeight: Kirigami.Units.gridUnit
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleName
    Accessible.checked: root.checked

    // Track
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)

        // Knob
        Rectangle {
            readonly property int pad: Kirigami.Units.smallSpacing / 2

            width: parent.height - Kirigami.Units.smallSpacing
            height: width
            radius: height / 2
            color: Kirigami.Theme.highlightedTextColor
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
            border.width: 0.5
            y: pad
            x: root.checked ? parent.width - width - pad : pad

            Behavior on x {
                PhosphorMotionAnimation {
                    // Direction-bound: profile is read at animation start, after
                    // root.checked has already flipped to the destination state.
                    profile: root.checked ? "widget.toggleOn" : "widget.toggleOff"
                }

            }

        }

        // Color uses widget.tint (no overshoot) — overshooting a colour
        // interpolation produces clamped out-of-gamut intermediates.
        // Position (Behavior on x above) keeps the toggle's overshoot.
        Behavior on color {
            PhosphorMotionAnimation {
                profile: "widget.tint"
            }

        }

    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }

}
