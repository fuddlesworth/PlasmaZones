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
    signal requestImportFromKZones()
    signal requestImportKZonesFile()
    signal requestOpenLayoutsFolder()
    signal requestCreateNewAlgorithm()
    signal requestImportAlgorithm()
    signal requestOpenAlgorithmsFolder()
    signal viewModeRequested(int mode)

    spacing: Kirigami.Units.smallSpacing

    // New Layout — only in Snapping view
    Button {
        visible: root.viewMode === 0
        text: i18n("New Layout")
        icon.name: "list-add"
        onClicked: root.requestCreateNewLayout()
    }

    // Import — only in Snapping view (dropdown menu)
    Button {
        id: importButton

        visible: root.viewMode === 0
        text: i18n("Import")
        icon.name: "document-import"
        onClicked: importMenu.popup()

        Menu {
            id: importMenu

            MenuItem {
                text: i18n("Import Layout File…")
                icon.name: "document-open"
                onTriggered: root.requestImportLayout()
            }

            MenuSeparator {
            }

            MenuItem {
                text: i18n("Import from KZones")
                icon.name: "document-import"
                enabled: settingsController.hasKZonesConfig()
                onTriggered: root.requestImportFromKZones()
            }

            MenuItem {
                text: i18n("Import KZones File…")
                icon.name: "document-open-folder"
                onTriggered: root.requestImportKZonesFile()
            }

        }

    }

    // Open Layouts Folder — only in Snapping view
    Button {
        visible: root.viewMode === 0
        text: i18n("Open Folder")
        icon.name: "folder-open"
        flat: true
        onClicked: root.requestOpenLayoutsFolder()
    }

    // New Algorithm — only in Tiling view
    Button {
        visible: root.viewMode === 1
        text: i18n("New Algorithm")
        icon.name: "list-add"
        onClicked: root.requestCreateNewAlgorithm()
    }

    // Import Algorithm — only in Tiling view
    Button {
        visible: root.viewMode === 1
        text: i18n("Import")
        icon.name: "document-import"
        onClicked: root.requestImportAlgorithm()
    }

    // Open Algorithms Folder — only in Tiling view
    Button {
        visible: root.viewMode === 1
        text: i18n("Open Folder")
        icon.name: "folder-open"
        flat: true
        onClicked: root.requestOpenAlgorithmsFolder()
    }

    Item {
        Layout.fillWidth: true
    }

    // View switcher — only visible when autotiling is enabled
    SettingsButtonGroup {
        visible: root.appSettings.autotileEnabled
        model: [i18n("Snapping"), i18n("Tiling")]
        currentIndex: root.viewMode
        onIndexChanged: (index) => {
            return root.viewModeRequested(index);
        }
    }

}
