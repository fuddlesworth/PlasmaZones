// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual preview of autotiling algorithm layouts
 *
 * Shows a representation of how windows would be arranged
 * with the selected algorithm, master count, and split ratio.
 */
Rectangle {
    id: root

    // Algorithm identifier (master-stack, columns, bsp)
    property string algorithm: "master-stack"

    // Layout parameters
    property int masterCount: 1
    property real splitRatio: 0.6
    property int windowCount: 4

    // Visual parameters
    property int innerGap: 4
    property int outerGap: 4

    implicitWidth: 280
    implicitHeight: 180
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.backgroundColor
    border.color: Kirigami.Theme.separatorColor
    border.width: 1

    // Preview area with gaps
    Item {
        id: previewArea
        anchors.fill: parent
        anchors.margins: root.outerGap

        // Generate zones based on algorithm
        Repeater {
            id: zoneRepeater
            model: generateZones()

            Rectangle {
                required property var modelData
                required property int index

                x: modelData.x
                y: modelData.y
                width: modelData.width
                height: modelData.height
                radius: Kirigami.Units.smallSpacing * 0.5
                color: index < root.masterCount && root.algorithm === "master-stack"
                    ? Qt.rgba(Kirigami.Theme.highlightColor.r,
                              Kirigami.Theme.highlightColor.g,
                              Kirigami.Theme.highlightColor.b, 0.3)
                    : Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.1)
                border.color: index < root.masterCount && root.algorithm === "master-stack"
                    ? Kirigami.Theme.highlightColor
                    : Kirigami.Theme.separatorColor
                border.width: 1

                // Window number label
                Label {
                    anchors.centerIn: parent
                    text: (index + 1).toString()
                    font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                    font.bold: index < root.masterCount && root.algorithm === "master-stack"
                    opacity: 0.7
                }
            }
        }
    }

    // Algorithm label
    Label {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 2
        text: {
            switch (root.algorithm) {
                case "master-stack": return i18n("Master + Stack")
                case "columns": return i18n("Columns")
                case "bsp": return i18n("BSP")
                default: return root.algorithm
            }
        }
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        opacity: 0.6
    }

    // Zone generation functions
    function generateZones() {
        const availableWidth = previewArea.width
        const availableHeight = previewArea.height - 20 // Reserve space for label
        const gap = root.innerGap

        if (root.windowCount === 0) {
            return []
        }

        if (root.windowCount === 1) {
            return [{x: 0, y: 0, width: availableWidth, height: availableHeight}]
        }

        switch (root.algorithm) {
            case "master-stack":
                return generateMasterStack(availableWidth, availableHeight, gap)
            case "columns":
                return generateColumns(availableWidth, availableHeight, gap)
            case "bsp":
                return generateBSP(availableWidth, availableHeight, gap)
            default:
                return generateMasterStack(availableWidth, availableHeight, gap)
        }
    }

    function generateMasterStack(width, height, gap) {
        const zones = []
        const masterWidth = Math.floor(width * root.splitRatio)
        const stackWidth = width - masterWidth - gap

        // Master windows
        const masterHeight = Math.floor((height - (root.masterCount - 1) * gap) / root.masterCount)
        for (let i = 0; i < root.masterCount && i < root.windowCount; i++) {
            zones.push({
                x: 0,
                y: i * (masterHeight + gap),
                width: root.windowCount > root.masterCount ? masterWidth : width,
                height: masterHeight
            })
        }

        // Stack windows
        const stackCount = root.windowCount - root.masterCount
        if (stackCount > 0) {
            const stackHeight = Math.floor((height - (stackCount - 1) * gap) / stackCount)
            for (let i = 0; i < stackCount; i++) {
                zones.push({
                    x: masterWidth + gap,
                    y: i * (stackHeight + gap),
                    width: stackWidth,
                    height: stackHeight
                })
            }
        }

        return zones
    }

    function generateColumns(width, height, gap) {
        const zones = []
        const columnCount = root.windowCount
        const columnWidth = Math.floor((width - (columnCount - 1) * gap) / columnCount)

        for (let i = 0; i < columnCount; i++) {
            zones.push({
                x: i * (columnWidth + gap),
                y: 0,
                width: columnWidth,
                height: height
            })
        }

        return zones
    }

    function generateBSP(width, height, gap) {
        const zones = []

        function split(x, y, w, h, count, horizontal) {
            if (count <= 0 || w < 20 || h < 20) {
                return
            }

            if (count === 1) {
                zones.push({x: x, y: y, width: w, height: h})
                return
            }

            const half1 = Math.floor(count / 2)
            const half2 = count - half1

            if (horizontal) {
                const splitHeight = Math.floor((h - gap) * root.splitRatio)
                split(x, y, w, splitHeight, half1, !horizontal)
                split(x, y + splitHeight + gap, w, h - splitHeight - gap, half2, !horizontal)
            } else {
                const splitWidth = Math.floor((w - gap) * root.splitRatio)
                split(x, y, splitWidth, h, half1, !horizontal)
                split(x + splitWidth + gap, y, w - splitWidth - gap, h, half2, !horizontal)
            }
        }

        split(0, 0, width, height, root.windowCount, width < height)
        return zones
    }
}
