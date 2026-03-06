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

        // Check for updates — the header banner in main.qml handles "update available";
        // this section only surfaces "up to date" or "error" feedback from manual checks.
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
                        // Fallback timer for rate-limited checks where checkingForUpdates
                        // never transitions (the check is silently skipped)
                        statusShowTimer.restart();
                    }
                }

                Kirigami.InlineMessage {
                    id: updateResultMessage

                    // Internal flag: true while waiting for a manual check to complete.
                    // Not a public API — only set by Button.onClicked, cleared by showResult().
                    property bool pendingResult: false
                    // Computed state avoids triplicating the condition across type/text
                    readonly property int resultState: kcm.updateAvailable ? 2 : kcm.latestVersion.length > 0 ? 1 : 0

                    function showResult() {
                        if (!pendingResult)
                            return ;

                        // re-entry guard (M1 fix)
                        pendingResult = false;
                        visible = true;
                        if (kcm.updateAvailable) {
                            // Only clear dismissal if the found version differs from
                            // what was previously dismissed (M2 fix) — re-shows header banner
                            if (kcm.latestVersion.length > 0 && kcm.dismissedUpdateVersion !== kcm.latestVersion)
                                kcm.dismissedUpdateVersion = "";

                        } else {
                            // Auto-hide non-actionable results after 8s (longer text needs more read time)
                            statusHideTimer.restart();
                        }
                    }

                    Layout.fillWidth: true
                    visible: false
                    type: resultState === 2 ? Kirigami.MessageType.Positive : resultState === 1 ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
                    text: resultState === 2 ? i18n("Version %1 is available! You are currently on %2.", kcm.latestVersion, kcm.currentVersion.length > 0 ? kcm.currentVersion : i18n("unknown")) : resultState === 1 ? i18n("You're up to date (%1).", kcm.currentVersion.length > 0 ? kcm.currentVersion : i18n("unknown")) : i18n("Could not check for updates. Please try again later.")
                    // Only show actions when an update is found; use conditional array
                    // for reliable rendering across all Kirigami versions (M4 fix)
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

                // Timers at ColumnLayout scope so both Button and InlineMessage
                // can reference them without cross-sibling id fragility (L1 fix)
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

                    // Synchronize with header banner dismiss (M3 fix)
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
