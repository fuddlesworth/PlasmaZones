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

        // Update available banner (only show if we have a valid latest version)
        Kirigami.InlineMessage {
            id: updateBanner
            Layout.fillWidth: true
            visible: kcm.updateAvailable
                     && kcm.latestVersion.length > 0
                     && kcm.latestVersion !== kcm.dismissedUpdateVersion
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
                    text: i18n("PlasmaZones")
                }

                Label {
                    text: kcm.currentVersion.length > 0
                          ? i18n("Version %1", kcm.currentVersion)
                          : i18n("Version unknown")
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
                    linkText: i18n("View License")
                    linkIcon: "license"
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

        // Check for updates section - use Item to prevent layout shift
        Item {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            implicitHeight: checkUpdateButton.height

            Button {
                id: checkUpdateButton
                anchors.horizontalCenter: parent.horizontalCenter
                text: kcm.checkingForUpdates ? i18n("Checking...") : i18n("Check for Updates")
                icon.name: "view-refresh"
                enabled: !kcm.checkingForUpdates
                onClicked: {
                    updateStatusLabel.manualCheck = true
                    updateStatusLabel.visible = false
                    kcm.checkForUpdates()
                    // Show result after short delay (handles rate-limited case where
                    // checkingForUpdates never becomes true because check was skipped)
                    statusShowTimer.restart()
                }

                Accessible.name: text
                Accessible.role: Accessible.Button
            }

            // Status message - anchored to right of button, doesn't affect button position
            Label {
                id: updateStatusLabel
                anchors.left: checkUpdateButton.right
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: checkUpdateButton.verticalCenter
                visible: false

                property bool manualCheck: false

                // Track when checking finishes to show result immediately
                Connections {
                    target: kcm
                    function onCheckingForUpdatesChanged() {
                        // When checking completes (transitions from true to false)
                        if (!kcm.checkingForUpdates && updateStatusLabel.manualCheck) {
                            statusShowTimer.stop()
                            updateStatusLabel.showResult()
                        }
                    }
                }

                function showResult() {
                    updateStatusLabel.visible = true
                    updateStatusLabel.manualCheck = false
                    statusHideTimer.restart()
                }

                text: {
                    if (kcm.updateAvailable) {
                        return i18n("Update available!")
                    } else if (kcm.latestVersion.length > 0) {
                        return i18n("You're up to date.")
                    } else {
                        return i18n("Could not check for updates.")
                    }
                }

                color: {
                    if (kcm.updateAvailable) {
                        return Kirigami.Theme.positiveTextColor
                    } else if (kcm.latestVersion.length > 0) {
                        return Kirigami.Theme.textColor
                    } else {
                        return Kirigami.Theme.neutralTextColor
                    }
                }
            }

            // Short delay to show result (handles rate-limited case)
            Timer {
                id: statusShowTimer
                interval: 500
                onTriggered: {
                    if (updateStatusLabel.manualCheck && !kcm.checkingForUpdates) {
                        updateStatusLabel.showResult()
                    }
                }
            }

            Timer {
                id: statusHideTimer
                interval: 5000
                onTriggered: updateStatusLabel.visible = false
            }
        }

        // Spacer
        Item {
            Layout.fillHeight: true
        }
    }

    // Helper component for link buttons
    // Uses explicit properties instead of fragile parent.parent references
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

        onClicked: Qt.openUrlExternally(linkButton.url)
    }
}
