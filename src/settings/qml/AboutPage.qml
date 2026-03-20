// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // --- App Header ---
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing * 2
            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                source: "plasmazones"
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            ColumnLayout {
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

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("Window tiling and zone management for Wayland compositors. " + "Define custom screen zones, snap windows with drag-and-drop or " + "keyboard shortcuts, and enjoy automatic tiling layouts.")
        }

        // --- Daemon ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Daemon")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        width: Kirigami.Units.gridUnit
                        height: Kirigami.Units.gridUnit
                        radius: width / 2
                        color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                    }

                    Label {
                        text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                        font.bold: true
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Start")
                        icon.name: "media-playback-start"
                        enabled: !settingsController.daemonRunning
                        onClicked: settingsController.daemonController.startDaemon()
                    }

                    Button {
                        text: i18n("Stop")
                        icon.name: "media-playback-stop"
                        enabled: settingsController.daemonRunning
                        onClicked: settingsController.daemonController.stopDaemon()
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Start daemon automatically")
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Switch {
                        checked: settingsController.daemonController.daemonEnabled
                        onToggled: settingsController.daemonController.daemonEnabled = checked
                        Accessible.name: i18n("Enable PlasmaZones daemon autostart")
                    }

                }

            }

        }

        // --- Links ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Links")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: i18n("GitHub Repository")
                    icon.name: "vcs-branch"
                    onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones")
                }

                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: i18n("Report a Bug")
                    icon.name: "tools-report-bug"
                    onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/issues/new")
                }

                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: i18n("Documentation / Wiki")
                    icon.name: "documentation"
                    onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/wiki")
                }

                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: i18n("Releases")
                    icon.name: "package-available"
                    onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/releases")
                }

            }

        }

        // --- License ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("License")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("PlasmaZones is free software licensed under the " + "GNU General Public License version 3 or later (GPL-3.0-or-later). " + "You are free to use, modify, and distribute this software " + "under the terms of the license.")
                }

                Button {
                    flat: true
                    text: i18n("View License Text")
                    icon.name: "text-x-copying"
                    onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/gpl-3.0.html")
                }

            }

        }

        // --- Credits ---
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2

            header: Kirigami.Heading {
                text: i18n("Credits")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Created by fuddlesworth")
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: i18n("Inspired by FancyZones and other tiling window managers. " + "Built for KDE Plasma and Wayland.")
                }

            }

        }

    }

}
