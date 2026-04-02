// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

OrderingPage {
    id: root

    readonly property var
    settingsBridge: SnappingBridge {
    }

    headerText: i18n("Snapping Layout Priority")
    infoText: i18n("Set the priority order for layouts when cycling with keyboard shortcuts and in the zone selector popup. Drag rows or use the arrow buttons to reorder.")
    emptyText: i18n("No layouts available")
    emptyExplanation: i18n("Create layouts in the Layouts page first.")
    resetAccessibleName: i18n("Reset layout order to default")
    hasCustomOrder: settingsController.settings.snappingLayoutOrder.length > 0
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
        }

        target: settingsController
    }

    Connections {
        function onSnappingLayoutOrderChanged() {
            if (!root._rebuilding)
                root.rebuildModel();

        }

        target: settingsController.settings
    }

}
