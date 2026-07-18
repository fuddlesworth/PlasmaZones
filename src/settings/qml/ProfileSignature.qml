// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Identicon for a profile's resolved settings.
 *
 * Renders the hex digest ProfileStore hands back as `signature` (a hash of the
 * profile's fully-resolved config + rules) into a small symmetric glyph: hue
 * from the leading bytes, a horizontally-mirrored 5x5 cell pattern from the
 * bits after them. Purely derived — two profiles that resolve to the same
 * settings therefore draw the same mark, and any change to the cascade
 * redraws it.
 */
Item {
    id: sig

    /// Hex digest from ProfileStore::availableProfiles()'s `signature` field.
    property string signature: ""

    /// Grid is 5 wide, mirrored about the centre column, so only the left
    /// three columns carry information (3 x 5 = 15 bits).
    readonly property int _gridSize: 5
    readonly property int _halfColumns: 3

    readonly property int _hue: signature.length >= 4 ? parseInt(signature.substring(0, 4), 16) % 360 : 0
    readonly property color _inkColor: Qt.hsla(sig._hue / 360, 0.55, 0.6, 1)

    /// True when the cell at @p index (row-major over the 5x5 grid) is inked.
    /// Mirrors the right half onto the left, then reads one bit out of the
    /// digest nibbles following the hue bytes.
    function cellInked(index) {
        if (signature.length < 8)
            return false;

        const column = index % sig._gridSize;
        const row = Math.floor(index / sig._gridSize);
        const mirrored = column > 2 ? sig._gridSize - 1 - column : column;
        const bit = row * sig._halfColumns + mirrored; // 0..14
        const nibbleIndex = 4 + Math.floor(bit / 4);
        if (nibbleIndex >= signature.length)
            return false;

        const nibble = parseInt(signature.charAt(nibbleIndex), 16);
        return ((nibble >> (bit % 4)) & 1) === 1;
    }

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: Kirigami.Units.iconSizes.medium

    // Tinted plate behind the pattern so a sparse glyph still reads as a mark
    // rather than a few floating dots.
    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing
        color: sig._inkColor
        opacity: 0.18
    }

    Grid {
        anchors.fill: parent
        anchors.margins: Math.round(sig.width / 10)
        columns: sig._gridSize
        rows: sig._gridSize
        spacing: 0

        Repeater {
            model: sig._gridSize * sig._gridSize

            delegate: Rectangle {
                required property int index

                width: Math.floor((sig.width - 2 * Math.round(sig.width / 10)) / sig._gridSize)
                height: width
                color: sig.cellInked(index) ? sig._inkColor : "transparent"
                radius: width * 0.25
            }
        }
    }
}
