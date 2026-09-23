// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control as Settings

Settings.AboutPageShell {
    appName: qsTr("Phosphor Settings — Minimal Demo")
    appIcon: "preferences-system"
    appVersion: "0.1.0"
    description: qsTr("Tiny end-to-end demo of the phosphor-control framework: ApplicationController + two registered pages routed through the standard sidebar/breadcrumbs/footer chrome.")
    copyright: "© 2026 fuddlesworth"
    license: "LGPL-2.1-or-later"
    homepageUrl: "https://github.com/fuddlesworth/PlasmaZones"
    // Extras land below the homepage URL via the named `extraContent`
    // slot — explicit list syntax, since the shell intentionally does
    // NOT make extraContent its default property (that would hijack
    // Kirigami.ScrollablePage's own default and break the shell's
    // internal layout).
    extraContent: [
        Kirigami.Heading {
            level: 3
            text: qsTr("Try the demo")
        },
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Switch to the General page, toggle the sound switch or edit the greeting, then watch the Apply / Cancel buttons in the footer light up.")
        }
    ]
}
