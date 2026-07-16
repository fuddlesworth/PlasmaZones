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
    property string errorText: ""
    /// Opt in to Return/Enter triggering Create on the final step, the way a
    /// dialog's default button would. Hosts with a custom footer get no default
    /// button from QQC2, and the focused control swallows Return first (each
    /// SettingsButtonGroup option and every card handles it), so the affordance
    /// has to come from a Shortcut. Off by default because a Shortcut preempts
    /// the focused item: a final step carrying a ComboBox or any other control
    /// that needs Return of its own must not enable it.
    property bool returnActivatesCreate: false

    signal backClicked
    signal nextClicked
    signal createClicked
    signal cancelClicked

    implicitHeight: footerColumn.implicitHeight + Kirigami.Units.largeSpacing * 2

    Shortcut {
        // `sequences` (plural) binds every sequence mapped to Return/Enter;
        // the singular form only catches one.
        sequences: [StandardKey.InsertParagraphSeparator]
        enabled: root.returnActivatesCreate && root.currentStep === 1 && root.createEnabled
        onActivated: root.createClicked()
    }

    ColumnLayout {
        id: footerColumn

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.errorText.length > 0
            text: root.errorText
            color: Kirigami.Theme.negativeTextColor
            font: Kirigami.Theme.smallFont
        }

        RowLayout {
            id: footerLayout

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
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
}
