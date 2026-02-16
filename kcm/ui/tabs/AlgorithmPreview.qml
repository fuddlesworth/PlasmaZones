// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual preview of autotiling algorithm layouts
 *
 * Renders a preview of how windows would be arranged with
 * the given algorithm settings.
 *
 * NOTE: This component implements simplified versions of the tiling algorithms
 * for preview purposes only. The actual tiling is performed by C++ algorithm
 * classes (MasterStackAlgorithm, BSPAlgorithm, FibonacciAlgorithm, etc.).
 * These QML previews are intentionally kept simple and may not reflect all
 * edge cases or algorithm-specific behaviors. They are designed to give users
 * a quick visual understanding of each algorithm's general layout pattern.
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

    // Throttled zone calculation (~60fps cap) to avoid redundant recalcs
    // when multiple properties change in the same frame
    property var zones: []

    Timer {
        id: recalcTimer
        interval: 16  // ~60fps cap
        onTriggered: root.zones = root.calculateZones()
    }

    onAlgorithmIdChanged: recalcTimer.restart()
    onWindowCountChanged: recalcTimer.restart()
    onSplitRatioChanged: recalcTimer.restart()
    onMasterCountChanged: recalcTimer.restart()

    Component.onCompleted: recalcTimer.start()

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
                return calculateFibonacci(count, ratio)
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

    function calculateFibonacci(count, ratio) {
        if (count <= 0) return []
        if (count === 1) return [{ x: 0, y: 0, w: 1, h: 1 }]

        // Dwindle pattern: alternates vertical/horizontal splits.
        // Window always takes left/top portion, remaining shifts right/down.
        let zones = []
        let x = 0, y = 0, w = 1, h = 1

        for (let i = 0; i < count; i++) {
            if (i === count - 1) {
                zones.push({ x, y, w, h })
            } else if (i % 2 === 0) {
                // Vertical split — window gets left portion
                zones.push({ x, y, w: w * ratio, h })
                x += w * ratio
                w *= (1 - ratio)
            } else {
                // Horizontal split — window gets top portion
                zones.push({ x, y, w, h: h * ratio })
                y += h * ratio
                h *= (1 - ratio)
            }
        }
        return zones
    }

    function calculateMonocle(count) {
        // In monocle, all windows are fullscreen — show as stacked cards
        // with slight horizontal offset to indicate depth
        let zones = []
        const step = count > 1 ? Math.min(0.04, 0.2 / (count - 1)) : 0
        for (let i = 0; i < count; i++) {
            zones.push({
                x: i * step,
                y: 0,
                w: 1 - (count - 1) * step,
                h: 1
            })
        }
        return zones
    }

    function calculateThreeColumn(count, ratio) {
        if (count <= 0) return []
        if (count === 1) return [{ x: 0, y: 0, w: 1, h: 1 }]
        if (count === 2) {
            return [
                { x: 0, y: 0, w: ratio, h: 1 },
                { x: ratio, y: 0, w: 1 - ratio, h: 1 }
            ]
        }

        let zones = []
        const sideWidth = (1 - ratio) / 2

        // Center (master) — first zone, matches C++ masterZoneIndex() = 0
        zones.push({ x: sideWidth, y: 0, w: ratio, h: 1 })

        // Distribute stack windows round-robin: left, right, left, right...
        const stackCount = count - 1
        const leftCount = Math.ceil(stackCount / 2)
        const rightCount = stackCount - leftCount

        let leftIdx = 0, rightIdx = 0
        const leftHeight = leftCount > 0 ? 1 / leftCount : 1
        const rightHeight = rightCount > 0 ? 1 / rightCount : 1

        for (let i = 0; i < stackCount; i++) {
            if (i % 2 === 0 && leftIdx < leftCount) {
                zones.push({
                    x: 0,
                    y: leftIdx * leftHeight,
                    w: sideWidth,
                    h: leftHeight
                })
                leftIdx++
            } else if (rightIdx < rightCount) {
                zones.push({
                    x: sideWidth + ratio,
                    y: rightIdx * rightHeight,
                    w: sideWidth,
                    h: rightHeight
                })
                rightIdx++
            } else if (leftIdx < leftCount) {
                zones.push({
                    x: 0,
                    y: leftIdx * leftHeight,
                    w: sideWidth,
                    h: leftHeight
                })
                leftIdx++
            }
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

            // Window number label (only first zone for monocle since they overlap)
            Label {
                anchors.centerIn: parent
                text: index + 1
                font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                font.bold: true
                color: Kirigami.Theme.textColor
                opacity: 0.8
                visible: root.algorithmId !== "monocle" || index === root.zones.length - 1
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

    // Algorithm name label (hidden when used inside the Tiling tab's algorithm section
    // where the name is already shown alongside the combo box)
    property bool showLabel: true

    Label {
        visible: root.showLabel
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 2
        text: {
            switch (root.algorithmId) {
                case "master-stack": return i18n("Master + Stack")
                case "bsp": return i18n("BSP")
                case "columns": return i18n("Columns")
                case "rows": return i18n("Rows")
                case "fibonacci": return i18n("Fibonacci")
                case "monocle": return i18n("Monocle")
                case "three-column": return i18n("Three Column")
                default: return root.algorithmId
            }
        }
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        opacity: 0.5
    }
}
