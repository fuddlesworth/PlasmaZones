// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import Phosphor.Widgets
import Phosphor.Theme
import Phosphor.Service.Network
import Phosphor.Service.UPower
import Phosphor.Service.PipeWire

AbstractButton {
    id: root
    signal activated
    property real railT: 0.93
    NetworkHost {
        id: network
    }
    UPowerHost {
        id: battery
    }
    readonly property var device: battery.displayDevice
    readonly property bool hasBattery: device && device.isPresent
    implicitWidth: symbols.implicitWidth + 24
    leftPadding: 12
    rightPadding: 12
    implicitHeight: 34
    Accessible.name: qsTr("Open quick settings")
    onClicked: activated()
    background: Rectangle {
        radius: 8
        color: Appearance.card
    }
    contentItem: Row {
        id: symbols
        spacing: 12
        ShellIcon {
            width: 15
            height: 15
            anchors.verticalCenter: parent.verticalCenter
            source: network.wirelessEnabled ? "network-wireless" : "network-wireless-offline"
            isMask: true
            color: Appearance.text
        }
        ShellIcon {
            width: 15
            height: 15
            anchors.verticalCenter: parent.verticalCenter
            source: PipeWireHost.defaultSink && PipeWireHost.defaultSink.muted ? "audio-volume-muted" : "audio-volume-high"
            isMask: true
            color: Appearance.text
        }
        Text {
            visible: root.hasBattery
            text: root.hasBattery ? Math.round(root.device.percentage) + "%" : ""
            color: Appearance.text
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
