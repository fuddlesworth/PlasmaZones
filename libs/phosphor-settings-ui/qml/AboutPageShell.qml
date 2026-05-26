// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Standard "About this application" page scaffold.
 *
 * The shell renders the conventional layout (icon + name + version on top,
 * description / copyright / license / homepage below) and exposes a
 * default-property slot for apps to inject extra content (daemon-status
 * cards, version-history toggles, "Report a bug" buttons, etc.).
 *
 * Usage:
 *
 *   AboutPageShell {
 *       appName: "phosphor-settings-ui-minimal"
 *       appIcon: "preferences-system"
 *       appVersion: "0.1.0"
 *       description: qsTr("A reusable Qt6/Kirigami settings framework.")
 *       homepageUrl: "https://example.com/phosphor"
 *
 *       // Anything declared as a child goes into the extras slot.
 *       Kirigami.Heading { text: qsTr("Build info") }
 *       QQC2.Label  { text: qsTr("Compiled against Qt %1").arg(qVersion) }
 *   }
 */
Kirigami.ScrollablePage {
    id: root

    /** Children placed inside the shell land in the extras slot below the
     *  standard content. Aliases `data` (not `children`) so the list-
     *  assignment form `extraContent: [item, item]` works as well as
     *  default-property nested children. */
    default property alias extraContent: extraColumn.data
    /** Optional content rendered ABOVE the icon/name/version header.
     *  Useful for app-level toggles (e.g. enable / disable the
     *  underlying daemon) that should anchor the page visually rather
     *  than sit below the standard about-page chrome. */
    property alias topContent: topColumn.data
    property string appName: ""
    property string appIcon: ""
    property string appVersion: ""
    property string description: ""
    property string copyright: ""
    property string license: ""
    property string homepageUrl: ""

    title: qsTr("About")

    ColumnLayout {
        width: root.width - root.leftPadding - root.rightPadding
        spacing: Kirigami.Units.largeSpacing

        // ── Top content slot ──────────────────────────────────────
        // Consumer-injected content anchored above the standard
        // about-page chrome. visible flips on once `topContent` has
        // been assigned (children.length comes from the data alias).
        ColumnLayout {
            id: topColumn

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: children.length > 0
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: topColumn.children.length > 0
        }

        // Header: icon + name + version
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                visible: root.appIcon !== ""
                source: root.appIcon
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    visible: root.appName !== ""
                    text: root.appName
                    level: 1
                }

                QQC2.Label {
                    visible: root.appVersion !== ""
                    text: root.appVersion
                    opacity: 0.7
                }

            }

        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.description !== ""
            wrapMode: Text.WordWrap
            text: root.description
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.copyright !== "" || root.license !== "" || root.homepageUrl !== ""
        }

        QQC2.Label {
            visible: root.copyright !== ""
            text: root.copyright
            opacity: 0.7
        }

        QQC2.Label {
            visible: root.license !== ""
            text: root.license
            opacity: 0.7
        }

        Kirigami.UrlButton {
            visible: root.homepageUrl !== ""
            url: root.homepageUrl
            text: root.homepageUrl
        }

        // Injection slot — apps drop extra cards/buttons here.
        ColumnLayout {
            id: extraColumn

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
        }

        Item {
            Layout.fillHeight: true
        }

    }

}
