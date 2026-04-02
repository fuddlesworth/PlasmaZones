// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

OrderingPage {
    id: root

    readonly property var
    settingsBridge: SnappingBridge {
    }

    function updateCustomOrderState() {
        root.hasCustomOrder = settingsController.hasCustomSnappingOrder();
    }

    headerText: i18n("Snapping Layout Priority")
    infoText: i18n("Set the priority order for layouts when cycling with keyboard shortcuts and in the zone selector popup. Drag rows or use the arrow buttons to reorder.")
    emptyText: i18n("No layouts available")
    emptyExplanation: i18n("Create layouts in the Layouts page first.")
    resetAccessibleName: i18n("Reset layout order to default")
    hasCustomOrder: settingsController.hasCustomSnappingOrder()
    previewZonesKey: "zones"
    zoneCountKey: "zoneCount"
    resolveOrder: function() {
        return settingsController.resolvedSnappingOrder();
    }
    moveItem: function(from, to) {
        settingsController.moveSnappingLayout(from, to);
    }
    resetOrder: function() {
        settingsController.resetSnappingOrder();
    }

    Connections {
        function onLayoutsChanged() {
            root.rebuildModel();
            root.updateCustomOrderState();
        }

        target: settingsController
    }

    Connections {
        function onStagedSnappingOrderChanged() {
            if (!root._rebuilding && !root._movingLocally)
                root.rebuildModel();

            root.updateCustomOrderState();
        }

        target: settingsController
    }

}
