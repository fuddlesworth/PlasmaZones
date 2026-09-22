// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

OrderingPage {
    id: root

    function updateCustomOrderState() {
        root.hasCustomOrder = settingsController.hasCustomTilingOrder();
    }

    headerText: i18n("Tiling algorithm priority")
    infoText: i18n("Set the priority order for algorithms when cycling with keyboard shortcuts and in the zone selector popup. Drag rows or use the arrow buttons to reorder.")
    emptyText: i18n("No algorithms available")
    emptyExplanation: i18n("Algorithms are registered by the daemon.")
    resetAccessibleName: i18n("Reset algorithm order to default")
    // Seed only: hasCustomTilingOrder() is a non-reactive Q_INVOKABLE, so the
    // first updateCustomOrderState() JS write severs this binding on purpose —
    // the Connections below are the refresh path from then on.
    hasCustomOrder: settingsController.hasCustomTilingOrder()
    previewZonesKey: "previewZones"
    zoneCountKey: "defaultMaxWindows"
    nameKey: "name"
    hideZeroBadge: true
    resolveOrder: function () {
        // Rows arrive with previewZones already stamped by the controller.
        return settingsController.resolvedTilingOrder();
    }
    moveItem: function (from, to) {
        settingsController.moveTilingAlgorithm(from, to);
    }
    resetOrder: function () {
        settingsController.resetTilingOrder();
    }

    Connections {
        function onAvailableAlgorithmsChanged() {
            root.rebuildModel();
            root.updateCustomOrderState();
        }

        target: settingsController
    }

    Connections {
        function onStagedTilingOrderChanged() {
            root.refreshFromStagedOrder();
            root.updateCustomOrderState();
        }

        target: settingsController
    }
}
