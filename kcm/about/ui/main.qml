// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

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
                    text: kcm.currentVersion.length > 0 ? i18n("Version %1", kcm.currentVersion) : i18n("Version unknown")
                    opacity: 0.7
                }

            }

        }

        // Description
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: i18n("A window tiling and zone management tool for KDE Plasma. Organize your desktop with customizable zones, automatic tiling layouts, and keyboard-driven window placement.")
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

        // Check for updates
        Item {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            implicitHeight: updateCheckColumn.implicitHeight

            ColumnLayout {
                id: updateCheckColumn

                anchors.fill: parent
                spacing: Kirigami.Units.smallSpacing

                Button {
                    id: checkUpdateButton

                    Layout.alignment: Qt.AlignHCenter
                    text: kcm.checkingForUpdates ? i18n("Checking...") : i18n("Check for Updates")
                    icon.name: "view-refresh"
                    enabled: !kcm.checkingForUpdates
                    Accessible.name: text
                    Accessible.role: Accessible.Button
                    onClicked: {
                        updateResultMessage.pendingResult = true;
                        updateResultMessage.visible = false;
                        statusHideTimer.stop();
                        kcm.checkForUpdates();
                        statusShowTimer.restart();
                    }
                }

                Kirigami.InlineMessage {
                    id: updateResultMessage

                    property bool pendingResult: false
                    readonly property int resultState: kcm.updateAvailable ? 2 : kcm.latestVersion.length > 0 ? 1 : 0

                    function showResult() {
                        if (!pendingResult)
                            return ;

                        pendingResult = false;
                        visible = true;
                        if (kcm.updateAvailable) {
                            if (kcm.latestVersion.length > 0 && kcm.dismissedUpdateVersion !== kcm.latestVersion)
                                kcm.dismissedUpdateVersion = "";

                        } else {
                            statusHideTimer.restart();
                        }
                    }

                    Layout.fillWidth: true
                    visible: false
                    type: resultState === 2 ? Kirigami.MessageType.Positive : resultState === 1 ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
                    text: resultState === 2 ? i18n("Version %1 is available! You are currently on %2.", kcm.latestVersion, kcm.currentVersion.length > 0 ? kcm.currentVersion : i18n("unknown")) : resultState === 1 ? i18n("You're up to date (%1).", kcm.currentVersion.length > 0 ? kcm.currentVersion : i18n("unknown")) : i18n("Could not check for updates. Please try again later.")
                    actions: resultState === 2 ? [viewReleaseAction, dismissAction] : []
                    Accessible.name: text
                    Accessible.description: i18n("Update check result")

                    Kirigami.Action {
                        id: viewReleaseAction

                        text: i18n("View Release")
                        icon.name: "internet-web-browser"
                        onTriggered: kcm.openReleaseUrl()
                    }

                    Kirigami.Action {
                        id: dismissAction

                        text: i18n("Dismiss")
                        icon.name: "dialog-close"
                        onTriggered: {
                            statusHideTimer.stop();
                            if (kcm.latestVersion.length > 0)
                                kcm.dismissedUpdateVersion = kcm.latestVersion;

                            updateResultMessage.visible = false;
                        }
                    }

                }

                Timer {
                    id: statusShowTimer

                    interval: 500
                    onTriggered: {
                        if (updateResultMessage.pendingResult && !kcm.checkingForUpdates)
                            updateResultMessage.showResult();

                    }
                }

                Timer {
                    id: statusHideTimer

                    interval: 8000
                    onTriggered: updateResultMessage.visible = false
                }

                Connections {
                    function onCheckingForUpdatesChanged() {
                        if (!kcm.checkingForUpdates && updateResultMessage.pendingResult) {
                            statusShowTimer.stop();
                            updateResultMessage.showResult();
                        }
                    }

                    function onDismissedUpdateVersionChanged() {
                        if (kcm.updateAvailable && kcm.dismissedUpdateVersion === kcm.latestVersion)
                            updateResultMessage.visible = false;

                    }

                    target: kcm
                }

            }

        }

        // Spacer
        Item {
            Layout.fillHeight: true
        }

    }

    header: ColumnLayout {
        width: parent.width
        spacing: 0

        // Update available banner
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: kcm.updateAvailable && kcm.dismissedUpdateVersion !== kcm.latestVersion
            type: Kirigami.MessageType.Information
            text: i18n("A new version is available: %1 (installed: %2)", kcm.latestVersion, kcm.currentVersion.length > 0 ? kcm.currentVersion : i18n("unknown"))
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

        // Master enable/disable row
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
                text: kcm.daemonRunning ? i18n("Running") : i18n("Stopped")
                opacity: 0.7
            }

            Switch {
                checked: kcm.daemonEnabled
                onToggled: kcm.daemonEnabled = checked
                Accessible.name: i18n("Enable PlasmaZones")
            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

    }

    // Helper component for link buttons
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
