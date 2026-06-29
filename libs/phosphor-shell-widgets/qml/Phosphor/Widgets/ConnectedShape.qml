// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.ConnectedShape, the connected-corner surface painter.
//
// A thin Shape wrapper that fills an SVG path string with a theme colour.
// It is the low-level renderer behind BarCanvas and any other surface
// that wants the connected-corner shape language (popout bodies, the
// dashboard). Hand it a `path` (SVG `d` data, typically from
// ConnectorGeometry) and a `fillColor`:
//
//   ConnectedShape {
//       anchors.fill: parent
//       fillColor: Theme.surface_container
//       path: ConnectorGeometry.buildBarPath(width, 48, 16, 16, sockets)
//   }
//
// The fill retints live (the binding reads Theme.* directly) and animates
// the colour on a palette change. The geometry morph is driven by the
// consumer animating whatever feeds `path`.

import QtQuick
import QtQuick.Shapes
import Phosphor.Theme

Shape {
    id: root

    // SVG path data for the filled outline.
    property alias path: svgPath.path
    property color fillColor: Theme.surface_container
    property color strokeColor: "transparent"
    property real strokeWidth: 0

    // The curve renderer gives analytic antialiasing on the concave
    // (inverted) corners that the geometry renderer would leave jagged.
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        fillColor: root.fillColor
        strokeColor: root.strokeColor
        strokeWidth: root.strokeWidth

        Behavior on fillColor {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }

        PathSvg {
            id: svgPath
        }
    }
}
