// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Read-only monitor overview strip at the top of WindowRulesPage.
 *
 * A horizontal row of MonitorOverviewTiles, one per connected monitor.
 * Clicking a tile sets the page's monitor filter; clicking the active tile
 * again clears it. Preserves the at-a-glance spatial overview the old
 * MonitorAssignmentsCard provided, without being an editor.
 */
ColumnLayout {
    id: overview

    /// `WindowRuleController.monitorOverview()` output — a list of tile maps.
    required property var tiles
    /// The currently-selected monitor filter (empty = no filter).
    property string selectedScreenId: ""

    signal monitorSelected(string screenId)

    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true

        Label {
            text: i18n("MONITORS")
            font.bold: true
            opacity: 0.6
            font.capitalization: Font.AllUppercase
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            text: i18n("Read-only — click a monitor to filter the list")
            opacity: 0.5
            font.italic: true
        }

    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: overview.tiles

            MonitorOverviewTile {
                required property var modelData

                tileData: modelData
                selected: overview.selectedScreenId === modelData.screenId
                onClicked: {
                    // Click the active tile to clear the filter.
                    var next = overview.selectedScreenId === modelData.screenId ? "" : modelData.screenId;
                    overview.monitorSelected(next);
                }
            }

        }

    }

    Label {
        visible: overview.tiles.length === 0
        text: i18n("No monitors detected.")
        opacity: 0.6
        font.italic: true
    }

}
