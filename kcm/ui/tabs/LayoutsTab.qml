// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Layouts tab - View, create, edit, import/export layouts
 *
 * * Refactored for better organization:
 * - LayoutToolbar handles action buttons
 * - LayoutGridDelegate handles individual card rendering
 * - This component handles overall layout and coordination
 */
ColumnLayout {
    id: root

    required property var kcm
    required property QtObject constants

    // Signals for dialog interactions (handled by main.qml)
    signal requestDeleteLayout(var layout)
    signal requestImportLayout()
    signal requestExportLayout(string layoutId)

    // Current layout helper for toolbar
    readonly property var currentLayout: layoutGrid.currentItem?.modelData ?? null

    spacing: Kirigami.Units.largeSpacing

    // Toolbar - extracted component
    LayoutToolbar {
        Layout.fillWidth: true
        kcm: root.kcm
        currentLayout: root.currentLayout

        onRequestDeleteLayout: (layout) => root.requestDeleteLayout(layout)
        onRequestImportLayout: root.requestImportLayout()
        onRequestExportLayout: (layoutId) => root.requestExportLayout(layoutId)
    }

    // Layout grid
    GridView {
        id: layoutGrid
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: root.constants.layoutListMinHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        focus: true
        keyNavigationEnabled: true

        // ── Imperative model management ─────────────────────────────────
        // Reactive `model: kcm.layouts` bindings cause scroll resets because
        // every layoutsChanged() creates a new QVariantList, which Qt treats
        // as a completely new model — destroying all delegates and resetting
        // contentY to 0. Instead we compare IDs before swapping.
        model: []

        function _extractIds(list) {
            let ids = []
            for (let i = 0; i < list.length; i++) {
                ids.push(String(list[i].id ?? ""))
            }
            return ids
        }

        function rebuildModel() {
            let newLayouts = kcm?.layouts ?? []

            // Compare by ID list — skip swap if order hasn't changed
            let oldIds = _extractIds(model)
            let newIds = _extractIds(newLayouts)
            if (oldIds.length === newIds.length) {
                let same = true
                for (let i = 0; i < oldIds.length; i++) {
                    if (oldIds[i] !== newIds[i]) { same = false; break }
                }
                if (same) {
                    // IDs unchanged — update individual entries in-place so
                    // delegates refresh zone previews without scroll reset.
                    // JS array assignment to model[i] does NOT reset scroll.
                    for (let i = 0; i < newLayouts.length; i++) {
                        model[i] = newLayouts[i]
                    }
                    return
                }
            }

            // ID list actually changed (add/remove/reorder) — full swap
            model = newLayouts
        }

        Connections {
            target: root.kcm ?? null
            function onLayoutsChanged() { layoutGrid.rebuildModel() }
        }

        // Responsive cell sizing - aim for 2-4 columns
        readonly property real minCellWidth: Kirigami.Units.gridUnit * 14
        readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
        readonly property real actualCellWidth: width / columnCount

        cellWidth: actualCellWidth
        cellHeight: Kirigami.Units.gridUnit * 10

        // Background
        Rectangle {
            anchors.fill: parent
            z: -1
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.Theme.disabledTextColor
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }

        // Selection by ID helper
        function selectLayoutById(layoutId) {
            if (!layoutId || !model) return false

            for (let i = 0; i < model.length; i++) {
                if (model[i] && String(model[i].id) === String(layoutId)) {
                    currentIndex = i
                    positionViewAtIndex(i, GridView.Contain)
                    return true
                }
            }
            return false
        }

        Connections {
            target: kcm
            function onLayoutToSelectChanged() {
                // Capture value immediately — C++ clears it after emission
                let layoutId = kcm.layoutToSelect
                if (layoutId) {
                    Qt.callLater(() => layoutGrid.selectLayoutById(layoutId))
                }
            }
        }

        Component.onCompleted: {
            rebuildModel()
            // Capture value — may be cleared by the time Qt.callLater runs
            let layoutId = kcm.layoutToSelect
            Qt.callLater(() => {
                if (layoutId && count > 0) {
                    selectLayoutById(layoutId)
                }
            })
        }

        // Delegate using extracted component
        // Note: modelData and index are automatically injected by GridView
        // into components with matching required properties
        delegate: LayoutGridDelegate {
            kcm: root.kcm
            cellWidth: layoutGrid.cellWidth
            cellHeight: layoutGrid.cellHeight
            isSelected: GridView.isCurrentItem

            onSelected: (idx) => layoutGrid.currentIndex = idx
            onActivated: (layoutId) => root.kcm.editLayout(layoutId)
            onDeleteRequested: (layout) => root.requestDeleteLayout(layout)
        }

        // Empty state
        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: layoutGrid.count === 0
            text: i18n("No layouts available")
            explanation: i18n("Start the PlasmaZones daemon or create a new layout")
        }
    }
}
