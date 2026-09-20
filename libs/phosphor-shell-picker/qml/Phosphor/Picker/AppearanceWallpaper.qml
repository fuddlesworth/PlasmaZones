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
    required property var controller
    property string filter: "All"
    property string query: ""
    readonly property var assignments: Appearance.settings.wallpapers
    readonly property var assignment: assignments[controller.selectedScreen] || assignments[""] || ({})
    readonly property var library: AppearanceLibrary.wallpapers
    readonly property var wallpaper: {
        library;
        return AppearanceLibrary.wallpaper(assignment.path || "");
    }
    readonly property var colors: wallpaper.colors || Appearance.settings.wallpaperColors
    readonly property var filtered: library.filter(w => (filter === "All" || w.collection === filter) && w.name.toLocaleLowerCase().includes(query.toLocaleLowerCase()))
    implicitHeight: Math.max(browser.implicitHeight, inspector.implicitHeight)
    function choose(path, fit) {
        AppearanceLibrary.chooseWallpaper(path, controller.linked ? "" : controller.selectedScreen, fit || assignment.fit || "fill");
    }
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 24
        ColumnLayout {
            id: browser
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 18
            LookScene {
                Layout.fillWidth: true
                wallpaper: root.wallpaper
                fit: root.assignment.fit || "fill"
                onPreviewRequested: root.controller.desktopPreview = true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 3
                Repeater {
                    model: [
                        {
                            value: "All",
                            label: qsTr("All")
                        },
                        {
                            value: "Abstract",
                            label: qsTr("Abstract")
                        },
                        {
                            value: "Nature",
                            label: qsTr("Nature")
                        },
                        {
                            value: "Added",
                            label: qsTr("Added")
                        }
                    ]
                    ShellButton {
                        required property var modelData
                        text: modelData.label
                        labelSize: 10
                        flat: root.filter !== modelData.value
                        highlighted: root.filter === modelData.value
                        onClicked: root.filter = modelData.value
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Basic.TextField {
                    Layout.preferredWidth: Math.min(140, browser.width * 0.26)
                    implicitHeight: 30
                    padding: 10
                    placeholderText: qsTr("Search")
                    Accessible.name: qsTr("Search wallpapers")
                    color: Appearance.text
                    placeholderTextColor: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((10) * Appearance.textScale)
                    selectByMouse: true
                    onTextChanged: root.query = text
                    background: Rectangle {
                        radius: 7
                        color: Qt.alpha(Appearance.card, .2)
                        border.color: parent.activeFocus ? Appearance.accent : Appearance.outline
                    }
                }
            }
            GridView {
                id: gallery
                Layout.fillWidth: true
                implicitHeight: Math.min(248, Math.max(124, Math.ceil(count / 4) * 124))
                cellWidth: width / 4
                cellHeight: 124
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.filtered
                Basic.ScrollBar.vertical: Basic.ScrollBar {
                    width: 5
                }
                delegate: Basic.AbstractButton {
                    id: tile
                    required property var modelData
                    width: gallery.cellWidth - 10
                    height: 116
                    Accessible.name: modelData.name
                    Accessible.selected: root.assignment.path === modelData.path
                    onClicked: root.choose(modelData.path)
                    background: Rectangle {
                        radius: 9
                        color: tile.hovered ? Qt.alpha(Appearance.text, .1) : Qt.alpha(Appearance.card, .4)
                        border.color: tile.visualFocus ? Appearance.text : root.assignment.path === tile.modelData.path ? Appearance.accent : "transparent"
                    }
                    contentItem: Item {
                        LookImage {
                            x: 5
                            y: 5
                            width: parent.width - 10
                            height: 78
                            radius: 5
                            path: tile.modelData.path
                            decodeWidth: 360
                        }
                        LookText {
                            x: 10
                            y: 92
                            width: parent.width - 58
                            size: 10
                            text: tile.modelData.name
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        LookText {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            y: 94
                            size: 8
                            muted: true
                            text: root.assignment.path === tile.modelData.path ? "✓" : tile.modelData.collection === "Nature" ? qsTr("Nature") : tile.modelData.collection === "Abstract" ? qsTr("Abstract") : ""
                        }
                    }
                }
                LookText {
                    anchors.centerIn: parent
                    visible: gallery.count === 0
                    text: qsTr("No wallpapers match your search.")
                    muted: true
                }
            }
        }
        LookCard {
            id: inspector
            body.spacing: 10
            Layout.preferredWidth: 236
            Layout.alignment: Qt.AlignTop
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 9
                Repeater {
                    model: root.controller.screens
                    ShellButton {
                        required property var modelData
                        required property int index
                        text: String(index + 1)
                        implicitWidth: 55
                        implicitHeight: 34
                        highlighted: root.controller.selectedScreen === modelData.value
                        outlined: true
                        Accessible.name: modelData.label
                        onClicked: root.controller.selectedScreen = modelData.value
                    }
                }
            }
            ColumnLayout {
                spacing: 5
                LookText {
                    text: qsTr("Make it fit")
                    size: 14
                }
                LookText {
                    text: qsTr("Wallpaper is set per display.")
                    size: 10
                    muted: true
                }
            }
            LookField {
                Layout.fillWidth: true
                label: qsTr("Display")
                choices: root.controller.screens
                value: root.controller.selectedScreen
                onChosen: value => root.controller.selectedScreen = value
            }
            LookToggle {
                Layout.fillWidth: true
                visible: root.controller.screens.length > 1
                label: qsTr("Use on all displays")
                description: qsTr("Keep the same wallpaper and fit.")
                checked: root.controller.linked
                onToggled: checked => {
                    root.controller.linked = checked;
                    if (checked && root.assignment.path)
                        root.choose(root.assignment.path);
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Appearance.outline
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                LookText {
                    text: qsTr("SELECTED WALLPAPER")
                    kicker: true
                }
                LookText {
                    text: root.wallpaper.name || qsTr("Current desktop")
                    size: 15
                    Layout.fillWidth: true
                }
                LookText {
                    text: root.wallpaper.size || qsTr("Choose an image to get started.")
                    muted: true
                    size: 9
                    Layout.fillWidth: true
                }
            }
            LookField {
                Layout.fillWidth: true
                label: qsTr("Image placement")
                value: root.assignment.fit || "fill"
                choices: [
                    {
                        value: "fill",
                        label: qsTr("Fill screen")
                    },
                    {
                        value: "fit",
                        label: qsTr("Fit image")
                    },
                    {
                        value: "stretch",
                        label: qsTr("Stretch")
                    },
                    {
                        value: "center",
                        label: qsTr("Center")
                    }
                ]
                onChosen: value => {
                    if (root.assignment.path)
                        root.choose(root.assignment.path, value);
                }
            }
            LookImage {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 63
                implicitHeight: 36
                path: root.assignment.path || ""
                fit: root.assignment.fit || "fill"
                decodeWidth: 126
                radius: 3
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Appearance.outline
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                LookText {
                    text: qsTr("Colors from this wallpaper")
                    muted: true
                    size: 9
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: root.colors
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 21
                            radius: 4
                            color: modelData
                        }
                    }
                }
            }
            LookToggle {
                Layout.fillWidth: true
                label: qsTr("Follow wallpaper")
                description: qsTr("Retint the shell when it changes.")
                checked: Appearance.settings.palette === "wallpaper"
                onToggled: checked => {
                    AppearanceStore.setValue("palette", checked ? "wallpaper" : "spectrum");
                    if (checked && root.assignment.path)
                        root.choose(root.assignment.path);
                }
            }
            ShellButton {
                text: qsTr("Fine-tune colors  →")
                flat: true
                labelSize: 10
                onClicked: root.controller.page = "style"
            }
        }
    }
}
