// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Shapes
import Phosphor.Shell
import Phosphor.Theme

Item {
    id: root
    property string metric: "cpu"
    property int minutes: 1
    property real maximum: 100
    property color tint: Appearance.stops[0]
    property bool grid: true
    readonly property int revision: SystemStats.revision
    readonly property var samples: {
        void revision;
        return SystemStats.history(metric, minutes);
    }
    readonly property var paths: {
        const ceiling = Math.max(1, maximum), h = Math.max(1, height - 4), w = Math.max(1, width);
        let line = "", area = "", segment = "", start = 0, last = 0;
        for (const sample of samples) {
            if (sample.value < 0) {
                if (segment)
                    area += segment + " L " + last + " " + height + " L " + start + " " + height + " Z ";
                segment = "";
                continue;
            }
            const x = (sample.x * w).toFixed(2), y = (2 + h * (1 - Math.max(0, Math.min(1, sample.value / ceiling)))).toFixed(2);
            const command = (segment ? " L " : " M ") + x + " " + y;
            if (!segment)
                start = x;
            last = x;
            segment += command;
            line += command;
        }
        if (segment)
            area += segment + " L " + last + " " + height + " L " + start + " " + height + " Z ";
        return {
            line: line,
            area: area
        };
    }
    Accessible.ignored: true
    Repeater {
        model: root.grid ? 3 : 0
        Rectangle {
            required property int index
            x: 0
            y: 2 + index * (root.height - 4) / 2
            width: root.width
            height: 1
            color: Appearance.outline
        }
    }
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillColor: Qt.alpha(root.tint, 0.09)
            PathSvg {
                path: root.paths.area
            }
        }
        ShapePath {
            strokeColor: root.tint
            strokeWidth: root.grid ? 1.6 : 1.2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: root.paths.line
            }
        }
    }
}
