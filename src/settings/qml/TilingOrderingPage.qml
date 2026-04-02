// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

OrderingPage {
    id: root

    readonly property var
    settingsBridge: TilingBridge {
    }

    headerText: i18n("Tiling Algorithm Priority")
    infoText: i18n("Set the priority order for algorithms when cycling with keyboard shortcuts and in the zone selector popup. Drag rows or use the arrow buttons to reorder.")
    emptyText: i18n("No algorithms available")
    emptyExplanation: i18n("Algorithms are registered by the daemon.")
    resetAccessibleName: i18n("Reset algorithm order to default")
    hasCustomOrder: settingsController.hasCustomTilingOrder()
    previewZonesKey: "previewZones"
    zoneCountKey: "defaultMaxWindows"
    hideZeroBadge: true
    resolveOrder: function() {
        let items = settingsController.resolvedTilingOrder();
        for (let i = 0; i < items.length; i++) items[i].previewZones = settingsController.generateAlgorithmPreview(items[i].id, 4, 0.6, 1)
        return items;
    }
    moveItem: function(from, to) {
        settingsController.moveTilingAlgorithm(from, to);
    }
    resetOrder: function() {
        settingsController.resetTilingOrder();
    }

    Connections {
        function onAvailableAlgorithmsChanged() {
            root.rebuildModel();
        }

        target: settingsController
    }

    Connections {
        function onStagedTilingOrderChanged() {
            if (!root._rebuilding)
                root.rebuildModel();

            root.hasCustomOrder = settingsController.hasCustomTilingOrder();
        }

        target: settingsController
    }

}
