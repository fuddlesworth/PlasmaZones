// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Drill-down sidebar driven by PageRegistry.
 *
 * Shows top-level pages as a flat list; when the user enters a parent
 * page, the sidebar swaps to that parent's children. A back button at the
 * top returns to the top-level list.
 */
QQC2.ScrollView {
    id: root

    required property ApplicationController controller

    /** Empty string means "showing top-level pages"; otherwise the parent id. */
    property string currentParentId: ""

    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        // Back button shown when we are inside a parent's child list.
        QQC2.ItemDelegate {
            visible: root.currentParentId !== ""
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    source: "go-previous-symbolic"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: qsTr("Back")
                }
            }
            onClicked: root.currentParentId = ""
        }

        Repeater {
            model: root.currentParentId === ""
                   ? root.controller.registry.topLevelPagesData()
                   : root.controller.registry.childPagesData(root.currentParentId)

            delegate: QQC2.ItemDelegate {
                id: pageDelegate
                required property var modelData

                readonly property bool hasChildren:
                    root.controller.registry.childPagesData(modelData.id).length > 0
                readonly property bool isCurrent:
                    root.controller.currentPageId === modelData.id

                Layout.fillWidth: true
                highlighted: isCurrent

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Kirigami.Icon {
                        visible: pageDelegate.modelData.iconSource !== ""
                        source: pageDelegate.modelData.iconSource
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: pageDelegate.modelData.title
                    }
                    Kirigami.Icon {
                        visible: pageDelegate.hasChildren
                        source: "go-next-symbolic"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    }
                }

                onClicked: {
                    if (hasChildren) {
                        root.currentParentId = modelData.id;
                    } else {
                        root.controller.currentPageId = modelData.id;
                    }
                }
            }
        }
    }
}
