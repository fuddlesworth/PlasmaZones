// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

OrderingPage {
    id: root

    function updateCustomOrderState() {
        root.hasCustomOrder = settingsController.hasCustomScrollingOrder();
    }

    headerText: i18n("Scrolling template priority")
    infoText: i18n("Set the priority order for templates when cycling with keyboard shortcuts and in the layout picker. Drag rows or use the arrow buttons to reorder.")
    emptyText: i18n("No templates available")
    emptyExplanation: i18n("Create templates on the Scrolling → Templates page first.")
    resetAccessibleName: i18n("Reset template order to default")
    hasCustomOrder: settingsController.hasCustomScrollingOrder()
    previewZonesKey: "zones"
    zoneCountKey: "zoneCount"
    countNoun: "column"
    resolveOrder: function () {
        return settingsController.resolvedScrollingOrder();
    }
    moveItem: function (from, to) {
        settingsController.moveScrollingTemplate(from, to);
    }
    resetOrder: function () {
        settingsController.resetScrollingOrder();
    }

    Connections {
        function onLayoutsChanged() {
            root.rebuildModel();
            root.updateCustomOrderState();
        }

        target: settingsController
    }

    Connections {
        function onStagedScrollingOrderChanged() {
            if (!root._rebuilding && !root._movingLocally)
                root.rebuildModel();

            root.updateCustomOrderState();
        }

        target: settingsController
    }
}
