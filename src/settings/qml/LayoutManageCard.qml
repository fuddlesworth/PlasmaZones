// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
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

        Rectangle {
            id: dropZone

            readonly property bool _highlight: dropArea.containsDrag

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            radius: Kirigami.Units.smallSpacing
            color: _highlight ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)
            border.width: Math.max(1, Math.round(Screen.devicePixelRatio))
            border.color: _highlight ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25)

            RowLayout {
                anchors.centerIn: parent
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: dropZone._highlight ? "document-import" : "document-open"
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                    color: dropZone._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                }

                Label {
                    text: dropZone._highlight ? (root._snapping ? i18n("Release to import layout") : i18n("Release to import algorithm")) : (root._snapping ? i18n("Drop a layout file here") : i18n("Drop an algorithm file here"))
                    color: dropZone._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                    font.italic: !dropZone._highlight
                }
            }

            DropArea {
                id: dropArea

                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: function (drop) {
                    var urls = drop.urls;
                    if (!urls || urls.length === 0) {
                        drop.accepted = false;
                        return;
                    }
                    var path = settingsController.urlToLocalFile(String(urls[0]));
                    if (root._snapping) {
                        // importLayout is fire-and-forget: success → layoutsChanged
                        // rebuild, failure → layoutOperationFailed toast (both
                        // handled by the page, matching the import-dialog path).
                        settingsController.importLayout(path);
                    } else if (settingsController.importAlgorithm(path)) {
                        if (window && window.showToast)
                            window.showToast(i18n("Algorithm imported"));
                    }
                    drop.accepted = true;
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

                    MenuItem {
                        text: i18n("Import Layout File…")
                        icon.name: "document-open"
                        onTriggered: root.requestImportLayout()
                    }

                    MenuSeparator {}

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
