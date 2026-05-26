// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    anchors.margins: Kirigami.Units.largeSpacing
    spacing: Kirigami.Units.largeSpacing

    Kirigami.Heading {
        text: qsTr("phosphor-settings-ui — minimal demo")
    }

    QQC2.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("This tiny example proves the framework: a sidebar, "
                   + "breadcrumbs, an apply/reset/cancel footer, and two "
                   + "registered pages — all driven by an ApplicationController.")
    }

    Item {
        Layout.fillHeight: true
    }
}
