// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control as PhosphorUi

// PhosphorUi.AboutPageShell hosts the standard chrome (icon + name +
// version + description + license + homepage). PlasmaZones-specific content
// (the Links and Credits cards) is injected through the shell's extras slot.
PhosphorUi.AboutPageShell {
    id: root

    appName: i18n("PlasmaZones")
    appIcon: "plasmazones"
    appVersion: Qt.application.version.length > 0 ? i18n("Version %1", Qt.application.version) : i18n("Version unknown")
    description: i18n("Window snapping, tiling and scrolling for Wayland compositors. Snap windows into zones you draw, let an algorithm tile them for you, or scroll them along an endless strip. Every monitor picks its own mode.")
    // Not translated: a copyright notice is a legal attribution, not UI prose.
    copyright: "© 2026 fuddlesworth"
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
                    linkText: i18n("Discord Community")
                    linkIcon: "im-user"
                    url: "https://discord.gg/9CQzAptdJ5"
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
                        // `window` resolves to Main.qml's root id, so this page
                        // only has it when mounted inside the settings app's
                        // chrome. The typeof form is required rather than
                        // stylistic: `window` is an ID lookup, not a context
                        // property, so a bare `window && …` THROWS a
                        // ReferenceError wherever the id is absent, before &&
                        // can short-circuit. Only typeof degrades to false.
                        if (typeof window !== "undefined" && window && window.showWhatsNew)
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
                            // Elide rather than wrap: this label is a button's
                            // contentItem, so wrapping would grow the button
                            // instead of fitting the text to it.
                            elide: Text.ElideRight
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

                // All three wrap. SettingsCard puts its contentItem inside a
                // `clip: true` item and binds the content width to the card, so
                // a Label that does not wrap is truncated mid-word rather than
                // merely overflowing. Layout.preferredWidth: 0 goes with
                // wrapMode for the reason AboutPageShell documents: a WordWrap
                // Label reports its full unwrapped length as implicitWidth,
                // which would otherwise inflate the column.
                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Created by fuddlesworth")
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Snapping is inspired by FancyZones and scrolling by the niri compositor.")
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Built with Qt, KDE Frameworks, and Kirigami.")
                    opacity: 0.7
                    wrapMode: Text.WordWrap
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
            // the user clicking the button. The typeof guard on
            // `window` is the same one the What's New handler above
            // needs, and for the same reason.
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
                // Elide, not wrap — see the What's New label above.
                elide: Text.ElideRight
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
