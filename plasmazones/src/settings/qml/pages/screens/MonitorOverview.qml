// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Read-only monitor overview strip at the top of RulesPage.
 *
 * Iterates `settingsController.screens` so the visual treatment (icon +
 * displayLabel + Primary badge) matches the canonical `DisplayMap`
 * used elsewhere in the app. Per-monitor rule data comes from
 * `RuleController.monitorOverview(screens)`, whose tiles are keyed by each
 * screen's `name`, with a `screenId` fallback for a nameless output (a
 * screen with neither is omitted).
 *
 * Clicking a tile sets the page's monitor filter; clicking the active tile
 * again clears it.
 */
ColumnLayout {
    id: overview

    /// The full screen list from `settingsController.screens` — drives the
    /// list of tiles and supplies the rich metadata (displayLabel,
    /// connectorName, isPrimary, width/height).
    required property var screens
    /// `RuleController.monitorOverview()` output — rule-related summary
    /// per screen.
    required property var tiles
    /// The currently-selected monitor filter (empty = no filter).
    property string selectedScreenId: ""
    /// Indexed by each tile's `screenId` field — which the controller sets to
    /// the screen's `name` (or `screenId` when the name is empty) — for O(1)
    /// lookup by `screen.name` during rendering.
    readonly property var _tilesByScreenId: {
        var map = {};
        // Guard `tiles` itself — the binding can transiently produce
        // `undefined` if a callsite passes an unevaluated expression while
        // the controller is still warming up.
        if (!overview.tiles)
            return map;

        for (var i = 0; i < overview.tiles.length; ++i) {
            var t = overview.tiles[i];
            if (t && t.screenId)
                map[t.screenId] = t;
        }
        return map;
    }

    signal monitorSelected(string screenId)

    /// Resolve the tile entry for a given screen. Tiles are keyed by the
    /// screen's `name`, with a `screenId` fallback for a nameless output —
    /// mirror that here so the lookup matches the controller's keying.
    function _tileForScreen(screen) {
        return screen ? (overview._tilesByScreenId[screen.name] || overview._tilesByScreenId[screen.screenId]) : undefined;
    }

    spacing: Kirigami.Units.smallSpacing

    // Centered tile row — mirrors DisplayMap's wrapper-Item +
    // anchors.horizontalCenter convention so the strip never visually leans
    // to the left edge when there are only one or two monitors. No section
    // header or hint label — matches the bare placement of DisplayMap
    // on the other pages.
    Item {
        Layout.fillWidth: true
        implicitHeight: tileRow.implicitHeight

        RowLayout {
            id: tileRow

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                model: overview.screens

                MonitorOverviewTile {
                    required property var modelData

                    // Key by name with a screenId fallback — matching the
                    // controller's tile keying and `_tileForScreen` — so a
                    // nameless output can still be selected/filtered.
                    readonly property string _key: modelData.name || modelData.screenId || ""

                    screenData: modelData
                    tileData: overview._tileForScreen(modelData)
                    selected: overview.selectedScreenId === _key
                    onClicked: {
                        // Click the active tile to clear the filter.
                        overview.monitorSelected(overview.selectedScreenId === _key ? "" : _key);
                    }
                }
            }
        }
    }
}
