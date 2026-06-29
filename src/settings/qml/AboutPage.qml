// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control as PhosphorUi

// PhosphorUi.AboutPageShell hosts the standard chrome (icon + name +
// version + description + license + homepage); PlasmaZones-specific
// content (daemon toggle on top, link / license / credits cards in
// extras) is injected through the shell's slots.
PhosphorUi.AboutPageShell {
    id: root

    appName: i18n("PlasmaZones")
    appIcon: "plasmazones"
    appVersion: Qt.application.version.length > 0 ? i18n("Version %1", Qt.application.version) : i18n("Version unknown")
    description: i18n("A window tiling and zone management tool for Wayland compositors. Organize your desktop with customizable zones, automatic tiling layouts, and keyboard-driven window placement.")
    license: i18n("PlasmaZones is free software licensed under the GNU General Public License version 3 or later (GPL-3.0-or-later).")
    homepageUrl: "https://github.com/fuddlesworth/PlasmaZones"
    // ── Extras: Links / Credits cards rendered below the homepage URL ──
    extraContent: [
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
                        // Defensive truthy-check: this AboutPage is also used
                        // by the standalone phosphor-control demo, which
                        // doesn't define `showWhatsNew`. Guard `window`
                        // itself too — when AboutPage is hosted by the demo
                        // (no chrome) the `window` context property may be
                        // undefined, in which case reading `.showWhatsNew`
                        // on it throws.
                        if (window && window.showWhatsNew)
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
        },
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Credits")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Created by fuddlesworth")
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Inspired by FancyZones, extended with automatic tiling.")
                    opacity: 0.7
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Built with Qt, KDE Frameworks, and Kirigami.")
                    opacity: 0.7
                }
            }
        }
    ]

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
        // Scheme-gate Qt.openUrlExternally — the LinkButton component
        // is reusable and a future consumer wiring a user-controlled
        // URL through it could otherwise navigate the browser to a
        // local file:// or other unintended scheme.
        onClicked: {
            const u = linkButton.url;
            if (u.startsWith("https://") || u.startsWith("http://")) {
                Qt.openUrlExternally(u);
                return;
            }
            // Surface the rejection via a toast in addition to the
            // console.warn — a silent console message is invisible to
            // the user clicking the button. Defensive truthy-check on
            // `window` + `showToast` mirrors the AboutPage's other
            // call sites: the standalone phosphor-control demo
            // mounts this page without the chrome's toast.
            console.warn("AboutPage.LinkButton: refusing to open non-http(s) URL:", u);
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(i18n("Cannot open this link"));
        }

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
