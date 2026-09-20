// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

ColumnLayout {
    id: root
    required property var controller
    signal dialogRequested(string mode, var preset)
    signal exportRequested(string id)
    spacing: 24
    RowLayout {
        Layout.fillWidth: true
        spacing: 24
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            LookText {
                text: qsTr("A STARTING POINT, NOT A BOX")
                kicker: true
            }
            LookText {
                text: qsTr("Keep what makes it yours.")
                size: 23
            }
            LookText {
                text: qsTr("Start with a look, change any part of it, then save your own.")
                muted: true
                size: 10
            }
        }
        Item {
            Layout.fillWidth: true
        }
        ShellButton {
            text: qsTr("Save current look")
            iconName: "document-save"
            highlighted: true
            implicitHeight: 37
            onClicked: root.dialogRequested("save", {})
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 16
        rowSpacing: 16
        Repeater {
            model: AppearanceLibrary.presets
            LookCard {
                id: card
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                readonly property var style: modelData.settings
                readonly property var palette: AppearanceStore.paletteFor(style)
                readonly property color base: palette.surface
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 136
                    radius: 8
                    color: card.base
                    clip: true
                    Rectangle {
                        width: parent.width * 1.5
                        height: parent.height
                        x: -30
                        y: 80
                        rotation: -18
                        radius: 65
                        color: card.palette.card
                    }
                    Rectangle {
                        x: 10
                        y: card.style.edge === "bottom" ? 108 : 10
                        width: parent.width - 20
                        height: 18
                        color: card.base
                        radius: Math.min(6, card.style.radius)
                        border.color: card.palette.outline
                        Text {
                            x: 7
                            y: 1
                            text: "φ"
                            color: card.palette.stops[0]
                            font.pixelSize: 13
                        }
                        Rectangle {
                            x: parent.width * .45
                            y: 5
                            width: 24
                            height: 8
                            color: "transparent"
                            border.color: card.palette.stops[1]
                        }
                    }
                    Rectangle {
                        x: parent.width * .18
                        y: 42
                        width: parent.width * .38
                        height: 61
                        radius: card.style.radius * .4
                        color: card.base
                        border.color: card.palette.stops[0]
                    }
                    Rectangle {
                        x: parent.width * .59
                        y: 42
                        width: parent.width * .23
                        height: 28
                        radius: card.style.radius * .25
                        color: card.base
                        border.color: card.palette.stops[2]
                    }
                    Rectangle {
                        x: parent.width * .59
                        y: 75
                        width: parent.width * .23
                        height: 28
                        radius: card.style.radius * .25
                        color: card.base
                        border.color: card.palette.stops[3]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    LookText {
                        text: card.modelData.name
                        size: 16
                        Layout.fillWidth: true
                    }
                    LookText {
                        text: card.modelData.builtIn ? qsTr("BUILT IN") : qsTr("SAVED")
                        kicker: true
                        font.pixelSize: 7
                    }
                }
                LookText {
                    Layout.fillWidth: true
                    text: card.modelData.description || qsTr("Your own colors, shape and spacing.")
                    muted: true
                    size: 10
                    lineHeight: 1.5
                }
                RowLayout {
                    Layout.fillWidth: true
                    LookText {
                        text: card.style.material === "light" ? qsTr("Paper") : card.style.material === "solid" ? qsTr("Solid") : qsTr("Glass")
                        size: 9
                        muted: true
                        Layout.fillWidth: true
                    }
                    LookText {
                        text: card.style.density === "compact" ? qsTr("Compact") : qsTr("Comfortable")
                        size: 9
                        muted: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ShellButton {
                        text: qsTr("Preview look")
                        highlighted: true
                        onClicked: root.dialogRequested("preset", card.modelData)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ShellButton {
                        visible: !card.modelData.builtIn
                        iconName: "document-export"
                        label: qsTr("Export %1").arg(card.modelData.name)
                        flat: true
                        onClicked: root.exportRequested(card.modelData.id)
                    }
                    ShellButton {
                        visible: !card.modelData.builtIn
                        iconName: "edit-delete"
                        label: qsTr("Delete %1").arg(card.modelData.name)
                        flat: true
                        onClicked: root.dialogRequested("delete", card.modelData)
                    }
                }
            }
        }
    }
    LookCard {
        Layout.fillWidth: true
        LookText {
            text: qsTr("Bring a look with you.")
            size: 14
        }
        LookText {
            text: qsTr("Import a Phosphor preset to inspect it before previewing. Your privacy and animation preferences stay yours.")
            muted: true
            size: 10
            Layout.fillWidth: true
        }
    }
}
