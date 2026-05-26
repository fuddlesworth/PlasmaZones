// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui as PhosphorUi

// Uses PhosphorUi.AboutPageShell for the standard chrome (icon + name +
// version + description + license + homepage) and injects PlasmaZones-
// specific content — daemon toggle, link cards, credits — via the
// shell's default-property `extraContent` slot.
PhosphorUi.AboutPageShell {
    id: root

    appName: i18n("PlasmaZones")
    appIcon: "plasmazones"
    appVersion: Qt.application.version.length > 0 ? i18n("Version %1", Qt.application.version) : i18n("Version unknown")
    description: i18n("A window tiling and zone management tool for " + "Wayland compositors. Organize your desktop with " + "customizable zones, automatic tiling layouts, " + "and keyboard-driven window placement.")
    license: i18n("PlasmaZones is free software licensed under the " + "GNU General Public License version 3 or later " + "(GPL-3.0-or-later).")
    homepageUrl: "https://github.com/fuddlesworth/PlasmaZones"
    // ── Daemon enable/disable toggle (top of page, above the header) ──
    topContent: [
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Enable PlasmaZones")
                font.weight: Font.DemiBold
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                opacity: 0.7
            }

            SettingsSwitch {
                checked: settingsController.daemonRunning
                enabled: !settingsController.daemonController.busy
                onToggled: function(newValue) {
                    settingsController.daemonController.setEnabled(newValue);
                }
                accessibleName: i18n("Enable PlasmaZones")
            }

        },
        Kirigami.Separator {
            Layout.fillWidth: true
        }
    ]

    // ── Links ───────────────────────────────────────────────────────
    SettingsCard {
        Layout.fillWidth: true
        headerText: i18n("Links")

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
                linkText: i18n("Documentation")
                linkIcon: "documentation"
                url: "https://phosphor-works.github.io/plasmazones/"
            }

            LinkButton {
                linkText: i18n("Releases")
                linkIcon: "package-available"
                url: "https://github.com/fuddlesworth/PlasmaZones/releases"
            }

            Button {
                Layout.fillWidth: true
                flat: true
                horizontalPadding: Kirigami.Units.largeSpacing
                Accessible.name: i18n("What's New")
                onClicked: {
                    if (typeof window !== "undefined" && window.showWhatsNew)
                        window.showWhatsNew();

                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "documentinfo"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    }

                    Label {
                        text: i18n("What's New")
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

    // ── Credits ─────────────────────────────────────────────────────
    SettingsCard {
        Layout.fillWidth: true
        headerText: i18n("Credits")

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                text: i18n("Created by fuddlesworth")
                font.weight: Font.DemiBold
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

    // ── LinkButton helper (kept inline so this file stays drop-in compatible
    // with the old AboutPage.qml — same component name + signature) ────
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
