// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief One chevron: two strokes meeting at a tip and splaying by 45 degrees.
 *
 * Built pointing LEFT and rotated into the other three directions, in a SQUARE
 * box. The square is load-bearing, not tidiness: `rotation` pivots on the
 * item's centre, so a square is the only box whose on-screen extent survives
 * the 90 and 270 degree legs unchanged. A snug arm*cos45 by arm*2*sin45 box
 * needs its own inset algebra to undo the swap, and that algebra is exactly
 * the kind that goes wrong silently on one axis only.
 *
 * Shared by ZonePreview's edge ticks (which say the strip continues past this
 * edge) and StripEmptyState's arrowheads (which say which way an empty strip
 * would run). The two were hand-kept copies of this geometry, held in step by
 * comment alone, in the very change that deleted the last such pair.
 *
 * Deliberately carries NO opacity of its own. The two hosts fade differently
 * — ZonePreview at 0.5 on the chevron, StripEmptyState at 0.55 on a parent
 * that also covers the shaft — and baking either in would multiply against
 * the other.
 */
Item {
    id: chevron

    /// 0 left, 1 right, 2 up, 3 down.
    required property int direction
    /// Length of one stroke, i.e. the hypotenuse of the splay.
    required property real arm
    /// Stroke thickness. Hosts floor this at 1: below a pixel a stroke stops
    /// being drawn at all.
    required property real thickness
    /// Stroke colour.
    required property color strokeColor

    width: chevron.arm * Math.SQRT2
    height: width
    rotation: {
        switch (chevron.direction) {
        case 1:
            return 180;
        case 2:
            return 90;
        case 3:
            return 270;
        default:
            return 0;
        }
    }
    Accessible.ignored: true

    Repeater {
        // Two strokes, splayed either side of the tip. The model IS the
        // rotation, so the pair cannot drift out of symmetry.
        model: [-45, 45]

        Rectangle {
            required property real modelData

            width: chevron.arm
            height: chevron.thickness
            radius: chevron.thickness / 2
            color: chevron.strokeColor
            // The tip, centred in the square box: the shape spans arm*cos45
            // across, so half the slack sits either side.
            x: (chevron.width - chevron.arm * Math.SQRT1_2) / 2
            y: chevron.height / 2 - chevron.thickness / 2
            // Pivot on the tip, not the stroke's centre: both strokes must
            // share one origin or the chevron opens into a Z.
            transformOrigin: Item.Left
            rotation: modelData
        }
    }
}
