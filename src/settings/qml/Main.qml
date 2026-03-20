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

        // Order matches KCM X-KDE-Weight values
        ListElement {
            name: "about"
            label: "About"
        }

        ListElement {
            name: "layouts"
            label: "Layouts"
        }

        ListElement {
            name: "editor"
            label: "Editor"
        }

        ListElement {
            name: "assignments"
            label: "Assignments"
        }

        ListElement {
            name: "snapping"
            label: "Snapping"
        }

        ListElement {
            name: "autotiling"
            label: "Autotiling"
        }

        ListElement {
            name: "general"
            label: "General"
        }

        ListElement {
            name: "exclusions"
            label: "Exclusions"
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
                anchors.fill: parent
                spacing: 0

                Kirigami.Heading {
                    level: 2
                    text: "PlasmaZones"
                    leftPadding: Kirigami.Units.largeSpacing
                    topPadding: Kirigami.Units.largeSpacing
                    bottomPadding: Kirigami.Units.largeSpacing
                    Layout.fillWidth: true
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
                        text: model.label
                        highlighted: ListView.isCurrentItem
                        onClicked: settingsController.activePage = model.name
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
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
                AboutPage {
                }

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

            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Defaults")
                        icon.name: "edit-undo"
                        onClicked: settingsController.defaults()
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
