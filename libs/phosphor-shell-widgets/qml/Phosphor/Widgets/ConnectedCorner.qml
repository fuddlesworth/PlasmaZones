// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.ConnectedCorner, a single fillet primitive.
//
// One quarter-corner fillet, themed and sized by radius, for hand-
// composing connected-corner joins where a whole BarCanvas path is
// overkill, e.g. painting the concave joint where a flush-edge popout
// meets the bar, or softening the inner angle between two stacked
// surfaces. BarCanvas builds its full outline in ConnectorGeometry; this
// is the same corner shape exposed as a standalone piece.
//
// At rotation 0:
//   convex (concave: false)  a quarter disc filling the corner, the
//                            rounded edge bulging away from the origin.
//   concave (concave: true)  a square with a quarter-disc bite taken out
//                            of the origin corner, the inverted/concave
//                            shape from the mockups.
//
// Orient with the standard `rotation` (0 / 90 / 180 / 270) and position
// the item where the join sits. The fill is a theme token, so it retints
// live with the surface it joins.
//
//   ConnectedCorner {
//       radius: 16
//       concave: true
//       fillColor: Theme.surface_container
//   }

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property real radius: 16
    property bool concave: false
    property color fillColor: Theme.surface_container

    implicitWidth: radius
    implicitHeight: radius

    readonly property string _path: {
        // Round to 2 decimals so a fractional radius doesn't produce
        // float-noise path strings (matches ConnectorGeometry).
        const r = Math.round(root.radius * 100) / 100;
        if (root.concave)
            // The r x r box minus a quarter disc bitten from the origin
            // corner: the inverted (concave) fillet.
            return "M " + r + " 0 L " + r + " " + r + " L 0 " + r + " A " + r + " " + r + " 0 0 1 " + r + " 0 Z";
        // A quarter-disc pie centred on the origin: the convex fillet.
        return "M 0 0 L " + r + " 0 A " + r + " " + r + " 0 0 1 0 " + r + " Z";
    }

    ConnectedShape {
        anchors.fill: parent
        fillColor: root.fillColor
        path: root._path
    }
}
