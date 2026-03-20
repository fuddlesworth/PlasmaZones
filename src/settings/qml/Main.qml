// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: window

    title: i18n("PlasmaZones Settings")
    width: 900
    height: 650
    visible: true

    ListModel {
        id: pageModel

        // Order and names match KCM X-KDE-Weight + metadata
        ListElement {
            name: "layouts"
            label: "Layouts"
            icon: "view-grid"
        }

        ListElement {
            name: "editor"
            label: "Editor"
            icon: "document-edit"
        }

        ListElement {
            name: "assignments"
            label: "Assignments"
            icon: "view-list-details"
        }

        ListElement {
            name: "snapping"
            label: "Snapping"
            icon: "view-split-left-right"
        }

        ListElement {
            name: "autotiling"
            label: "Tiling"
            icon: "window-duplicate"
        }

        ListElement {
            name: "general"
            label: "General"
            icon: "configure"
        }

        ListElement {
            name: "exclusions"
            label: "Exclusions"
            icon: "dialog-cancel"
        }

        ListElement {
            name: "about"
            label: "About"
            icon: "help-about"
        }

    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillHeight: true
            Layout.preferredWidth: 170
            padding: 0

            ColumnLayout {
                // Daemon status is shown in the header toggle above

                anchors.fill: parent
                spacing: 0

                // Header: title
                Label {
                    text: "PlasmaZones"
                    font.bold: true
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                ListView {
                    id: sidebar

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: pageModel
                    currentIndex: {
                        for (var i = 0; i < pageModel.count; i++) {
                            if (pageModel.get(i).name === settingsController.activePage)
                                return i;

                        }
                        return 0;
                    }

                    delegate: ItemDelegate {
                        width: sidebar.width
                        highlighted: ListView.isCurrentItem
                        onClicked: settingsController.activePage = model.name

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: model.icon
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }

                            Label {
                                text: model.label
                                Layout.fillWidth: true
                            }

                        }

                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                    }

                    Label {
                        text: settingsController.daemonRunning ? i18n("Daemon running") : i18n("Daemon stopped")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                        Layout.fillWidth: true
                    }

                    Switch {
                        checked: settingsController.daemonRunning
                        scale: 0.8
                        onToggled: {
                            if (checked)
                                settingsController.daemonController.startDaemon();
                            else
                                settingsController.daemonController.stopDaemon();
                        }
                    }

                }

            }

        }

        Kirigami.Separator {
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: sidebar.currentIndex

                // Order must match pageModel
                LayoutsPage {
                }

                EditorPage {
                }

                AssignmentsPage {
                }

                SnappingPage {
                }

                AutotilingPage {
                }

                GeneralPage {
                }

                ExclusionsPage {
                }

                AboutPage {
                }

            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent

                    Button {
                        text: i18n("Reset")
                        icon.name: "edit-undo"
                        enabled: settingsController.needsSave
                        onClicked: settingsController.load()
                    }

                    Button {
                        text: i18n("Defaults")
                        icon.name: "document-revert"
                        onClicked: settingsController.defaults()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Apply")
                        icon.name: "dialog-ok-apply"
                        enabled: settingsController.needsSave
                        highlighted: settingsController.needsSave
                        onClicked: settingsController.save()
                    }

                }

            }

        }

    }

}
