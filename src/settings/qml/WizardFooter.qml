// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Footer for multi-step wizard dialogs with Back/Next/Create/Cancel buttons.
 */
Item {
    id: root

    required property int currentStep
    required property string createText
    required property bool createEnabled

    signal backClicked()
    signal nextClicked()
    signal createClicked()
    signal cancelClicked()

    implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2

    RowLayout {
        id: footerLayout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Kirigami.Units.largeSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Item {
            Layout.fillWidth: true
        }

        Button {
            visible: root.currentStep > 0
            text: i18n("Back")
            icon.name: "go-previous"
            Accessible.name: text
            onClicked: root.backClicked()
        }

        Button {
            visible: root.currentStep === 0
            text: i18n("Next")
            icon.name: "go-next"
            highlighted: true
            Accessible.name: text
            onClicked: root.nextClicked()
        }

        Button {
            id: createButton

            visible: root.currentStep === 1
            text: root.createText
            icon.name: "list-add"
            enabled: root.createEnabled
            highlighted: true
            Accessible.name: text
            onClicked: root.createClicked()
        }

        Button {
            text: i18n("Cancel")
            Accessible.name: text
            onClicked: root.cancelClicked()
        }

        Item {
            Layout.fillWidth: true
        }

    }

}
