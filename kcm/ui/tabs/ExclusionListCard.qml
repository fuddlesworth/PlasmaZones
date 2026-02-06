// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for exclusion lists (applications or window classes)
 *
 * Eliminates duplication between excluded applications and excluded window classes
 * lists in ExclusionsTab.
 */
Kirigami.Card {
    id: root

    required property string title
    required property string placeholderText
    required property string emptyTitle
    required property string emptyExplanation
    required property string iconSource
    required property var model

    // Whether to use monospace font for list items
    property bool useMonospaceFont: false

    // Whether to show the "Pick from running windows" button
    property bool showPickButton: false

    // Signals for add/remove actions
    signal addRequested(string text)
    signal removeRequested(int index)
    signal pickRequested()

    header: Kirigami.Heading {
        level: 3
        text: root.title
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: root.placeholderText

                onAccepted: {
                    if (text.length > 0) {
                        root.addRequested(text)
                        text = ""
                    }
                }
            }

            Button {
                text: i18n("Add")
                icon.name: "list-add"
                enabled: inputField.text.length > 0
                onClicked: {
                    root.addRequested(inputField.text)
                    inputField.text = ""
                }
            }

            ToolButton {
                visible: root.showPickButton
                icon.name: "crosshairs"
                ToolTip.text: i18n("Pick from running windows")
                ToolTip.visible: hovered
                onClicked: root.pickRequested()
                Accessible.name: i18n("Pick from running windows")
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            // Use contentHeight with minimum fallback - never use fillHeight in ScrollView
            Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 6)
            Layout.minimumHeight: Kirigami.Units.gridUnit * 6
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.model
            interactive: false  // Parent ScrollView handles scrolling
            focus: true
            keyNavigationEnabled: true
            activeFocusOnTab: true

            Accessible.name: root.title
            Accessible.role: Accessible.List

            delegate: ItemDelegate {
                width: ListView.view.width
                required property string modelData
                required property int index

                highlighted: ListView.isCurrentItem
                onClicked: listView.currentIndex = index

                Keys.onDeletePressed: root.removeRequested(index)

                contentItem: RowLayout {
                    Kirigami.Icon {
                        source: root.iconSource
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Label {
                        text: modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        font: root.useMonospaceFont ? Kirigami.Theme.fixedWidthFont : undefined
                    }

                    ToolButton {
                        icon.name: "edit-delete"
                        onClicked: root.removeRequested(index)
                        Accessible.name: i18n("Remove %1", modelData)
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: parent.count === 0
                text: root.emptyTitle
                explanation: root.emptyExplanation
            }
        }
    }
}
