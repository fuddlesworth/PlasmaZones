// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    property var node: null
    property string label: ""
    property bool microphone: false
    readonly property real volume: node && node.volumes.length ? Math.max(0, Math.min(1, node.volumes[0])) : 0
    readonly property bool muted: node ? node.muted : false
    width: parent ? parent.width : 0
    spacing: 8
    RowLayout {
        width: parent.width
        DetailText {
            Layout.fillWidth: true
            text: root.label
            size: 11
        }
        DetailText {
            text: qsTr("%1%").arg(Math.round(root.volume * 100))
            size: 11
            muted: true
            font.family: Tokens.font_family_mono
        }
    }
    RowLayout {
        width: parent.width
        spacing: 11
        ShellButton {
            implicitWidth: 34
            implicitHeight: 34
            iconName: root.microphone ? (root.muted ? "microphone-sensitivity-muted" : "audio-input-microphone") : root.muted ? "audio-volume-muted" : "audio-volume-high"
            label: root.muted ? qsTr("Unmute %1").arg(root.label) : qsTr("Mute %1").arg(root.label)
            foreground: root.muted ? Appearance.stops[3] : Appearance.muted
            enabled: root.node !== null
            Accessible.checkable: true
            Accessible.checked: root.muted
            onClicked: root.node.setMuted(!root.muted)
        }
        Basic.Slider {
            id: slider
            Layout.fillWidth: true
            implicitHeight: 30
            padding: 0
            from: 0
            to: 1
            stepSize: 0.01
            value: root.volume
            enabled: root.node !== null && root.node.volumes.length > 0
            Accessible.name: root.label
            onMoved: root.node.setVolume(Math.max(0, Math.min(1, value)))
            background: Rectangle {
                y: (slider.height - height) / 2
                width: slider.width
                height: 10
                radius: 5
                color: Appearance.recess
                Rectangle {
                    width: parent.width * slider.visualPosition
                    height: parent.height
                    radius: parent.radius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: root.muted ? Appearance.muted : Appearance.stops[0]
                        }
                        GradientStop {
                            position: 1
                            color: root.muted ? Appearance.muted : Appearance.stops[2]
                        }
                    }
                }
            }
            handle: Rectangle {
                x: slider.visualPosition * (slider.width - width)
                y: (slider.height - height) / 2
                width: 7
                height: 22
                radius: 3
                color: Appearance.text
                border.width: slider.visualFocus ? 2 : 0
                border.color: Appearance.stops[1]
            }
        }
    }
    DetailText {
        visible: root.muted
        width: parent.width
        text: qsTr("Muted · Your level is remembered.")
        size: 10
        muted: true
    }
}
