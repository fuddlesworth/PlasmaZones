// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control.examples.minimal

Item {
    id: root

    // Set by PageHost.qml when the page is loaded.
    property GeneralPage controller: null

    Kirigami.FormLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing

        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Sound effects:")
            checked: root.controller ? root.controller.soundsEnabled : false
            onToggled: {
                if (root.controller)
                    root.controller.soundsEnabled = checked;
            }
        }

        QQC2.TextField {
            Kirigami.FormData.label: qsTr("Greeting:")
            text: root.controller ? root.controller.greeting : ""
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            onEditingFinished: {
                if (root.controller)
                    root.controller.greeting = text;
            }
        }
    }
}
