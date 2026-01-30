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
 * Refactored to follow SRP:
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
        model: kcm.layouts
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        focus: true
        keyNavigationEnabled: true

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
            if (!layoutId || !kcm.layouts) return false

            for (let i = 0; i < kcm.layouts.length; i++) {
                if (kcm.layouts[i] && String(kcm.layouts[i].id) === String(layoutId)) {
                    currentIndex = i
                    positionViewAtIndex(i, GridView.Contain)
                    return true
                }
            }
            return false
        }

        // Sync with kcm.layoutToSelect
        Connections {
            target: kcm
            function onLayoutToSelectChanged() {
                if (kcm.layoutToSelect) {
                    Qt.callLater(() => layoutGrid.selectLayoutById(kcm.layoutToSelect))
                }
            }
        }

        onCountChanged: {
            if (kcm.layoutToSelect && count > 0) {
                selectLayoutById(kcm.layoutToSelect)
            }
        }

        Component.onCompleted: {
            Qt.callLater(() => {
                if (kcm.layoutToSelect && count > 0) {
                    selectLayoutById(kcm.layoutToSelect)
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
