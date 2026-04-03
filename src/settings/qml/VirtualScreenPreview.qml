// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual preview of virtual screen subdivisions with draggable dividers.
 *
 * Displays a scaled representation of the physical monitor with colored
 * rectangles for each virtual screen region and draggable divider handles
 * between adjacent regions.
 */
Rectangle {
    id: previewRoot

    // ── Required properties ─────────────────────────────────────────────
    required property var pendingScreens
    required property int screenWidth
    required property int screenHeight

    // ── Signals ─────────────────────────────────────────────────────────
    signal dividerMoved(int dividerIndex, real newFraction)

    Accessible.role: Accessible.Graphic
    Accessible.name: i18n("Virtual screen preview")
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
    border.width: Math.round(Kirigami.Units.devicePixelRatio)
    radius: Kirigami.Units.smallSpacing

    // "No subdivisions" label when empty
    Label {
        anchors.centerIn: parent
        visible: previewRoot.pendingScreens.length === 0
        text: i18n("No subdivisions (full screen)")
        color: Kirigami.Theme.disabledTextColor
        font.italic: true
    }

    // Virtual screen region rectangles
    Repeater {
        model: previewRoot.pendingScreens

        Rectangle {
            id: regionRect

            required property var modelData
            required property int index

            x: modelData.x * previewRoot.width + Math.round(Kirigami.Units.devicePixelRatio)
            y: modelData.y * previewRoot.height + Math.round(Kirigami.Units.devicePixelRatio)
            width: modelData.width * previewRoot.width - Math.round(Kirigami.Units.devicePixelRatio) * 2
            height: modelData.height * previewRoot.height - Math.round(Kirigami.Units.devicePixelRatio) * 2
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
            border.color: Kirigami.Theme.highlightColor
            border.width: 2
            radius: Kirigami.Units.smallSpacing / 2
            Accessible.name: i18n("Virtual screen region %1", modelData.displayName || (index + 1))

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: modelData.displayName || i18n("Screen %1", index + 1)
                    font.weight: Font.DemiBold
                    font.pixelSize: Math.max(Kirigami.Theme.defaultFont.pixelSize * 0.7, Math.min(Kirigami.Theme.defaultFont.pixelSize * 1, regionRect.width / 8))
                    color: Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: Math.round(modelData.width * previewRoot.screenWidth) + "px \u00b7 " + Math.round(modelData.width * 100) + "%"
                    font.pixelSize: Math.max(Kirigami.Theme.defaultFont.pixelSize * 0.65, Math.min(Kirigami.Theme.defaultFont.pixelSize * 0.85, regionRect.width / 10))
                    color: Kirigami.Theme.disabledTextColor
                }

            }

        }

    }

    // Draggable divider handles between regions
    Repeater {
        model: previewRoot.pendingScreens.length > 1 ? previewRoot.pendingScreens.length - 1 : 0

        Item {
            id: dividerHandle

            required property int index
            readonly property real dividerX: {
                if (index < previewRoot.pendingScreens.length - 1)
                    return (previewRoot.pendingScreens[index].x + previewRoot.pendingScreens[index].width) * previewRoot.width;

                return 0;
            }
            readonly property int handleHalfWidth: Math.round(Kirigami.Units.smallSpacing * 1.5)

            x: dividerX - handleHalfWidth
            y: 0
            width: handleHalfWidth * 2 + 1
            height: previewRoot.height
            Accessible.name: i18n("Virtual screen divider %1", index + 1)
            Accessible.role: Accessible.Separator

            // Visual divider line
            Rectangle {
                anchors.centerIn: parent
                width: dividerDragArea.containsMouse || dividerDragArea.pressed ? Math.round(Kirigami.Units.devicePixelRatio * 3) : Math.round(Kirigami.Units.devicePixelRatio)
                height: parent.height - 4
                radius: 1
                color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5)

                Behavior on width {
                    NumberAnimation {
                        duration: 100
                    }

                }

                Behavior on color {
                    ColorAnimation {
                        duration: 100
                    }

                }

            }

            // Drag grip indicator
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(Kirigami.Units.gridUnit * 0.75)
                height: Math.round(Kirigami.Units.gridUnit * 1.5)
                radius: 4
                color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                border.width: 1
                border.color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                visible: previewRoot.height > Kirigami.Units.gridUnit * 2.5

                // Grip dots
                Column {
                    anchors.centerIn: parent
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    Repeater {
                        model: 3

                        Rectangle {
                            width: Math.max(2, Math.round(Kirigami.Units.devicePixelRatio * 2))
                            height: Math.max(2, Math.round(Kirigami.Units.devicePixelRatio * 2))
                            radius: 1
                            color: Kirigami.Theme.textColor
                            opacity: 0.5
                        }

                    }

                }

            }

            MouseArea {
                id: dividerDragArea

                property real dragStartX: 0
                property real dragStartFraction: 0

                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.SplitHCursor
                hoverEnabled: true
                Accessible.name: i18n("Virtual screen divider %1", dividerHandle.index + 1)
                Accessible.description: i18n("Use numeric controls for keyboard adjustment")
                onPressed: function(mouse) {
                    dragStartX = mapToItem(previewRoot, mouse.x, 0).x;
                    dragStartFraction = dividerHandle.dividerX / previewRoot.width;
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return ;

                    var globalX = mapToItem(previewRoot, mouse.x, 0).x;
                    var deltaFraction = (globalX - dragStartX) / previewRoot.width;
                    var newFraction = dragStartFraction + deltaFraction;
                    previewRoot.dividerMoved(dividerHandle.index, newFraction);
                }
            }

        }

    }

}
