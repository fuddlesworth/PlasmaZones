// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

Column {
    id: root
    property bool detailed: false
    width: parent ? parent.width : 0
    spacing: detailed ? 12 : 13
    Repeater {
        model: root.detailed ? StatsModel.drives : StatsModel.drives.slice(0, 2)
        Rectangle {
            id: drive
            required property var modelData
            readonly property bool low: modelData.usage >= 90
            readonly property real paddingSize: root.detailed ? 16 : 0
            width: root.width
            height: content.implicitHeight + paddingSize * 2
            radius: Appearance.radius * 0.6
            color: root.detailed ? Qt.alpha(Appearance.card, 0.4) : "transparent"
            border.width: root.detailed ? 1 : 0
            border.color: low ? Qt.alpha(Appearance.stops[3], 0.5) : Appearance.outline
            Column {
                id: content
                x: drive.paddingSize
                y: drive.paddingSize
                width: parent.width - drive.paddingSize * 2
                spacing: 8
                RowLayout {
                    width: parent.width
                    spacing: 8
                    DetailText {
                        Layout.fillWidth: true
                        text: drive.modelData.mount === "/" ? qsTr("System") : drive.modelData.mount === "/home" ? qsTr("Home") : drive.modelData.name || drive.modelData.mount
                        size: root.detailed ? 12 : 10
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                    }
                    DetailText {
                        text: drive.low ? qsTr("%1 free · Low space").arg(StatsModel.bytes(drive.modelData.free)) : qsTr("%1 free").arg(StatsModel.bytes(drive.modelData.free))
                        color: drive.low ? Appearance.stops[3] : Appearance.muted
                        size: 9
                    }
                }
                DetailText {
                    visible: root.detailed
                    width: parent.width
                    text: drive.modelData.mount + " · " + drive.modelData.device
                    muted: true
                    size: 9
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                }
                Rectangle {
                    width: parent.width
                    height: 5
                    radius: 2
                    color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.muted, 0.15))
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, drive.modelData.usage / 100))
                        height: parent.height
                        radius: 2
                        color: Appearance.stops[3]
                    }
                }
                RowLayout {
                    visible: root.detailed
                    width: parent.width
                    DetailText {
                        Layout.fillWidth: true
                        text: qsTr("%1 / %2").arg(StatsModel.bytes(drive.modelData.used)).arg(StatsModel.bytes(drive.modelData.total))
                        muted: true
                        size: 9
                    }
                    DetailText {
                        text: qsTr("%1 used").arg(StatsModel.percent(drive.modelData.usage))
                        muted: true
                        size: 9
                    }
                }
            }
        }
    }
    DetailText {
        visible: StatsModel.drives.length === 0
        width: parent.width
        text: qsTr("No local drive readings available.")
        muted: true
        size: 11
    }
}
