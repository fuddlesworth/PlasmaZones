// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief About tab - Version info, links, license, and credits
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Update available banner
        Kirigami.InlineMessage {
            id: updateBanner
            Layout.fillWidth: true
            visible: kcm.updateAvailable && kcm.latestVersion !== kcm.dismissedUpdateVersion
            type: Kirigami.MessageType.Information
            text: i18n("A new version is available: %1", kcm.latestVersion)
            actions: [
                Kirigami.Action {
                    text: i18n("View Release")
                    icon.name: "internet-web-browser"
                    onTriggered: kcm.openReleaseUrl()
                },
                Kirigami.Action {
                    text: i18n("Dismiss")
                    icon.name: "dialog-close"
                    onTriggered: kcm.dismissedUpdateVersion = kcm.latestVersion
                }
            ]
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
                    text: "PlasmaZones"
                }

                Label {
                    text: i18n("Version %1", kcm.currentVersion)
                    opacity: 0.7
                }
            }
        }

        // Description
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: i18n("A window tiling and zone management tool for KDE Plasma, inspired by Windows PowerToys FancyZones. Organize your desktop with customizable zones for efficient window placement.")
            wrapMode: Text.WordWrap
        }

        // Links card
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing

            header: Kirigami.Heading {
                level: 3
                text: i18n("Links")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                LinkButton {
                    text: i18n("GitHub Repository")
                    icon.name: "vcs-branch"
                    url: "https://github.com/fuddlesworth/PlasmaZones"
                }

                LinkButton {
                    text: i18n("Report a Bug")
                    icon.name: "tools-report-bug"
                    url: "https://github.com/fuddlesworth/PlasmaZones/issues/new"
                }

                LinkButton {
                    text: i18n("Documentation / Wiki")
                    icon.name: "documentation"
                    url: "https://github.com/fuddlesworth/PlasmaZones/wiki"
                }

                LinkButton {
                    text: i18n("Releases")
                    icon.name: "package-available"
                    url: "https://github.com/fuddlesworth/PlasmaZones/releases"
                }
            }
        }

        // License card
        Kirigami.Card {
            Layout.fillWidth: true

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
                    text: i18n("View License")
                    icon.name: "license"
                    url: "https://www.gnu.org/licenses/gpl-3.0.html"
                }
            }
        }

        // Credits card
        Kirigami.Card {
            Layout.fillWidth: true

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
                    text: i18n("Inspired by Microsoft PowerToys FancyZones")
                    opacity: 0.7
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Built with Qt, KDE Frameworks, and Kirigami")
                    opacity: 0.7
                }
            }
        }

        // Check for updates button
        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: kcm.checkingForUpdates ? i18n("Checking...") : i18n("Check for Updates")
            icon.name: "view-refresh"
            enabled: !kcm.checkingForUpdates
            onClicked: kcm.checkForUpdates()
        }

        // Spacer
        Item {
            Layout.fillHeight: true
        }
    }

    // Helper component for link buttons
    component LinkButton: Button {
        required property string url
        Layout.fillWidth: true
        flat: true
        horizontalPadding: Kirigami.Units.largeSpacing
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: parent.parent.icon.name
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            Label {
                text: parent.parent.text
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
        onClicked: Qt.openUrlExternally(url)
    }
}
