// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Visual snap indicator showing alignment guide lines
 *
 * Displays vertical and horizontal guide lines when zones snap to
 * grid positions or other zone edges during drag/resize operations.
 */
Item {
    id: snapIndicator

    // Active snap positions (0-1 relative coordinates)
    property var verticalSnapLines: []
    // Array of x positions
    property var horizontalSnapLines: []
    // Array of y positions
    property bool showSnapLines: true

    // Clear snap lines
    function clearSnapLines() {
        verticalSnapLines = [];
        horizontalSnapLines = [];
        showSnapLines = false; // Hide when cleared
    }

    // Set snap lines from snapped geometry
    function setSnapLines(snappedX, snappedY, snappedRight, snappedBottom, originalX, originalY, originalRight, originalBottom, threshold) {
        var vLines = [];
        var hLines = [];
        threshold = threshold || 0.005; // Default threshold
        // Check if left edge snapped
        if (Math.abs(snappedX - originalX) > threshold)
            vLines.push(snappedX);

        // Check if right edge snapped
        if (Math.abs(snappedRight - originalRight) > threshold)
            vLines.push(snappedRight);

        // Check if top edge snapped
        if (Math.abs(snappedY - originalY) > threshold)
            hLines.push(snappedY);

        // Check if bottom edge snapped
        if (Math.abs(snappedBottom - originalBottom) > threshold)
            hLines.push(snappedBottom);

        // Also show if snapping is close
        if (vLines.length === 0 && hLines.length === 0) {
            // No clear snap, but still show lines if very close
            var veryCloseThreshold = threshold * 0.5;
            if (Math.abs(snappedX - originalX) < veryCloseThreshold && Math.abs(snappedX - originalX) > 0.001)
                vLines.push(snappedX);

            // Show grid line even if exact snap didn't occur
            if (Math.abs(snappedRight - originalRight) < veryCloseThreshold && Math.abs(snappedRight - originalRight) > 0.001)
                vLines.push(snappedRight);

            if (Math.abs(snappedY - originalY) < veryCloseThreshold && Math.abs(snappedY - originalY) > 0.001)
                hLines.push(snappedY);

            if (Math.abs(snappedBottom - originalBottom) < veryCloseThreshold && Math.abs(snappedBottom - originalBottom) > 0.001)
                hLines.push(snappedBottom);

        }
        verticalSnapLines = vLines;
        horizontalSnapLines = hLines;
        showSnapLines = (vLines.length > 0 || hLines.length > 0); // Enable if any lines exist
    }

    anchors.fill: parent
    z: 200 // Above everything except dialogs

    // Vertical snap lines
    Repeater {
        model: (snapIndicator.showSnapLines && snapIndicator.width > 0) ? snapIndicator.verticalSnapLines : []

        Rectangle {
            required property real modelData

            visible: !isNaN(modelData) && modelData >= 0 && modelData <= 1
            x: visible ? (modelData * snapIndicator.width - Kirigami.Units.smallSpacing / 2) : 0 // Center 4px line
            y: 0
            width: Kirigami.Units.smallSpacing
            height: snapIndicator.height
            color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.3, Kirigami.Theme.highlightColor.g * 1.3, Kirigami.Theme.highlightColor.b * 1.3, 1)
            opacity: 1

            // Subtle shadow for contrast
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: parent.z - 1
                // Use theme text color for contrast (works in both light and dark themes)
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                opacity: 0.8
            }

            // Dashed line effect - thicker dashes for better visibility
            Rectangle {
                anchors.fill: parent

                gradient: Gradient {
                    orientation: Gradient.Vertical

                    GradientStop {
                        position: 0
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.15
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.2
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.35
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.4
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.55
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.6
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.75
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.8
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.95
                        color: "transparent"
                    }

                    GradientStop {
                        position: 1
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                }

            }

        }

    }

    // Horizontal snap lines
    Repeater {
        model: (snapIndicator.showSnapLines && snapIndicator.height > 0) ? snapIndicator.horizontalSnapLines : []

        Rectangle {
            required property real modelData

            visible: !isNaN(modelData) && modelData >= 0 && modelData <= 1
            x: 0
            y: visible ? (modelData * snapIndicator.height - Kirigami.Units.smallSpacing / 2) : 0 // Center 4px line
            width: snapIndicator.width
            height: Kirigami.Units.smallSpacing
            color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.3, Kirigami.Theme.highlightColor.g * 1.3, Kirigami.Theme.highlightColor.b * 1.3, 1)
            opacity: 1

            // Subtle shadow for contrast
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: parent.z - 1
                // Use theme text color for contrast (works in both light and dark themes)
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                opacity: 0.8
            }

            // Dashed line effect - thicker dashes for better visibility
            Rectangle {
                anchors.fill: parent

                gradient: Gradient {
                    orientation: Gradient.Horizontal

                    GradientStop {
                        position: 0
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.15
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.2
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.35
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.4
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.55
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.6
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.75
                        color: "transparent"
                    }

                    GradientStop {
                        position: 0.8
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                    GradientStop {
                        position: 0.95
                        color: "transparent"
                    }

                    GradientStop {
                        position: 1
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r * 1.5, Kirigami.Theme.highlightColor.g * 1.5, Kirigami.Theme.highlightColor.b * 1.5, 1)
                    }

                }

            }

        }

    }

}
