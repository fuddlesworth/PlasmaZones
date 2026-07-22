// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Import / open-folder card for the Layouts page.
 *
 * Mirrors the shader browser's "User shaders" card: a drop-zone for installing
 * by drag-and-drop plus an Open Folder action, with the explicit import sources
 * as buttons. View-mode aware — in Snapping view it imports layout JSON (and
 * exposes the KZones import sources); in Tiling view it imports Luau algorithms.
 *
 * The drop-zone imports the dropped file directly via settingsController; the
 * dialog-backed sources (file pickers) are surfaced as signals the page wires to
 * its FileDialogs.
 */
// Deliberately a plain SettingsCard rather than an ImportDropCard, which the
// shader browser and the sets pages share. ImportDropCard drives its result
// banner from an importFn that RETURNS whether the import worked, and
// importLayout is fire-and-forget: it reports through the page's
// layoutOperationFailed toast instead. Its Import affordance is also a menu
// (layout file, KZones config, KZones file), not a single button. Reshaping
// either side to fit would cost more than the duplication saves.
SettingsCard {
    id: root

    // 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0

    signal requestImportLayout
    signal requestImportFromKZones
    signal requestImportKZonesFile
    signal requestOpenLayoutsFolder
    signal requestImportAlgorithm
    signal requestOpenAlgorithmsFolder

    readonly property bool _snapping: viewMode === 0

    Layout.fillWidth: true
    headerText: _snapping ? i18n("User layouts") : i18n("User algorithms")
    searchAnchor: "manageLayouts"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            text: root._snapping ? i18n("Drop a layout file here to import it, or use the buttons below. User layouts live under your data directory.") : i18n("Drop a Luau algorithm file here to import it, or use the buttons below. User algorithms live under your data directory.")
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
        }

        FileDropZone {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            idleText: root._snapping ? i18n("Drop a layout file here") : i18n("Drop an algorithm file here")
            hoverText: root._snapping ? i18n("Release to import layout") : i18n("Release to import algorithm")
            onFileDropped: function (url) {
                var path = settingsController.urlToLocalFile(url);
                if (root._snapping) {
                    // importLayout is fire-and-forget: success → layoutsChanged
                    // rebuild, failure → layoutOperationFailed toast (both
                    // handled by the page, matching the import-dialog path).
                    settingsController.importLayout(path);
                } else {
                    // Success only, matching the import dialog: a failure
                    // toasts through algorithmOperationFailed, which carries
                    // the reason a dropped file was refused.
                    if (settingsController.importAlgorithm(path) && typeof window !== "undefined" && window && window.showToast)
                        window.showToast(i18n("Algorithm imported"));
                }
            }
        }

        // Explicit sources + Open Folder.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            // Snapping: Import menu (layout file + KZones sources).
            Button {
                visible: root._snapping
                text: i18n("Import")
                icon.name: "document-import"
                onClicked: importMenu.popup()

                Menu {
                    id: importMenu

                    // hasKZonesConfig() is a non-reactive Q_INVOKABLE, so a
                    // plain `enabled:` binding samples it once at creation.
                    // Refresh the cached value each time the menu opens,
                    // mirroring LayoutFilterBar's _refreshHasPriorityOrder.
                    property bool _hasKZonesConfig: false

                    onAboutToShow: importMenu._hasKZonesConfig = settingsController.hasKZonesConfig()

                    MenuItem {
                        text: i18n("Import Layout File…")
                        icon.name: "document-open"
                        onTriggered: root.requestImportLayout()
                    }

                    MenuSeparator {}

                    MenuItem {
                        text: i18n("Import from KZones")
                        icon.name: "document-import"
                        enabled: importMenu._hasKZonesConfig
                        onTriggered: root.requestImportFromKZones()
                    }

                    MenuItem {
                        text: i18n("Import KZones File…")
                        icon.name: "document-open-folder"
                        onTriggered: root.requestImportKZonesFile()
                    }
                }
            }

            // Tiling: single algorithm import (no KZones equivalent).
            Button {
                visible: !root._snapping
                text: i18n("Import")
                icon.name: "document-import"
                onClicked: root.requestImportAlgorithm()
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: i18n("Open Folder")
                icon.name: "folder-open"
                flat: true
                Accessible.name: root._snapping ? i18n("Open user layouts directory") : i18n("Open user algorithms directory")
                onClicked: root._snapping ? root.requestOpenLayoutsFolder() : root.requestOpenAlgorithmsFolder()
            }
        }
    }
}
