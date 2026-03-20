// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width - Kirigami.Units.largeSpacing * 2
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Kirigami.Units.largeSpacing

        // App header with icon and version (matches KCM)
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                source: "plasmazones"
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    level: 1
                    text: i18n("PlasmaZones")
                }

                Label {
                    text: Qt.application.version.length > 0 ? i18n("Version %1", Qt.application.version) : i18n("Version unknown")
                    opacity: 0.7
                }

            }

        }

        // Description (matches KCM — left-aligned, full text)
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: i18n("A window tiling and zone management tool for Wayland compositors. Organize your desktop with customizable zones, automatic tiling layouts, and keyboard-driven window placement.")
            wrapMode: Text.WordWrap
        }

        // Enable/disable section (matches KCM header pattern)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing

                Label {
                    text: i18n("Enable PlasmaZones")
                    font.bold: true
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                    opacity: 0.7
                }

                Switch {
                    checked: settingsController.daemonRunning
                    onToggled: {
                        if (checked)
                            settingsController.daemonController.startDaemon();
                        else
                            settingsController.daemonController.stopDaemon();
                    }
                    Accessible.name: i18n("Enable PlasmaZones")
                }

            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

        }

        // Links card (matches KCM LinkButton pattern)
        Item {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            implicitHeight: linksCard.implicitHeight

            Kirigami.Card {
                id: linksCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Links")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // LinkButton pattern from KCM: icon + text + arrow, left-aligned
                    Repeater {
                        model: [{
                            "text": i18n("GitHub Repository"),
                            "icon": "vcs-branch",
                            "url": "https://github.com/fuddlesworth/PlasmaZones"
                        }, {
                            "text": i18n("Report a Bug"),
                            "icon": "tools-report-bug",
                            "url": "https://github.com/fuddlesworth/PlasmaZones/issues/new"
                        }, {
                            "text": i18n("Documentation / Wiki"),
                            "icon": "documentation",
                            "url": "https://github.com/fuddlesworth/PlasmaZones/wiki"
                        }, {
                            "text": i18n("Releases"),
                            "icon": "package-available",
                            "url": "https://github.com/fuddlesworth/PlasmaZones/releases"
                        }]

                        Button {
                            Layout.fillWidth: true
                            flat: true
                            horizontalPadding: Kirigami.Units.largeSpacing
                            Accessible.name: modelData.text
                            Accessible.role: Accessible.Link
                            onClicked: Qt.openUrlExternally(modelData.url)

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: modelData.icon
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                }

                                Label {
                                    text: modelData.text
                                    Layout.fillWidth: true
                                    color: Kirigami.Theme.linkColor
                                }

                                Kirigami.Icon {
                                    source: "arrow-right"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    opacity: 0.5
                                }

                            }

                        }

                    }

                }

            }

        }

        // License card
        Item {
            Layout.fillWidth: true
            implicitHeight: licenseCard.implicitHeight

            Kirigami.Card {
                id: licenseCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("License")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.fillWidth: true
                        text: i18n("PlasmaZones is free software licensed under the GNU General Public License version 3 or later (GPL-3.0-or-later).")
                        wrapMode: Text.WordWrap
                    }

                    Button {
                        flat: true
                        horizontalPadding: Kirigami.Units.largeSpacing
                        onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/gpl-3.0.html")

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "text-x-copying"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }

                            Label {
                                text: i18n("View License")
                                color: Kirigami.Theme.linkColor
                            }

                            Kirigami.Icon {
                                source: "arrow-right"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.5
                            }

                        }

                    }

                }

            }

        }

        // Credits card
        Item {
            Layout.fillWidth: true
            implicitHeight: creditsCard.implicitHeight

            Kirigami.Card {
                id: creditsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Credits")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.fillWidth: true
                        text: i18n("Created by fuddlesworth")
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: i18n("Inspired by FancyZones, extended with automatic tiling")
                        opacity: 0.7
                    }

                    Label {
                        Layout.fillWidth: true
                        text: i18n("Built with Qt, KDE Frameworks, and Kirigami")
                        opacity: 0.7
                    }

                }

            }

        }

        Item {
            Layout.fillHeight: true
        }

    }

}
