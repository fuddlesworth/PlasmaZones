// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    // URL scheme launch handles the common case. For non-KDE desktops,
    // users can run plasmazones-settings from the terminal.

    id: root

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // App header
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
                    text: kcm.currentVersion.length > 0 ? i18n("Version %1", kcm.currentVersion) : i18n("Version unknown")
                    opacity: 0.7
                }

            }

        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            text: i18n("Window tiling and zone management for Wayland compositors")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        // Open Settings button
        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing * 2
            text: i18n("Open PlasmaZones Settings")
            icon.name: "configure"
            font.bold: true
            implicitHeight: Kirigami.Units.gridUnit * 3
            implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, Kirigami.Units.gridUnit * 18)
            onClicked: kcm.openSettings()
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: i18n("Configure zones, tiling, appearance, shortcuts, and more")
            opacity: 0.5
            font: Kirigami.Theme.smallFont
        }

        // Links
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.largeSpacing

            Button {
                flat: true
                text: i18n("GitHub")
                icon.name: "vcs-branch"
                onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones")
            }

            Button {
                flat: true
                text: i18n("Report Bug")
                icon.name: "tools-report-bug"
                onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/issues/new")
            }

            Button {
                flat: true
                text: i18n("Wiki")
                icon.name: "documentation"
                onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/wiki")
            }

        }

        Item {
            Layout.fillHeight: true
        }

    }

}
