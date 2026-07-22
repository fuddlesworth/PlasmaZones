// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Identicon for a profile's resolved settings.
 *
 * Renders the hex digest ProfileStore hands back as `signature` (a hash of the
 * profile's fully-resolved config + rules) as a small symmetric pattern chip:
 * a 5x5 grid mirrored about its vertical centre, inked cells drawn in the
 * theme's highlight colour and the rest left as a faint wash of the same
 * colour. Deliberately monochrome — the mark takes its colour from the active
 * colour scheme so it never fights the palette, and profiles are told apart by
 * pattern alone.
 *
 * Two profiles that resolve to the same settings draw the same mark; any change
 * to the cascade redraws it.
 */
Item {
    id: sig

    /// Hex digest from ProfileStore::availableProfiles()'s `signature` field.
    property string signature: ""

    /// 5 columns mirrored about the centre, so only the left three carry
    /// information (3 x 5 = 15 bits of pattern).
    readonly property int _gridSize: 5
    readonly property int _halfColumns: 3

    readonly property real _cellSpacing: Math.max(1, Math.round(width * 0.06))
    readonly property real _cellSize: Math.max(1, (width - (sig._gridSize - 1) * sig._cellSpacing) / sig._gridSize)

    /// True when the cell at @p index (row-major over the grid) is inked.
    /// Mirrors the right half onto the left, then reads one digest bit.
    function cellInked(index) {
        if (signature.length < 4)
            return false;

        const column = index % sig._gridSize;
        const row = Math.floor(index / sig._gridSize);
        const mirrored = column >= sig._halfColumns ? sig._gridSize - 1 - column : column;
        const bit = row * sig._halfColumns + mirrored; // 0..14
        const nibbleIndex = Math.floor(bit / 4);
        if (nibbleIndex >= signature.length)
            return false;

        const nibble = parseInt(signature.charAt(nibbleIndex), 16);
        return ((nibble >> (bit % 4)) & 1) === 1;
    }

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: Kirigami.Units.iconSizes.medium

    Grid {
        anchors.centerIn: parent
        columns: sig._gridSize
        rows: sig._gridSize
        spacing: sig._cellSpacing

        Repeater {
            model: sig._gridSize * sig._gridSize

            delegate: Rectangle {
                required property int index

                width: sig._cellSize
                height: sig._cellSize
                radius: width * 0.25
                color: Kirigami.Theme.highlightColor
                // Unlit cells stay as a faint wash so the chip keeps a readable
                // shape instead of collapsing into a few floating dots.
                opacity: sig.cellInked(index) ? 1 : 0.15
            }
        }
    }
}
