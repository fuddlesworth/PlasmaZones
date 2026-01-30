// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Data-driven toolbar for layout management actions
 *
 * Single Responsibility: Provide layout action buttons.
 * Follows DRY by using a model-driven approach instead of 7 separate Button definitions.
 */
RowLayout {
    id: root

    required property var kcm
    required property var currentLayout  // Currently selected layout or null

    // Signals for actions that require dialog handling in parent
    signal requestDeleteLayout(var layout)
    signal requestImportLayout()
    signal requestExportLayout(string layoutId)

    spacing: Kirigami.Units.smallSpacing

    // Left-side actions (layout manipulation)
    Repeater {
        model: [
            {
                text: i18n("New Layout"),
                icon: "list-add",
                enabled: true,
                action: () => root.kcm.createNewLayout(),
                tooltip: ""
            },
            {
                text: i18n("Edit"),
                icon: "document-edit",
                enabled: root.currentLayout !== null,
                action: () => { if (root.currentLayout) root.kcm.editLayout(root.currentLayout.id) },
                tooltip: ""
            },
            {
                text: i18n("Duplicate"),
                icon: "edit-copy",
                enabled: root.currentLayout !== null,
                action: () => { if (root.currentLayout) root.kcm.duplicateLayout(root.currentLayout.id) },
                tooltip: ""
            },
            {
                text: i18n("Delete"),
                icon: "edit-delete",
                enabled: root.currentLayout !== null && !root.currentLayout?.isSystem,
                action: () => { if (root.currentLayout) root.requestDeleteLayout(root.currentLayout) },
                tooltip: i18n("Delete the selected layout")
            },
            {
                text: i18n("Set as Default"),
                icon: "favorite",
                enabled: root.currentLayout !== null && root.currentLayout?.id !== root.kcm.defaultLayoutId,
                action: () => { if (root.currentLayout) root.kcm.defaultLayoutId = root.currentLayout.id },
                tooltip: i18n("Set this layout as the default for screens without specific assignments")
            }
        ]

        Button {
            required property var modelData
            text: modelData.text
            icon.name: modelData.icon
            enabled: modelData.enabled
            onClicked: modelData.action()

            ToolTip.visible: modelData.tooltip !== "" && hovered
            ToolTip.text: modelData.tooltip
        }
    }

    Item { Layout.fillWidth: true }

    // Right-side actions (import/export)
    Button {
        text: i18n("Import")
        icon.name: "document-import"
        onClicked: root.requestImportLayout()
    }

    Button {
        text: i18n("Export")
        icon.name: "document-export"
        enabled: root.currentLayout !== null
        onClicked: { if (root.currentLayout) root.requestExportLayout(root.currentLayout.id) }
    }
}
