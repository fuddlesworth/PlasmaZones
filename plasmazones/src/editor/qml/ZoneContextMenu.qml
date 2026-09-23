// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls

/**
 * @brief Context menu for zone operations
 *
 * Provides right-click menu with split, duplicate, delete, fill, z-order, and clipboard actions.
 * Extracted from EditorZone.qml to reduce file size.
 */
Menu {
    id: contextMenu

    // Required properties.
    //
    // QtObject, not var, and the type is load-bearing for the `enabled` guards
    // below. A binding that evaluates to `undefined` does NOT write false to a
    // bool property: QML leaves the property at its default, and `enabled`
    // defaults to true. So with a `var` holding undefined, both
    // `editorController` and `editorController !== null` leave the item
    // ENABLED, and the guard fails open.
    //
    // Declaring the type closes it, but NOT by coercing: a typed object
    // property simply refuses an undefined write and keeps its PRIOR value.
    // On the BINDING path this file uses (EditorWindow passes
    // `editorController: editorWindow._editorController`) the refusal is
    // SILENT -- no warning is logged at all. Only a direct JS assignment
    // surfaces it, and that throws `Error: Cannot assign [undefined] to
    // QObject*` rather than logging it.
    // For a required property that has never held anything, that prior value
    // is null, so every spelling below evaluates false. The consequence worth
    // knowing: if this ever held a real controller and its source later went
    // undefined, the property would go STALE rather than null. It cannot
    // today, because main.cpp installs the context property before load and
    // never replaces it. Measured with qml6 6.11.2 on Item and QQC2 MenuItem.
    required property QtObject editorController
    required property string zoneId

    // Signals for zone operations
    signal splitHorizontalRequested
    signal splitVerticalRequested
    signal duplicateRequested
    signal deleteRequested
    signal deleteWithFillRequested
    signal fillRequested
    signal bringToFrontRequested
    signal bringForwardRequested
    signal sendBackwardRequested
    signal sendToBackRequested

    // Workaround for Qt 6 use-after-free in QQuickPopupPrivate::finalizeExitTransition.
    // dismiss()/close() still triggers the transition machinery which crashes on stale
    // Instantiator items. Setting visible=false bypasses the transition system entirely.
    function deferAction(action) {
        contextMenu.visible = false;
        Qt.callLater(action);
    }

    MenuItem {
        text: i18nc("@action", "Split Horizontally")
        icon.name: "view-split-top-bottom"
        onTriggered: contextMenu.deferAction(contextMenu.splitHorizontalRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Split Vertically")
        icon.name: "view-split-left-right"
        onTriggered: contextMenu.deferAction(contextMenu.splitVerticalRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {}

    MenuItem {
        text: i18nc("@action", "Copy")
        icon.name: "edit-copy"
        enabled: contextMenu.zoneId !== "" && contextMenu.editorController
        onTriggered: {
            let ctrl = contextMenu.editorController;
            let id = contextMenu.zoneId;
            contextMenu.deferAction(function () {
                if (ctrl && id)
                    ctrl.copyZones([id]);
            });
        }
        Accessible.name: i18nc("@action", "Copy zone")
        Accessible.description: i18nc("@info", "Copy the selected zone to clipboard")
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Cut")
        icon.name: "edit-cut"
        enabled: contextMenu.zoneId !== "" && contextMenu.editorController
        onTriggered: {
            let ctrl = contextMenu.editorController;
            let id = contextMenu.zoneId;
            contextMenu.deferAction(function () {
                if (ctrl && id)
                    ctrl.cutZones([id]);
            });
        }
        Accessible.name: i18nc("@action", "Cut zone")
        Accessible.description: i18nc("@info", "Cut the selected zone to clipboard")
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Duplicate")
        icon.name: "edit-copy"
        onTriggered: contextMenu.deferAction(contextMenu.duplicateRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Delete")
        icon.name: "edit-delete"
        onTriggered: contextMenu.deferAction(contextMenu.deleteRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Delete and Fill")
        icon.name: "edit-delete"
        onTriggered: contextMenu.deferAction(contextMenu.deleteWithFillRequested)
        Accessible.name: text
        Accessible.description: i18nc("@info:tooltip", "Delete this zone and expand neighbors to fill the space")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {}

    MenuItem {
        text: i18nc("@action", "Paste")
        icon.name: "edit-paste"
        enabled: contextMenu.editorController && contextMenu.editorController.canPaste
        onTriggered: {
            let ctrl = contextMenu.editorController;
            contextMenu.deferAction(function () {
                if (ctrl)
                    ctrl.pasteZones(false);
            });
        }
        Accessible.name: i18nc("@action", "Paste zone")
        Accessible.description: i18nc("@info", "Paste zones from clipboard")
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Paste with Offset")
        icon.name: "edit-paste"
        enabled: contextMenu.editorController && contextMenu.editorController.canPaste
        onTriggered: {
            let ctrl = contextMenu.editorController;
            contextMenu.deferAction(function () {
                if (ctrl)
                    ctrl.pasteZones(true);
            });
        }
        Accessible.name: i18nc("@action", "Paste zone with offset")
        Accessible.description: i18nc("@info", "Paste zones from clipboard with offset to avoid overlap")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {}

    MenuItem {
        text: i18nc("@action", "Fill Available Space")
        icon.name: "zoom-fit-best"
        onTriggered: contextMenu.deferAction(contextMenu.fillRequested)
        Accessible.name: text
        Accessible.description: i18nc("@info:tooltip", "Expand zone to fill adjacent empty space")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {}

    MenuItem {
        text: i18nc("@action", "Bring to Front")
        icon.name: "layer-top"
        onTriggered: contextMenu.deferAction(contextMenu.bringToFrontRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Bring Forward")
        icon.name: "layer-raise"
        onTriggered: contextMenu.deferAction(contextMenu.bringForwardRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Send Backward")
        icon.name: "layer-lower"
        onTriggered: contextMenu.deferAction(contextMenu.sendBackwardRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Send to Back")
        icon.name: "layer-bottom"
        onTriggered: contextMenu.deferAction(contextMenu.sendToBackRequested)
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }
}
