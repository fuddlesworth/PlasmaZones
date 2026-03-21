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

        // Enable/disable toggle (matching KCM header placement)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

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

        // App header with icon and version
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

        // Description
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: i18n("A window tiling and zone management tool for Wayland compositors. Organize your desktop with customizable zones, automatic tiling layouts, and keyboard-driven window placement.")
            wrapMode: Text.WordWrap
        }

        // Links card
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

                    LinkButton {
                        linkText: i18n("GitHub Repository")
                        linkIcon: "vcs-branch"
                        url: "https://github.com/fuddlesworth/PlasmaZones"
                    }

                    LinkButton {
                        linkText: i18n("Report a Bug")
                        linkIcon: "tools-report-bug"
                        url: "https://github.com/fuddlesworth/PlasmaZones/issues/new"
                    }

                    LinkButton {
                        linkText: i18n("Documentation / Wiki")
                        linkIcon: "documentation"
                        url: "https://github.com/fuddlesworth/PlasmaZones/wiki"
                    }

                    LinkButton {
                        linkText: i18n("Releases")
                        linkIcon: "package-available"
                        url: "https://github.com/fuddlesworth/PlasmaZones/releases"
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

                    LinkButton {
                        linkText: i18n("View License")
                        linkIcon: "license"
                        url: "https://www.gnu.org/licenses/gpl-3.0.html"
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

    // Helper component for link buttons (matching original KCM)
    component LinkButton: Button {
        id: linkButton

        required property string linkText
        required property string linkIcon
        required property string url

        Layout.fillWidth: true
        flat: true
        horizontalPadding: Kirigami.Units.largeSpacing
        Accessible.name: linkText
        Accessible.role: Accessible.Link
        Accessible.description: i18n("Opens %1 in web browser", url)
        onClicked: Qt.openUrlExternally(linkButton.url)

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: linkButton.linkIcon
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            Label {
                text: linkButton.linkText
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
