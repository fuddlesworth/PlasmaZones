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
 * * Uses a model-driven approach instead of separate Button definitions.
 */
RowLayout {
    id: root

    required property var kcm
    required property var currentLayout  // Currently selected layout or null
    property int viewMode: 0  // 0 = Snapping Layouts, 1 = Auto Tile

    // The full autotile default ID including prefix, for comparison
    readonly property string autotileDefaultId: "autotile:" + root.kcm.autotileAlgorithm

    // Signals for actions that require dialog handling in parent
    signal requestDeleteLayout(var layout)
    signal requestImportLayout()
    signal requestExportLayout(string layoutId)
    signal viewModeRequested(int mode)

    spacing: Kirigami.Units.smallSpacing

    // Left-side actions — New/Duplicate/Delete only in Snapping view; Edit in both
    Repeater {
        model: {
            let items = []
            if (root.viewMode === 0) {
                items.push({
                    text: i18n("New Layout"),
                    icon: "list-add",
                    enabled: true,
                    action: () => root.kcm.createNewLayout(),
                    tooltip: ""
                })
            }
            items.push({
                text: i18n("Edit"),
                icon: "document-edit",
                enabled: root.currentLayout !== null,
                action: () => { if (root.currentLayout) root.kcm.editLayout(root.currentLayout.id) },
                tooltip: ""
            })
            if (root.viewMode === 0) {
                items.push({
                    text: i18n("Duplicate"),
                    icon: "edit-copy",
                    enabled: root.currentLayout !== null && root.currentLayout?.isAutotile !== true,
                    action: () => { if (root.currentLayout) root.kcm.duplicateLayout(root.currentLayout.id) },
                    tooltip: ""
                })
                items.push({
                    text: i18n("Delete"),
                    icon: "edit-delete",
                    enabled: root.currentLayout !== null && !root.currentLayout?.isSystem && !root.currentLayout?.isAutotile,
                    action: () => { if (root.currentLayout) root.requestDeleteLayout(root.currentLayout) },
                    tooltip: i18n("Delete the selected layout")
                })
            }
            return items
        }

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

    // "Set as Default" — visible in both views, behavior changes by viewMode
    Button {
        text: i18n("Set as Default")
        icon.name: "favorite"
        enabled: {
            if (!root.currentLayout) return false
            if (root.viewMode === 1) {
                return root.currentLayout.id !== root.autotileDefaultId
            }
            return root.currentLayout.id !== root.kcm.defaultLayoutId
        }
        onClicked: {
            if (!root.currentLayout) return
            if (root.viewMode === 1) {
                root.kcm.autotileAlgorithm = root.currentLayout.id.replace("autotile:", "")
            } else {
                root.kcm.defaultLayoutId = root.currentLayout.id
            }
        }

        ToolTip.visible: hovered
        ToolTip.text: root.viewMode === 1
            ? i18n("Set this algorithm as the default autotile algorithm")
            : i18n("Set this layout as the default for screens without specific assignments")
    }

    Item { Layout.fillWidth: true }

    // Right-side actions (import/export) — hidden in Auto Tile view
    Button {
        visible: root.viewMode === 0
        text: i18n("Import")
        icon.name: "document-import"
        onClicked: root.requestImportLayout()
    }

    Button {
        visible: root.viewMode === 0
        text: i18n("Export")
        icon.name: "document-export"
        enabled: root.currentLayout !== null
        onClicked: { if (root.currentLayout) root.requestExportLayout(root.currentLayout.id) }
    }

    // View switcher — only visible when autotiling is enabled
    ComboBox {
        visible: root.kcm.autotileEnabled
        model: [i18n("Snapping Layouts"), i18n("Auto Tile")]
        currentIndex: root.viewMode
        onActivated: (index) => root.viewModeRequested(index)
    }
}
