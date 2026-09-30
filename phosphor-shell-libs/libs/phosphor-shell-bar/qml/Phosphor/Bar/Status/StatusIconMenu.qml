// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    required property var controller
    required property var entry
    signal detailRequested(string panelId)
    spacing: Tokens.spacing_xs
    readonly property string iconId: entry.id || ""
    readonly property var power: controller ? controller.icons.find(item => item.id === "power") : null
    readonly property bool canToggle: !!entry.available && !entry.pending
    readonly property string detailLabel: iconId === "wifi" ? i18n("Choose a network") : iconId === "bluetooth" ? i18n("Connect a device") : iconId === "audio" ? i18n("Sound controls") : iconId === "microphone" ? i18n("Input controls") : iconId === "focus" ? i18n("Notification center") : i18n("%1 details").arg(entry.title || i18n("Indicator"))
    function focusFirst(): void {
        const items = focusItems();
        if (items.length)
            items[0].forceActiveFocus();
    }
    function focusItems(): var {
        const items = [volume, toggle, saver];
        for (let i = 0; i < outputs.count; ++i)
            items.push(outputs.itemAt(i));
        for (let i = 0; i < profiles.count; ++i)
            items.push(profiles.itemAt(i));
        items.push(details);
        return items.filter(item => item && item.visible && item.enabled);
    }
    Keys.onPressed: event => {
        if (![Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key))
            return;
        const items = focusItems();
        if (!items.length)
            return;
        const current = items.findIndex(item => item.activeFocus);
        const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? items.length - 1 : (current + (event.key === Qt.Key_Up ? -1 : 1) + items.length) % items.length;
        items[next].forceActiveFocus();
        event.accepted = true;
    }
    DetailNotice {
        text: root.entry.error || ""
        error: true
    }
    Column {
        visible: root.iconId === "audio"
        width: parent.width
        spacing: Tokens.spacing_s
        RowLayout {
            width: parent.width
            DetailText {
                Layout.fillWidth: true
                text: i18n("Volume")
                muted: true
                font.pixelSize: Tokens.font_size_label_s
            }
            DetailText {
                text: root.controller && root.controller.muted ? i18n("Muted") : i18n("%1%").arg(root.controller ? root.controller.volume : 0)
                font.pixelSize: Tokens.font_size_label_s
            }
        }
        Basic.Slider {
            id: volume
            objectName: "statusVolume"
            width: parent.width
            implicitHeight: 30
            from: 0
            to: 100
            stepSize: 1
            value: root.controller ? root.controller.volume : 0
            enabled: root.controller && root.controller.audioAvailable
            Accessible.name: i18n("Volume")
            onMoved: root.controller.setVolume(Math.round(value))
            background: Rectangle {
                x: volume.leftPadding
                y: (volume.height - height) / 2
                width: volume.availableWidth
                height: 6
                radius: height / 2
                color: Appearance.recess
                Rectangle {
                    width: parent.width * volume.visualPosition
                    height: parent.height
                    radius: parent.radius
                    color: Appearance.stops[1]
                }
            }
            handle: Rectangle {
                x: volume.leftPadding + volume.visualPosition * (volume.availableWidth - width)
                y: (volume.height - height) / 2
                width: 14
                height: width
                radius: width / 2
                color: Appearance.text
                border.width: volume.visualFocus ? 2 : 0
                border.color: Appearance.stops[1]
            }
        }
    }
    Column {
        visible: root.iconId === "battery"
        width: parent.width
        spacing: Tokens.spacing_m
        Basic.ProgressBar {
            id: charge
            visible: root.entry.percent >= 0
            width: parent.width
            height: 6
            from: 0
            to: 100
            value: root.entry.percent >= 0 ? root.entry.percent : 0
            background: Rectangle {
                radius: height / 2
                color: Appearance.recess
            }
            contentItem: Item {
                Rectangle {
                    width: parent.width * charge.position
                    height: parent.height
                    radius: height / 2
                    color: Appearance.stops[0]
                }
            }
            Accessible.name: i18n("Battery charge")
        }
        DetailText {
            width: parent.width
            text: root.entry.summary || ""
            muted: true
            font.pixelSize: Tokens.font_size_label_s
        }
    }
    StatusMenuItem {
        id: toggle
        objectName: "statusToggle-" + root.iconId
        visible: root.iconId !== "battery" && root.iconId !== "power" && root.iconId !== ""
        text: root.iconId === "audio" ? i18n("Mute sound") : root.iconId === "microphone" ? i18n("Mute microphone") : root.entry.title || ""
        iconName: root.entry.iconName || "configure"
        checkable: true
        selected: root.iconId === "audio" || root.iconId === "microphone" ? !!root.entry.off : root.iconId === "wifi" || root.iconId === "bluetooth" ? !root.entry.off : !!root.entry.active
        enabled: root.canToggle
        onClicked: root.controller.toggle(root.iconId)
    }
    StatusMenuItem {
        id: saver
        objectName: "statusUsePowerSaver"
        visible: root.iconId === "battery"
        text: i18n("Use Power saver")
        iconName: "battery-low"
        enabled: root.power && root.power.available && !root.power.pending && root.controller.powerProfiles.includes("power-saver")
        onClicked: root.controller.setPowerProfile("power-saver")
    }
    DetailText {
        visible: root.iconId === "audio"
        width: parent.width
        topPadding: Tokens.spacing_m
        bottomPadding: Tokens.spacing_xs
        text: i18n("OUTPUT DEVICE")
        muted: true
        font.pixelSize: Tokens.font_size_label_s
    }
    Repeater {
        id: outputs
        model: root.iconId === "audio" && root.controller ? root.controller.outputs : []
        delegate: StatusMenuItem {
            required property var modelData
            objectName: "statusOutput-" + modelData.id
            text: modelData.name
            iconName: "audio-speakers-symbolic"
            radio: true
            selected: modelData.selected
            enabled: root.canToggle
            onClicked: root.controller.selectOutput(modelData.id)
        }
    }
    DetailText {
        visible: root.iconId === "audio" && outputs.count === 0
        width: parent.width
        text: i18n("No output devices are available.")
        muted: true
        font.pixelSize: Tokens.font_size_label_s
    }
    Repeater {
        id: profiles
        model: root.iconId === "power" && root.controller ? root.controller.powerProfiles : []
        delegate: StatusMenuItem {
            required property string modelData
            objectName: "statusProfile-" + modelData
            text: modelData === "power-saver" ? i18n("Power saver") : modelData === "performance" ? i18n("Performance") : i18n("Balanced")
            iconName: "speedometer"
            radio: true
            selected: root.controller.powerProfile === modelData
            enabled: root.canToggle
            onClicked: root.controller.setPowerProfile(modelData)
        }
    }
    Rectangle {
        width: parent.width
        height: 1
        color: Appearance.outline
    }
    StatusMenuItem {
        id: details
        objectName: "statusDetails"
        visible: !!root.entry.detailId
        text: root.detailLabel
        iconName: "go-next-symbolic"
        onClicked: root.detailRequested(root.entry.detailId)
    }
}
