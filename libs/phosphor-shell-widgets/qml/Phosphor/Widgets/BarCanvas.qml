// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.BarCanvas, the connected-corner bar surface.
//
// The shell's signature surface: a rounded bar strip whose bottom edge
// weaves down into "sockets" so attached popouts grow out of the bar as
// one continuous painted shape (concave/inverted corners at the join,
// convex corners at the pocket floor). See the design mockups in
// docs/phosphor-shell-design/mockups/{bar-top,control-center}.svg.
//
//   BarCanvas {
//       width: parent.width
//       barHeight: 48
//       sockets: ccOpen ? [{ x: ccX, width: ccW, depth: ccDepth }] : []
//       RowLayout { anchors.fill: parent /* bar widgets */ }
//   }
//
// `sockets` is the seam PopoutService drives: when a popout opens
// anchored to the bar it contributes a socket entry; animating that
// entry's `depth` (0 -> full) morphs the shape so the bar appears to grow
// downward. The popout's content is rendered over the pocket area by the
// host (the painted pocket and the content read as one surface).
//
// Default children land in the bar strip (the top `barHeight` band), so
// bar widgets are declared directly inside BarCanvas.

import QtQuick
import Phosphor.Theme
import "ConnectorGeometry.js" as ConnectorGeometry

Item {
    id: root

    // Closed bar height (the strip). Sockets extend below this.
    property real barHeight: 48
    // The bar's own four outer corners.
    property real cornerRadius: Tokens.radius_l
    // Fillet radius for socket corners (concave joins + pocket floor).
    property real connectorRadius: Tokens.radius_l
    // Surface fill. Tinted/animated by ConnectedShape.
    property color color: Theme.surface_container

    // Socket descriptors: [{ x, width, depth }] in bar-local coordinates.
    // Driven by the host / PopoutService. A socket with depth <= ~0.5 is
    // treated as closed (flat edge), so animating depth from 0 grows the
    // pocket smoothly. Update by REASSIGNING a fresh array (the path /
    // maxSocketDepth bindings react to the property, not to in-place
    // mutation of an element, e.g. `sockets[0].depth = x` will not redraw).
    property var sockets: []

    // Default children land in the bar strip, not over the whole item, so
    // a popout's pocket area below the strip stays clear for its content.
    default property alias content: strip.data

    // The generated outline, exposed for debugging and tests.
    readonly property alias pathData: shape.path

    // Deepest open socket, so the item reserves height for the pocket and
    // the host can size content against it. A socket at or below MIN_DEPTH
    // renders flat (no pocket), so it reserves no height either, matching
    // the geometry's gate.
    readonly property real maxSocketDepth: {
        let m = 0;
        const list = root.sockets;
        for (let i = 0; i < (list ? list.length : 0); ++i) {
            const d = list[i].depth || 0;
            if (d > ConnectorGeometry.MIN_DEPTH)
                m = Math.max(m, d);
        }
        return m;
    }

    implicitHeight: barHeight + maxSocketDepth

    ConnectedShape {
        id: shape

        anchors.fill: parent
        fillColor: root.color
        path: ConnectorGeometry.buildBarPath(root.width, root.barHeight, root.cornerRadius, root.connectorRadius, root.sockets)
    }

    // The bar strip: the band where widgets live. Sized to the closed bar
    // height; pockets open below it.
    Item {
        id: strip

        x: 0
        y: 0
        width: root.width
        height: root.barHeight
    }
}
