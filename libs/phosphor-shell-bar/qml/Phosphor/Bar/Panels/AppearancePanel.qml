// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Phosphor.Theme
import Phosphor.Widgets

PanelFrame {
    id: root
    title: qsTr("Appearance")
    subtitle: qsTr("Make Phosphor yours")
    iconName: "configure"
    panelWidth: 400
    maxBodyHeight: 560

    RowLayout {
        width: parent ? parent.width : 0
        ShellButton {
            text: qsTr("Phosphor")
            Layout.fillWidth: true
            onClicked: AppearanceStore.applyPreset("phosphor")
        }
        ShellButton {
            text: qsTr("Paper")
            Layout.fillWidth: true
            onClicked: AppearanceStore.applyPreset("paper")
        }
        ShellButton {
            text: qsTr("Ember")
            Layout.fillWidth: true
            onClicked: AppearanceStore.applyPreset("ember")
        }
    }
    Item {
        height: 12
        width: 1
    }
    Repeater {
        model: [
            {
                key: "presentation",
                label: qsTr("Overview"),
                names: [qsTr("Navigator"), qsTr("Stage")],
                values: ["navigator", "stage"]
            },
            {
                key: "palette",
                label: qsTr("Palette"),
                names: [qsTr("Spectrum"), qsTr("Wallpaper"), qsTr("Ember")],
                values: ["spectrum", "wallpaper", "ember"]
            },
            {
                key: "material",
                label: qsTr("Material"),
                names: [qsTr("Glass"), qsTr("Solid"), qsTr("Light")],
                values: ["glass", "solid", "light"]
            },
            {
                key: "density",
                label: qsTr("Density"),
                names: [qsTr("Comfortable"), qsTr("Compact")],
                values: ["comfortable", "compact"]
            },
            {
                key: "edge",
                label: qsTr("Bar position"),
                names: [qsTr("Top"), qsTr("Bottom")],
                values: ["top", "bottom"]
            },
            {
                key: "visualizer",
                label: qsTr("Visualizer"),
                names: [qsTr("Ribbon"), qsTr("Bars"), qsTr("Halo"), qsTr("Off")],
                values: ["ribbon", "bars", "halo", "off"]
            }
        ]
        delegate: RowLayout {
            required property var modelData
            width: parent ? parent.width : 0
            Text {
                Layout.fillWidth: true
                text: modelData.label
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: 12
            }
            Basic.ComboBox {
                id: choice
                implicitHeight: 34
                background: Rectangle {
                    radius: Math.min(8, Appearance.radius)
                    color: Appearance.card
                    border.width: 1
                    border.color: choice.visualFocus ? Appearance.text : Appearance.outline
                }
                indicator: Text {
                    x: choice.width - width - 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌄"
                    color: Appearance.muted
                    font.pixelSize: 18
                }
                palette.button: Appearance.card
                palette.buttonText: Appearance.text
                palette.base: Appearance.surface
                palette.text: Appearance.text
                palette.highlight: Appearance.accent
                palette.highlightedText: Appearance.text
                palette.window: Appearance.surface
                Layout.preferredWidth: 170
                model: modelData.names
                currentIndex: modelData.values.indexOf(Appearance.settings[modelData.key])
                Accessible.name: modelData.label
                onActivated: AppearanceStore.setValue(modelData.key, modelData.values[currentIndex])
            }
        }
    }
    Repeater {
        model: [
            {
                key: "radius",
                label: qsTr("Corner radius"),
                minimum: 4
            },
            {
                key: "gap",
                label: qsTr("Bar inset"),
                minimum: 6
            }
        ]
        delegate: RowLayout {
            required property var modelData
            width: parent ? parent.width : 0
            Text {
                Layout.fillWidth: true
                text: modelData.label
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: 12
            }
            Basic.Slider {
                id: slider
                implicitHeight: 32
                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: slider.availableWidth
                    height: 6
                    radius: 3
                    color: Appearance.card
                    Rectangle {
                        width: parent.width * slider.visualPosition
                        height: parent.height
                        radius: 3
                        color: Appearance.accent
                    }
                }
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: 16
                    height: 16
                    radius: 8
                    color: Appearance.text
                    border.width: slider.visualFocus ? 2 : 0
                    border.color: Appearance.accent
                }
                Layout.preferredWidth: 138
                from: modelData.minimum
                to: 30
                stepSize: 1
                value: Appearance.settings[modelData.key]
                Accessible.name: modelData.label
                onMoved: if (!pressed)
                    AppearanceStore.setValue(modelData.key, Math.round(value))
                onPressedChanged: if (!pressed)
                    AppearanceStore.setValue(modelData.key, Math.round(value))
            }
            Text {
                text: Math.round(slider.value)
                color: Appearance.muted
                font.family: Tokens.font_family_mono
                font.pixelSize: 12
            }
        }
    }
    Repeater {
        model: [
            {
                key: "glow",
                label: qsTr("Surface glow")
            },
            {
                key: "motion",
                label: qsTr("Animations")
            },
            {
                key: "media",
                label: qsTr("Show media")
            },
            {
                key: "surfacePacks",
                label: qsTr("Custom surface packs")
            }
        ]
        delegate: Basic.Switch {
            id: toggle
            implicitHeight: 30
            indicator: Rectangle {
                x: toggle.leftPadding
                y: toggle.topPadding + toggle.availableHeight / 2 - height / 2
                width: 34
                height: 20
                radius: 10
                color: toggle.checked ? Appearance.accent : Appearance.card
                border.width: 1
                border.color: toggle.visualFocus ? Appearance.text : Appearance.outline
                Rectangle {
                    x: toggle.checked ? 16 : 2
                    y: 2
                    width: 16
                    height: 16
                    radius: 8
                    color: Appearance.light ? "#ffffff" : Appearance.text
                }
            }
            required property var modelData
            width: parent ? parent.width : 0
            text: modelData.label
            checked: Appearance.settings[modelData.key]
            onToggled: AppearanceStore.setValue(modelData.key, checked)
            palette.windowText: Appearance.text
            palette.buttonText: Appearance.text
            palette.text: Appearance.text
            palette.highlight: Appearance.accent
        }
    }
    Text {
        width: parent ? parent.width : 0
        text: qsTr("Changing bar position, density or inset reloads the shell.")
        color: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        topPadding: 6
        bottomPadding: 10
    }
    RowLayout {
        width: parent ? parent.width : 0
        ShellButton {
            text: qsTr("Import preset")
            Layout.fillWidth: true
            onClicked: importDialog.open()
        }
        ShellButton {
            text: qsTr("Export preset")
            Layout.fillWidth: true
            onClicked: exportDialog.open()
        }
    }
    Text {
        width: parent ? parent.width : 0
        visible: AppearanceStore.error !== ""
        text: AppearanceStore.error
        color: Appearance.at(1)
        wrapMode: Text.WordWrap
        font.pixelSize: 12
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import appearance")
        nameFilters: [qsTr("JSON presets (*.json)")]
        onAccepted: AppearanceStore.importPreset(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export appearance")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON presets (*.json)")]
        onAccepted: AppearanceStore.exportPreset(selectedFile)
    }
}
