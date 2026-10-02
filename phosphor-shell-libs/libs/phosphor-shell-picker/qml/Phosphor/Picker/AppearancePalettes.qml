// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property string query: ""
    property string filter: "all"
    readonly property var entries: presets.presets.filter(entry => presets.isCompletePalette(entry.tokens) && (filter === "all" || (filter === "included") === entry.builtIn) && entry.name.toLocaleLowerCase().includes(query.toLocaleLowerCase()))
    implicitHeight: content.implicitHeight

    ThemePresets {
        id: presets
        Component.onCompleted: rescan()
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                LookText {
                    text: qsTr("Color palettes")
                    size: 22
                    font.weight: Font.Medium
                }
                LookText {
                    Layout.fillWidth: true
                    text: qsTr("Choose a palette to preview it across the shell. Apply changes when you are ready.")
                    size: 10
                    muted: true
                }
            }
            ShellButton {
                text: qsTr("Refresh")
                iconName: "view-refresh"
                outlined: true
                flat: true
                onClicked: presets.rescan()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    {
                        id: "all",
                        label: qsTr("All")
                    },
                    {
                        id: "included",
                        label: qsTr("Included")
                    },
                    {
                        id: "added",
                        label: qsTr("Added")
                    }
                ]
                ShellButton {
                    required property var modelData
                    text: modelData.label
                    flat: root.filter !== modelData.id
                    highlighted: root.filter === modelData.id
                    onClicked: root.filter = modelData.id
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Basic.TextField {
                id: search
                Layout.preferredWidth: Math.min(220, Math.max(110, root.width * .28))
                implicitHeight: 34
                padding: 9
                placeholderText: qsTr("Search palettes")
                Accessible.name: qsTr("Search palettes")
                color: Appearance.text
                placeholderTextColor: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round(11 * Appearance.textScale)
                selectByMouse: true
                onTextChanged: root.query = text
                background: Rectangle {
                    radius: 7
                    color: Qt.alpha(Appearance.card, .28)
                    border.color: search.activeFocus ? Appearance.accent : Appearance.outline
                }
            }
        }

        LookCard {
            Layout.fillWidth: true
            visible: Appearance.settings.palette === "library"
            RowLayout {
                Layout.fillWidth: true
                LookText {
                    Layout.fillWidth: true
                    text: qsTr("Current palette: %1").arg(Appearance.settings.libraryPaletteName)
                    size: 12
                }
                Repeater {
                    model: Appearance.stops
                    Rectangle {
                        required property var modelData
                        implicitWidth: 23
                        implicitHeight: 23
                        radius: 5
                        color: modelData
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width < 520 ? 1 : root.width < 850 ? 2 : 3
            columnSpacing: 14
            rowSpacing: 14
            Repeater {
                model: root.entries
                Basic.AbstractButton {
                    id: tile
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 122
                    readonly property bool selected: Appearance.settings.palette === "library" && Appearance.settings.libraryPaletteName === modelData.name
                    Accessible.name: qsTr("Preview %1 palette").arg(modelData.name)
                    Accessible.selected: selected
                    onClicked: AppearanceStore.setLibraryPalette(modelData.name, modelData.tokens)
                    background: Rectangle {
                        radius: Math.max(8, Appearance.radius * .55)
                        color: tile.selected ? Qt.alpha(Appearance.accent, .13) : Qt.alpha(Appearance.card, .28)
                        border.color: tile.visualFocus ? Appearance.text : tile.selected ? Appearance.accent : Appearance.outline
                    }
                    contentItem: Item {
                        Row {
                            x: 12
                            y: 12
                            width: parent.width - 24
                            height: 54
                            clip: true
                            Repeater {
                                model: tile.modelData.swatches
                                Rectangle {
                                    required property var modelData
                                    width: (parent.width || 0) / 5
                                    height: 54
                                    color: modelData
                                }
                            }
                        }
                        LookText {
                            x: 12
                            y: 76
                            width: parent.width - 24
                            text: tile.modelData.name
                            size: 12
                            font.weight: Font.Medium
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        LookText {
                            x: 12
                            y: 98
                            text: tile.selected ? qsTr("Previewing") : tile.modelData.builtIn ? qsTr("Included") : qsTr("Added")
                            size: 9
                            muted: true
                        }
                    }
                }
            }
        }

        LookText {
            Layout.fillWidth: true
            visible: root.entries.length === 0
            text: qsTr("No palettes match your search.")
            size: 11
            muted: true
        }

        LookCard {
            Layout.fillWidth: true
            LookText {
                Layout.fillWidth: true
                text: qsTr("Add your own palettes")
                size: 14
            }
            LookText {
                Layout.fillWidth: true
                text: qsTr("Place palette JSON files in %1, then select Refresh.").arg(presets.directory)
                size: 10
                muted: true
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
