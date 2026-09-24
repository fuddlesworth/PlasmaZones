// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    property var availableWidgets: []
    readonly property var layout: Appearance.settings.barLayout
    readonly property var placed: [].concat(...layout.left, ...layout.center, ...layout.right)
    readonly property var hiddenWidgets: availableWidgets.filter(id => placed.indexOf(id) < 0)
    spacing: 8
    Text {
        text: qsTr("Bar widgets")
        color: Appearance.text
        font.family: Tokens.font_family_ui
        font.pixelSize: 15
    }
    Repeater {
        model: [
            {
                key: "left",
                label: qsTr("Left")
            },
            {
                key: "center",
                label: qsTr("Center")
            },
            {
                key: "right",
                label: qsTr("Right")
            }
        ]
        Column {
            id: region
            required property var modelData
            readonly property var ids: [].concat(...root.layout[modelData.key])
            width: root.width
            spacing: 4
            Text {
                text: region.modelData.label
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 12
            }
            Repeater {
                model: region.ids
                RowLayout {
                    required property string modelData
                    required property int index
                    width: region.width
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: ({
                                placementmap: qsTr("Workspace map"),
                                focusedapp: qsTr("Focused app"),
                                clock: qsTr("Date and time"),
                                media: qsTr("Media"),
                                tray: qsTr("System tray"),
                                audio: qsTr("Volume"),
                                network: qsTr("Network"),
                                bluetooth: qsTr("Bluetooth"),
                                battery: qsTr("Battery"),
                                notification: qsTr("Notifications"),
                                controlcenter: qsTr("Quick settings"),
                                power: qsTr("Power")
                            })[modelData] || modelData
                        color: Appearance.text
                        elide: Text.ElideRight
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 12
                    }
                    ShellButton {
                        text: "↑"
                        implicitWidth: 28
                        enabled: index > 0
                        Accessible.name: qsTr("Move %1 earlier").arg(modelData)
                        onClicked: AppearanceStore.moveWidget(modelData, region.modelData.key, index - 1)
                    }
                    ShellButton {
                        text: "↓"
                        implicitWidth: 28
                        enabled: index + 1 < region.ids.length
                        Accessible.name: qsTr("Move %1 later").arg(modelData)
                        onClicked: AppearanceStore.moveWidget(modelData, region.modelData.key, index + 1)
                    }
                    Repeater {
                        model: ["left", "center", "right"]
                        ShellButton {
                            required property string modelData
                            readonly property string widgetId: parent.modelData
                            objectName: "move-" + widgetId + "-" + modelData
                            text: modelData === "left" ? "←" : modelData === "right" ? "→" : "·"
                            implicitWidth: 28
                            highlighted: region.modelData.key === modelData
                            enabled: region.modelData.key !== modelData
                            Accessible.name: qsTr("Move %1 to %2").arg(widgetId).arg(modelData)
                            onClicked: AppearanceStore.moveWidget(widgetId, modelData)
                        }
                    }
                    ShellButton {
                        text: "×"
                        implicitWidth: 28
                        Accessible.name: qsTr("Hide %1").arg(modelData)
                        onClicked: AppearanceStore.moveWidget(modelData, "")
                    }
                }
            }
        }
    }
    Flow {
        width: parent.width
        spacing: 4
        Repeater {
            model: root.hiddenWidgets
            ShellButton {
                required property string modelData
                text: qsTr("Add %1").arg(modelData)
                onClicked: AppearanceStore.moveWidget(modelData, "right")
            }
        }
    }
    ShellButton {
        text: qsTr("Reset widget arrangement")
        onClicked: AppearanceStore.resetBarLayout()
    }
}
