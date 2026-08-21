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
        // Guarded because `parent` is null for one event loop while
        // SimpleKCM (a Kirigami.ScrollablePage) instantiates its content item.
        // Unguarded, the binding computes NaN for that frame and the NaN
        // propagates into every Layout.fillWidth child. AboutPageShell carries
        // the same guard for the same reason.
        width: parent ? parent.width : 0
        spacing: Kirigami.Units.largeSpacing

        // App header
        RowLayout {
            // No Layout.fillWidth here: a filling item ignores the horizontal
            // half of Layout.alignment, which left-aligned the icon, heading and
            // version under a page whose every other element centres itself.
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
                    text: qsTr("PlasmaZones")
                }

                Label {
                    // qsTr takes no substitution arguments, so %1 is filled with
                    // .arg() rather than by the call itself.
                    text: kcm.currentVersion.length > 0 ? qsTr("Version %1").arg(kcm.currentVersion) : qsTr("Version unknown")
                    opacity: 0.7
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Window snapping, tiling and scrolling for Wayland compositors")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        // Open Settings button
        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing * 2
            // The implicitWidth below is a floor with no ceiling, and button
            // text never wraps, so a long translation or a large font scale
            // would push the button past the viewport. Cap it at the column.
            Layout.maximumWidth: parent ? parent.width : 0
            text: qsTr("Open PlasmaZones Settings")
            icon.name: "configure"
            font.bold: true
            implicitHeight: Kirigami.Units.gridUnit * 3
            implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, Kirigami.Units.gridUnit * 18)
            onClicked: kcm.openSettings()
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Configure zones, tiling, scrolling, appearance, shortcuts, and more")
            // This PR lengthened the string. Without wrapMode it overflows the
            // column at a narrow KCM width instead of wrapping, and a German or
            // Russian translation makes that worse. Matches the tagline Label above.
            wrapMode: Text.WordWrap
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
                text: qsTr("GitHub")
                icon.name: "vcs-branch"
                onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones")
            }

            Button {
                flat: true
                text: qsTr("Report Bug")
                icon.name: "tools-report-bug"
                onClicked: Qt.openUrlExternally("https://github.com/fuddlesworth/PlasmaZones/issues/new")
            }

            Button {
                flat: true
                text: qsTr("Documentation")
                icon.name: "documentation"
                onClicked: Qt.openUrlExternally("https://phosphor-works.github.io/plasmazones/")
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
