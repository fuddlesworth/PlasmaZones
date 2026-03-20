// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // --- Toolbar ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("Create New")
                icon.name: "list-add"
                onClicked: settingsController.createNewLayout()
            }

            Button {
                text: i18n("Open Layouts Folder")
                icon.name: "folder-open"
                onClicked: settingsController.openLayoutsFolder()
            }

            Button {
                text: i18n("Launch Editor")
                icon.name: "document-edit"
                onClicked: settingsController.launchEditor()
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: i18n("%1 layout(s)", layoutRepeater.count)
                opacity: 0.7
            }

        }

        // --- Layout List ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Layouts")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: 0

                // Empty state
                Label {
                    visible: settingsController.layouts.length === 0
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.gridUnit
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: i18n("No layouts found. Start the PlasmaZones daemon or create a new layout.")
                    opacity: 0.7
                }

                Repeater {
                    id: layoutRepeater

                    model: settingsController.layouts

                    delegate: ItemDelegate {
                        id: layoutDelegate

                        required property var modelData
                        required property int index
                        readonly property string layoutId: modelData.id || ""
                        readonly property string layoutName: modelData.name || i18n("Untitled")
                        readonly property int zoneCount: modelData.zoneCount || 0
                        readonly property bool isDefault: layoutId === kcm.defaultLayoutId
                        readonly property bool isSystem: modelData.isSystem === true
                        readonly property bool isAutotile: modelData.isAutotile === true

                        Layout.fillWidth: true
                        width: parent ? parent.width : 0

                        // Separator between items
                        Kirigami.Separator {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            visible: layoutDelegate.index < layoutRepeater.count - 1
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            // Default indicator
                            Kirigami.Icon {
                                source: "starred-symbolic"
                                visible: layoutDelegate.isDefault
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                color: Kirigami.Theme.positiveTextColor
                            }

                            // Autotile indicator
                            Kirigami.Icon {
                                source: "view-grid-symbolic"
                                visible: layoutDelegate.isAutotile
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: layoutDelegate.layoutName
                                    font.bold: layoutDelegate.isDefault
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        let parts = [];
                                        if (layoutDelegate.zoneCount > 0)
                                            parts.push(i18n("%1 zone(s)", layoutDelegate.zoneCount));

                                        if (layoutDelegate.isSystem)
                                            parts.push(i18n("Built-in"));

                                        if (layoutDelegate.isDefault)
                                            parts.push(i18n("Default"));

                                        if (layoutDelegate.isAutotile)
                                            parts.push(i18n("Autotile"));

                                        return parts.join(" \u00b7 ");
                                    }
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                    elide: Text.ElideRight
                                }

                            }

                            // Actions
                            Button {
                                text: i18n("Edit")
                                icon.name: "document-edit"
                                flat: true
                                visible: !layoutDelegate.isAutotile
                                onClicked: settingsController.editLayout(layoutDelegate.layoutId)
                            }

                            Button {
                                icon.name: "edit-copy"
                                flat: true
                                visible: !layoutDelegate.isAutotile
                                ToolTip.text: i18n("Duplicate")
                                ToolTip.visible: hovered
                                onClicked: settingsController.duplicateLayout(layoutDelegate.layoutId)
                            }

                            Button {
                                icon.name: "edit-delete"
                                flat: true
                                visible: !layoutDelegate.isSystem && !layoutDelegate.isAutotile
                                ToolTip.text: i18n("Delete")
                                ToolTip.visible: hovered
                                onClicked: {
                                    deleteConfirmDialog.layoutId = layoutDelegate.layoutId;
                                    deleteConfirmDialog.layoutName = layoutDelegate.layoutName;
                                    deleteConfirmDialog.open();
                                }
                            }

                        }

                    }

                }

            }

        }

        // --- Default Layout ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Default Layout")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Current default:")
                    }

                    Label {
                        text: kcm.defaultLayoutId || i18n("(none)")
                        font.bold: true
                    }

                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("The default layout is applied to any screen or virtual desktop that does not have a specific assignment. Change it in KDE System Settings or edit the config file directly.")
                    opacity: 0.7
                }

            }

        }

        // --- Storage Info ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Storage")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Layout files:")
                    }

                    Label {
                        text: "~/.local/share/plasmazones/layouts/"
                        font.family: "monospace"
                        opacity: 0.7
                    }

                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Layouts are stored as individual JSON files. You can import layouts by placing .json files in this directory and restarting the daemon.")
                    opacity: 0.7
                }

            }

        }

    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog

        property string layoutId: ""
        property string layoutName: ""

        anchors.centerIn: parent
        title: i18n("Delete Layout")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            settingsController.deleteLayout(layoutId);
            layoutId = "";
            layoutName = "";
        }
        onRejected: {
            layoutId = "";
            layoutName = "";
        }

        Label {
            text: i18n("Are you sure you want to delete \"%1\"?", deleteConfirmDialog.layoutName)
            wrapMode: Text.WordWrap
        }

    }

}
