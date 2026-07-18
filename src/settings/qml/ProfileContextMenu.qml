// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Per-row overflow menu for a profile.
 *
 * A pure view: it emits one signal per action carrying the row, and the host
 * page performs the work (opening dialogs for rename / set-parent / export, a
 * confirm for delete, and calling the bridge directly for activate / duplicate).
 * Call showFor(row) to bind a row and open it at the cursor.
 */
Menu {
    id: root

    property var profileRow: null

    signal activateRequested(var row)
    signal renameRequested(var row)
    signal duplicateRequested(var row)
    signal setParentRequested(var row)
    signal exportRequested(var row)
    signal deleteRequested(var row)

    function showFor(row) {
        root.profileRow = row;
        root.popup();
    }

    modal: true

    MenuItem {
        text: i18n("Activate")
        icon.name: "dialog-ok-apply"
        // Already-active profile has nothing to switch to.
        enabled: root.profileRow && !root.profileRow.active
        onTriggered: root.activateRequested(root.profileRow)
    }

    MenuSeparator {}

    MenuItem {
        text: i18n("Rename…")
        icon.name: "edit-rename"
        onTriggered: root.renameRequested(root.profileRow)
    }

    MenuItem {
        text: i18n("Duplicate")
        icon.name: "edit-copy"
        onTriggered: root.duplicateRequested(root.profileRow)
    }

    MenuItem {
        text: i18n("Set Parent…")
        icon.name: "document-import"
        onTriggered: root.setParentRequested(root.profileRow)
    }

    MenuItem {
        text: i18n("Export…")
        icon.name: "document-export"
        onTriggered: root.exportRequested(root.profileRow)
    }

    MenuSeparator {}

    MenuItem {
        text: i18n("Delete")
        icon.name: "edit-delete"
        onTriggered: root.deleteRequested(root.profileRow)
    }
}
