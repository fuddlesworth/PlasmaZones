// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Toolbar for layout creation, import, and view switching.
 *
 * Per-layout actions (edit, duplicate, delete, export, set default) are
 * in the LayoutGridDelegate's right-click context menu.
 */
RowLayout {
    id: root

    required property var appSettings
    property int viewMode: 0 // 0 = Snapping Layouts, 1 = Auto Tile

    signal requestCreateNewLayout()
    signal requestImportLayout()
    signal requestOpenLayoutsFolder()
    signal viewModeRequested(int mode)

    spacing: Kirigami.Units.smallSpacing

    // New Layout — only in Snapping view
    Button {
        visible: root.viewMode === 0
        text: i18n("New Layout")
        icon.name: "list-add"
        onClicked: root.requestCreateNewLayout()
    }

    // Import — only in Snapping view
    Button {
        visible: root.viewMode === 0
        text: i18n("Import")
        icon.name: "document-import"
        onClicked: root.requestImportLayout()
    }

    // Open Layouts Folder — only in Snapping view
    Button {
        visible: root.viewMode === 0
        text: i18n("Open Folder")
        icon.name: "folder-open"
        flat: true
        onClicked: root.requestOpenLayoutsFolder()
    }

    Item {
        Layout.fillWidth: true
    }

    // View switcher — only visible when autotiling is enabled
    ComboBox {
        visible: root.appSettings.autotileEnabled
        model: [i18n("Snapping"), i18n("Tiling")]
        currentIndex: root.viewMode
        onActivated: (index) => {
            return root.viewModeRequested(index);
        }
    }

}
