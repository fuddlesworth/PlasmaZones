// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual 3x3 position picker for zone selector placement
 *
 * Grid layout (cell indices):
 *   0=TopLeft,    1=Top,    2=TopRight
 *   3=Left,       4=Center, 5=Right
 *   6=BottomLeft, 7=Bottom, 8=BottomRight
 *
 * Center (4) is disabled.
 */
Item {
    id: root

    // Selected cell index (0-8), center (4) is invalid
    property int position: 1
    property bool enabled: true
    // Position labels for tooltips
    readonly property var positionLabels: [i18n("Top-Left"), i18n("Top"), i18n("Top-Right"), i18n("Left"), "", i18n("Right"), i18n("Bottom-Left"), i18n("Bottom"), i18n("Bottom-Right")]

    signal positionSelected(int newPosition)

    implicitWidth: 160
    implicitHeight: 110

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        Rectangle {
            id: screenFrame

            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Kirigami.Theme.backgroundColor
            radius: Kirigami.Units.smallSpacing
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
            border.width: 1

            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05)
                radius: Kirigami.Units.smallSpacing / 2

                Grid {
                    id: positionGrid

                    anchors.fill: parent
                    anchors.margins: 6
                    columns: 3
                    rows: 3
                    spacing: 3

                    Repeater {
                        model: 9

                        Rectangle {
                            id: cell

                            required property int index
                            property bool isCenter: index === 4
                            property bool isEdge: index === 1 || index === 3 || index === 5 || index === 7
                            property bool isCorner: !isCenter && !isEdge
                            property bool isSelected: index === root.position
                            property bool isHovered: cellMouse.containsMouse
                            // Zone selector indicator bars
                            // Corners show TWO bars (both edges), sides show ONE bar
                            property bool isTopRow: cell.index <= 2
                            property bool isBottomRow: cell.index >= 6
                            property bool isLeftCol: cell.index % 3 === 0
                            property bool isRightCol: cell.index % 3 === 2

                            width: (positionGrid.width - positionGrid.spacing * 2) / 3
                            height: (positionGrid.height - positionGrid.spacing * 2) / 3
                            radius: 3
                            color: {
                                if (isCenter)
                                    return "transparent";

                                if (isSelected)
                                    return Kirigami.Theme.highlightColor;

                                if (isHovered && root.enabled)
                                    return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4);

                                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15);
                            }
                            border.color: {
                                if (isCenter)
                                    return "transparent";

                                if (isSelected)
                                    return Kirigami.Theme.highlightColor;

                                if (isHovered && root.enabled)
                                    return Kirigami.Theme.highlightColor;

                                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3);
                            }
                            border.width: isSelected ? 2 : 1
                            opacity: root.enabled ? 1 : 0.5

                            // Horizontal bar (top or bottom edge)
                            Rectangle {
                                visible: cell.isSelected && !cell.isCenter && (cell.isTopRow || cell.isBottomRow)
                                color: Kirigami.Theme.backgroundColor
                                opacity: 0.95
                                radius: 2
                                width: Math.min(parent.width * 0.7, 24)
                                height: 4
                                x: {
                                    if (cell.isLeftCol)
                                        return 2;

                                    if (cell.isRightCol)
                                        return parent.width - width - 2;

                                    return (parent.width - width) / 2;
                                }
                                y: cell.isTopRow ? 2 : parent.height - height - 2
                            }

                            // Vertical bar (left or right edge)
                            Rectangle {
                                visible: cell.isSelected && !cell.isCenter && (cell.isLeftCol || cell.isRightCol)
                                color: Kirigami.Theme.backgroundColor
                                opacity: 0.95
                                radius: 2
                                width: 4
                                height: Math.min(parent.height * 0.6, 16)
                                x: cell.isLeftCol ? 2 : parent.width - width - 2
                                y: {
                                    if (cell.isTopRow)
                                        return 2;

                                    if (cell.isBottomRow)
                                        return parent.height - height - 2;

                                    return (parent.height - height) / 2;
                                }
                            }

                            MouseArea {
                                id: cellMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.enabled && !cell.isCenter
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    root.position = cell.index;
                                    root.positionSelected(cell.index);
                                }
                                ToolTip.visible: containsMouse && !cell.isCenter && root.enabled
                                ToolTip.delay: 500
                                ToolTip.text: root.positionLabels[cell.index]
                            }

                            Behavior on color {
                                ColorAnimation {
                                    duration: 100
                                }

                            }

                            Behavior on border.color {
                                ColorAnimation {
                                    duration: 100
                                }

                            }

                        }

                    }

                }

            }

        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.positionLabels[root.position] || ""
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            opacity: 0.7
        }

    }

}
