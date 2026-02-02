// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual preview of autotiling algorithm layouts
 *
 * Renders a preview of how windows would be arranged with
 * the given algorithm settings.
 */
Item {
    id: root

    // Algorithm configuration
    property string algorithmId: "master-stack"
    property int windowCount: 4
    property real splitRatio: 0.6
    property int masterCount: 1

    // Color customization
    property color windowColor: Kirigami.Theme.highlightColor
    property color windowBorder: Kirigami.Theme.textColor
    property real windowOpacity: 0.7

    // Calculate zones based on algorithm
    readonly property var zones: calculateZones()

    function calculateZones() {
        const count = Math.max(1, windowCount)
        const ratio = splitRatio
        const master = Math.min(masterCount, count)

        switch (algorithmId) {
            case "master-stack":
                return calculateMasterStack(count, ratio, master)
            case "bsp":
                return calculateBSP(count)
            case "columns":
                return calculateColumns(count)
            case "rows":
                return calculateRows(count)
            case "fibonacci":
                return calculateFibonacci(count)
            case "monocle":
                return calculateMonocle(count)
            case "three-column":
                return calculateThreeColumn(count, ratio)
            default:
                return calculateMasterStack(count, ratio, master)
        }
    }

    function calculateMasterStack(count, ratio, masterCount) {
        let zones = []
        if (count === 1) {
            zones.push({ x: 0, y: 0, w: 1, h: 1 })
            return zones
        }

        const masterWidth = count <= masterCount ? 1 : ratio
        const stackWidth = 1 - masterWidth
        const masterHeight = 1 / Math.min(count, masterCount)

        // Master area
        for (let i = 0; i < Math.min(count, masterCount); i++) {
            zones.push({
                x: 0,
                y: i * masterHeight,
                w: masterWidth,
                h: masterHeight
            })
        }

        // Stack area
        const stackCount = count - masterCount
        if (stackCount > 0) {
            const stackHeight = 1 / stackCount
            for (let i = 0; i < stackCount; i++) {
                zones.push({
                    x: masterWidth,
                    y: i * stackHeight,
                    w: stackWidth,
                    h: stackHeight
                })
            }
        }

        return zones
    }

    function calculateBSP(count) {
        if (count <= 0) return []
        if (count === 1) return [{ x: 0, y: 0, w: 1, h: 1 }]

        let zones = []
        let areas = [{ x: 0, y: 0, w: 1, h: 1 }]

        for (let i = 1; i < count; i++) {
            // Find largest area
            let maxIdx = 0
            let maxArea = 0
            for (let j = 0; j < areas.length; j++) {
                const area = areas[j].w * areas[j].h
                if (area > maxArea) {
                    maxArea = area
                    maxIdx = j
                }
            }

            const parent = areas[maxIdx]
            areas.splice(maxIdx, 1)

            // Split based on aspect ratio
            if (parent.w >= parent.h) {
                // Vertical split
                const half = parent.w / 2
                areas.push({ x: parent.x, y: parent.y, w: half, h: parent.h })
                areas.push({ x: parent.x + half, y: parent.y, w: half, h: parent.h })
            } else {
                // Horizontal split
                const half = parent.h / 2
                areas.push({ x: parent.x, y: parent.y, w: parent.w, h: half })
                areas.push({ x: parent.x, y: parent.y + half, w: parent.w, h: half })
            }
        }

        return areas
    }

    function calculateColumns(count) {
        let zones = []
        const colWidth = 1 / count
        for (let i = 0; i < count; i++) {
            zones.push({
                x: i * colWidth,
                y: 0,
                w: colWidth,
                h: 1
            })
        }
        return zones
    }

    function calculateRows(count) {
        let zones = []
        const rowHeight = 1 / count
        for (let i = 0; i < count; i++) {
            zones.push({
                x: 0,
                y: i * rowHeight,
                w: 1,
                h: rowHeight
            })
        }
        return zones
    }

    function calculateFibonacci(count) {
        if (count <= 0) return []
        if (count === 1) return [{ x: 0, y: 0, w: 1, h: 1 }]

        let zones = []
        let x = 0, y = 0, w = 1, h = 1

        for (let i = 0; i < count; i++) {
            if (i === count - 1) {
                zones.push({ x, y, w, h })
            } else {
                const direction = i % 4
                switch (direction) {
                    case 0: // Right
                        zones.push({ x, y, w: w * 0.5, h })
                        x += w * 0.5
                        w *= 0.5
                        break
                    case 1: // Down
                        zones.push({ x, y, w, h: h * 0.5 })
                        y += h * 0.5
                        h *= 0.5
                        break
                    case 2: // Left
                        zones.push({ x: x + w * 0.5, y, w: w * 0.5, h })
                        w *= 0.5
                        break
                    case 3: // Up
                        zones.push({ x, y: y + h * 0.5, w, h: h * 0.5 })
                        h *= 0.5
                        break
                }
            }
        }
        return zones
    }

    function calculateMonocle(count) {
        // In monocle, all windows are fullscreen, but we show them slightly offset for preview
        let zones = []
        for (let i = 0; i < count; i++) {
            const offset = i * 0.03
            zones.push({
                x: offset,
                y: offset,
                w: 1 - offset * 2,
                h: 1 - offset * 2
            })
        }
        return zones
    }

    function calculateThreeColumn(count, ratio) {
        if (count <= 0) return []
        if (count === 1) return [{ x: 0, y: 0, w: 1, h: 1 }]
        if (count === 2) {
            return [
                { x: 0, y: 0, w: 0.5, h: 1 },
                { x: 0.5, y: 0, w: 0.5, h: 1 }
            ]
        }

        let zones = []
        const sideWidth = (1 - ratio) / 2

        // Left column
        zones.push({ x: 0, y: 0, w: sideWidth, h: 1 })

        // Center (master)
        zones.push({ x: sideWidth, y: 0, w: ratio, h: 1 })

        // Right column - remaining windows
        const rightCount = count - 2
        const rightHeight = 1 / rightCount
        for (let i = 0; i < rightCount; i++) {
            zones.push({
                x: sideWidth + ratio,
                y: i * rightHeight,
                w: sideWidth,
                h: rightHeight
            })
        }

        return zones
    }

    // Render the zones
    Repeater {
        model: root.zones

        Rectangle {
            required property var modelData
            required property int index

            x: modelData.x * root.width
            y: modelData.y * root.height
            width: modelData.w * root.width - 2
            height: modelData.h * root.height - 2

            color: Qt.rgba(root.windowColor.r, root.windowColor.g, root.windowColor.b, root.windowOpacity)
            border.color: root.windowBorder
            border.width: 1
            radius: 2

            // Window number label
            Label {
                anchors.centerIn: parent
                text: index + 1
                font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                font.bold: true
                color: Kirigami.Theme.textColor
                opacity: 0.8
            }

            // Master indicator for first window(s)
            Rectangle {
                visible: root.algorithmId === "master-stack" && index < root.masterCount
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 2
                width: 8
                height: 8
                radius: 4
                color: Kirigami.Theme.positiveTextColor
            }
        }
    }

    // Algorithm name label
    Label {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 2
        text: {
            switch (root.algorithmId) {
                case "master-stack": return "Master + Stack"
                case "bsp": return "BSP"
                case "columns": return "Columns"
                case "rows": return "Rows"
                case "fibonacci": return "Fibonacci"
                case "monocle": return "Monocle"
                case "three-column": return "Three Column"
                default: return root.algorithmId
            }
        }
        font.pixelSize: 10
        opacity: 0.5
    }
}
