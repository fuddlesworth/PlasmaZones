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
    // Optional caption rendered to the right of the toggle. Empty by default, so
    // every existing call site (which already carries its own row label to the
    // left) keeps the bare toggle and its prior implicit size. The rule-builder
    // bool editors set it to a per-instance On/Off state read so a lone toggle
    // standing in a value column still says what is or isn't happening.
    property string label: ""

    signal toggled(bool newValue)

    implicitWidth: track.width + (stateLabel.visible ? Kirigami.Units.smallSpacing + stateLabel.implicitWidth : 0)
    implicitHeight: Kirigami.Units.gridUnit
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleName
    Accessible.checked: root.checked
    Accessible.onToggleAction: root.toggled(!root.checked)
    // Keyboard reachable. A card's master toggle hides its rows when off, and
    // those rows leave the focus chain with them, so a toggle that only answered
    // to the mouse would strand a keyboard user in a card they cannot re-enable.
    activeFocusOnTab: true
    Keys.onSpacePressed: root.toggled(!root.checked)
    Keys.onReturnPressed: root.toggled(!root.checked)
    Keys.onEnterPressed: root.toggled(!root.checked)

    // Focus ring, so the keyboard position is visible.
    Rectangle {
        anchors.fill: track
        anchors.margins: -Kirigami.Units.smallSpacing / 2
        radius: height / 2
        color: "transparent"
        visible: root.activeFocus
        border.width: 1
        border.color: Kirigami.Theme.highlightColor
    }

    // Track
    Rectangle {
        id: track

        width: Kirigami.Units.gridUnit * 2
        height: Kirigami.Units.gridUnit
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
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

    // Optional state caption. Mirrors the toggle's emphasis: full text colour
    // when on, muted when off, so the word reads as part of the live state.
    Label {
        id: stateLabel

        visible: root.label.length > 0
        text: root.label
        anchors.left: track.right
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: track.verticalCenter
        color: root.checked ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
        // The toggle already exposes its state through Accessible.checked; the
        // caption is a visual echo, so keep it out of the a11y tree to avoid a
        // duplicate "On/Off" reading.
        Accessible.ignored: true
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
