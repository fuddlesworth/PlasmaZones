// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Context menu for zone operations
 *
 * Provides right-click menu with split, duplicate, delete, fill, z-order, and clipboard actions.
 * Extracted from EditorZone.qml to reduce file size.
 */
Menu {
    id: contextMenu

    // Required properties
    required property var editorController
    required property string zoneId

    // Signals for zone operations
    signal splitHorizontalRequested()
    signal splitVerticalRequested()
    signal duplicateRequested()
    signal deleteRequested()
    signal deleteWithFillRequested()
    signal fillRequested()
    signal bringToFrontRequested()
    signal bringForwardRequested()
    signal sendBackwardRequested()
    signal sendToBackRequested()

    MenuItem {
        text: i18nc("@action", "Split Horizontally")
        icon.name: "view-split-top-bottom"
        onTriggered: contextMenu.splitHorizontalRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Split Vertically")
        icon.name: "view-split-left-right"
        onTriggered: contextMenu.splitVerticalRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {
    }

    MenuItem {
        text: i18nc("@action", "Copy")
        icon.name: "edit-copy"
        enabled: contextMenu.zoneId !== "" && contextMenu.editorController !== null
        onTriggered: {
            if (contextMenu.editorController && contextMenu.zoneId) {
                var zoneIds = [contextMenu.zoneId];
                contextMenu.editorController.copyZones(zoneIds);
            }
        }
        Accessible.name: i18nc("@action", "Copy zone")
        Accessible.description: i18nc("@info", "Copy the selected zone to clipboard")
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Cut")
        icon.name: "edit-cut"
        enabled: contextMenu.zoneId !== "" && contextMenu.editorController !== null
        onTriggered: {
            if (contextMenu.editorController && contextMenu.zoneId) {
                var zoneIds = [contextMenu.zoneId];
                contextMenu.editorController.cutZones(zoneIds);
            }
        }
        Accessible.name: i18nc("@action", "Cut zone")
        Accessible.description: i18nc("@info", "Cut the selected zone to clipboard")
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Duplicate")
        icon.name: "edit-copy"
        onTriggered: contextMenu.duplicateRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Delete")
        icon.name: "edit-delete"
        onTriggered: contextMenu.deleteRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Delete and Fill")
        icon.name: "edit-delete"
        onTriggered: contextMenu.deleteWithFillRequested()
        Accessible.name: text
        Accessible.description: i18nc("@info:tooltip", "Delete this zone and expand neighbors to fill the space")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {
    }

    MenuItem {
        text: i18nc("@action", "Paste")
        icon.name: "edit-paste"
        enabled: contextMenu.editorController && contextMenu.editorController.canPaste
        onTriggered: {
            if (contextMenu.editorController)
                contextMenu.editorController.pasteZones(false);

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
            if (contextMenu.editorController)
                contextMenu.editorController.pasteZones(true);

        }
        Accessible.name: i18nc("@action", "Paste zone with offset")
        Accessible.description: i18nc("@info", "Paste zones from clipboard with offset to avoid overlap")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {
    }

    MenuItem {
        text: i18nc("@action", "Fill Available Space")
        icon.name: "zoom-fit-best"
        onTriggered: contextMenu.fillRequested()
        Accessible.name: text
        Accessible.description: i18nc("@info:tooltip", "Expand zone to fill adjacent empty space")
        Accessible.role: Accessible.MenuItem
    }

    MenuSeparator {
    }

    MenuItem {
        text: i18nc("@action", "Bring to Front")
        icon.name: "layer-top"
        onTriggered: contextMenu.bringToFrontRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Bring Forward")
        icon.name: "layer-raise"
        onTriggered: contextMenu.bringForwardRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Send Backward")
        icon.name: "layer-lower"
        onTriggered: contextMenu.sendBackwardRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

    MenuItem {
        text: i18nc("@action", "Send to Back")
        icon.name: "layer-bottom"
        onTriggered: contextMenu.sendToBackRequested()
        Accessible.name: text
        Accessible.role: Accessible.MenuItem
    }

}
