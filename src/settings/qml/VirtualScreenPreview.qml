// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Visual preview of virtual screen subdivisions with draggable dividers.
 *
 * Displays a scaled representation of the physical monitor with colored
 * rectangles for each virtual screen region. Supports both column (vertical)
 * and row (horizontal) dividers for grid layouts.
 */
Rectangle {
    id: previewRoot

    // ── Required properties ─────────────────────────────────────────────
    required property var pendingScreens
    required property int screenWidth
    required property int screenHeight
    required property int columns
    required property int rows
    // True when pendingScreens form a strict row-major columns×rows grid.
    // The divider handles assume that indexing, so they are only created
    // when this holds; non-grid configs render regions without dividers.
    required property bool isGrid
    // Keyboard nudge per arrow press on a focused divider: 1% of the axis,
    // matching the granularity of a small mouse drag (the drag path is
    // continuous; the page-side move handler clamps either way).
    readonly property real keyboardStepFraction: 0.01
    // ── Font sizing ratios for region labels ────────────────────────────
    // Fraction of region width used to scale the label font (0.125 = 1/8).
    readonly property real titleFontScaleFraction: 0.125
    // Fraction of region width used to scale the detail font (0.1 = 1/10).
    readonly property real detailFontScaleFraction: 0.1

    // ── Signals ─────────────────────────────────────────────────────────
    signal columnDividerMoved(int colIndex, real newFraction)
    signal rowDividerMoved(int rowIndex, real newFraction)

    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    border.width: 1
    radius: Kirigami.Units.smallSpacing

    // "No subdivisions" label when empty
    Label {
        anchors.centerIn: parent
        visible: (previewRoot.pendingScreens || []).length === 0
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

            x: modelData.x * previewRoot.width
            y: modelData.y * previewRoot.height
            width: modelData.width * previewRoot.width
            height: modelData.height * previewRoot.height
            Accessible.name: modelData.displayName || i18n("Screen %1", index + 1)
            Accessible.role: Accessible.Pane
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
            border.color: Kirigami.Theme.highlightColor
            border.width: 2
            radius: Kirigami.Units.smallSpacing / 2
            // Keep labels from painting outside the region in narrow (portrait /
            // thin-split) zones, belt-and-braces alongside the width cap below.
            clip: true

            ColumnLayout {
                anchors.centerIn: parent
                // Cap to the region width so the labels elide instead of
                // overflowing past narrow zone edges.
                width: Math.min(implicitWidth, regionRect.width - Kirigami.Units.smallSpacing)
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.displayName || i18n("Screen %1", index + 1)
                    font.weight: Font.DemiBold
                    font.pixelSize: Math.max(Kirigami.Theme.defaultFont.pixelSize * 0.7, Math.min(Kirigami.Theme.defaultFont.pixelSize * 1, regionRect.width * previewRoot.titleFontScaleFraction))
                    color: Kirigami.Theme.textColor
                    // Wrap rather than elide: portrait/thin zones are narrow but
                    // tall, so wrapping keeps the full label readable.
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: {
                        var wpx = Math.round(modelData.width * previewRoot.screenWidth);
                        var hpx = Math.round(modelData.height * previewRoot.screenHeight);
                        var pct = Math.round(modelData.width * 100);
                        if (previewRoot.rows > 1)
                            return i18nc("@info screen resolution in pixels", "%1\u00d7%2 px", wpx, hpx);

                        return i18nc("@info region width in pixels and as a percentage of the screen", "%1 px \u00b7 %2%", wpx, pct);
                    }
                    font.pixelSize: Math.max(Kirigami.Theme.defaultFont.pixelSize * 0.65, Math.min(Kirigami.Theme.defaultFont.pixelSize * 0.85, regionRect.width * previewRoot.detailFontScaleFraction))
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // ── Column dividers (vertical lines between adjacent columns) ────────
    Repeater {
        // Dividers require the strict row-major grid their move handlers
        // assume; hide them entirely for non-grid configs.
        model: previewRoot.isGrid && previewRoot.columns > 1 ? previewRoot.columns - 1 : 0

        Item {
            id: colDividerHandle

            required property int index
            readonly property real dividerX: {
                // Column boundary: right edge of column `index` in the first row
                if (index < previewRoot.pendingScreens.length - 1) {
                    var cell = previewRoot.pendingScreens[index];
                    return (cell.x + cell.width) * previewRoot.width;
                }
                return 0;
            }

            width: Kirigami.Units.smallSpacing * 2
            x: dividerX - Math.round(width / 2)
            y: 0
            height: previewRoot.height
            // Keyboard operation: Tab focuses the handle, Left/Right nudge the
            // boundary by keyboardStepFraction of the axis (same signal path as
            // the mouse drag; the page-side handler clamps to minimum widths).
            activeFocusOnTab: true
            Keys.onLeftPressed: previewRoot.columnDividerMoved(index, dividerX / previewRoot.width - previewRoot.keyboardStepFraction)
            Keys.onRightPressed: previewRoot.columnDividerMoved(index, dividerX / previewRoot.width + previewRoot.keyboardStepFraction)
            Accessible.name: i18n("Column divider %1", index + 1)
            Accessible.description: i18n("Move with the left and right arrow keys")
            Accessible.role: Accessible.Separator
            Accessible.focusable: true

            // Visual divider line
            Rectangle {
                anchors.centerIn: parent
                width: colDragArea.containsMouse || colDragArea.pressed || colDividerHandle.activeFocus ? 3 : 1
                height: parent.height - 4
                radius: 1
                // Functional divider (drag affordance), needs more presence
                // than a decorative separator. Focus turns it focusColor so the
                // keyboard target is visible.
                color: colDividerHandle.activeFocus ? Kirigami.Theme.focusColor : (colDragArea.containsMouse || colDragArea.pressed ? Kirigami.Theme.hoverColor : Kirigami.Theme.disabledTextColor)

                Behavior on width {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
            }

            // Drag grip indicator
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(Kirigami.Units.gridUnit * 0.75)
                height: Math.round(Kirigami.Units.gridUnit * 1.5)
                radius: 4
                color: colDividerHandle.activeFocus ? Qt.alpha(Kirigami.Theme.focusColor, 0.3) : (colDragArea.containsMouse || colDragArea.pressed ? Qt.alpha(Kirigami.Theme.hoverColor, 0.3) : Kirigami.Theme.backgroundColor)
                border.width: 1
                border.color: colDividerHandle.activeFocus ? Kirigami.Theme.focusColor : (colDragArea.containsMouse || colDragArea.pressed ? Kirigami.Theme.hoverColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast))
                visible: previewRoot.height > Math.round(Kirigami.Units.gridUnit * 2.5)

                // Grip dots (vertical)
                Column {
                    anchors.centerIn: parent
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    Repeater {
                        model: 3

                        Rectangle {
                            width: 2
                            height: 2
                            radius: 1
                            color: Kirigami.Theme.textColor
                            opacity: 0.5
                        }
                    }
                }
            }

            MouseArea {
                id: colDragArea

                property real dragStartX: 0
                property real dragStartFraction: 0

                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.SplitHCursor
                hoverEnabled: true
                // Keep the drag from being stolen by the enclosing
                // SettingsFlickable — without this, dragging a divider also
                // flicks/scrolls the whole settings page.
                preventStealing: true
                onPressed: function (mouse) {
                    dragStartX = mouse.x + colDividerHandle.x;
                    dragStartFraction = colDividerHandle.dividerX / previewRoot.width;
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return;

                    var globalX = mouse.x + colDividerHandle.x;
                    var deltaFraction = (globalX - dragStartX) / previewRoot.width;
                    var newFraction = dragStartFraction + deltaFraction;
                    previewRoot.columnDividerMoved(colDividerHandle.index, newFraction);
                }
            }
        }
    }

    // ── Row dividers (horizontal lines between adjacent rows) ────────────
    Repeater {
        // Dividers require the strict row-major grid their move handlers
        // assume; hide them entirely for non-grid configs.
        model: previewRoot.isGrid && previewRoot.rows > 1 ? previewRoot.rows - 1 : 0

        Item {
            id: rowDividerHandle

            required property int index
            readonly property real dividerY: {
                // Row boundary: bottom edge of row `index` in the first column
                var cellIndex = index * previewRoot.columns;
                if (cellIndex < previewRoot.pendingScreens.length) {
                    var cell = previewRoot.pendingScreens[cellIndex];
                    return (cell.y + cell.height) * previewRoot.height;
                }
                return 0;
            }

            x: 0
            y: dividerY - Math.round(height / 2)
            width: previewRoot.width
            height: Kirigami.Units.smallSpacing * 2
            // Keyboard operation: Tab focuses the handle, Up/Down nudge the
            // boundary by keyboardStepFraction of the axis (same signal path as
            // the mouse drag; the page-side handler clamps to minimum heights).
            activeFocusOnTab: true
            Keys.onUpPressed: previewRoot.rowDividerMoved(index, dividerY / previewRoot.height - previewRoot.keyboardStepFraction)
            Keys.onDownPressed: previewRoot.rowDividerMoved(index, dividerY / previewRoot.height + previewRoot.keyboardStepFraction)
            Accessible.name: i18n("Row divider %1", index + 1)
            Accessible.description: i18n("Move with the up and down arrow keys")
            Accessible.role: Accessible.Separator
            Accessible.focusable: true

            // Visual divider line
            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 4
                height: rowDragArea.containsMouse || rowDragArea.pressed || rowDividerHandle.activeFocus ? 3 : 1
                radius: 1
                // Functional divider (drag affordance), needs more presence
                // than a decorative separator. Focus turns it focusColor so the
                // keyboard target is visible.
                color: rowDividerHandle.activeFocus ? Kirigami.Theme.focusColor : (rowDragArea.containsMouse || rowDragArea.pressed ? Kirigami.Theme.hoverColor : Kirigami.Theme.disabledTextColor)

                Behavior on height {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
            }

            // Drag grip indicator (horizontal orientation)
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(Kirigami.Units.gridUnit * 1.5)
                height: Math.round(Kirigami.Units.gridUnit * 0.75)
                radius: 4
                color: rowDividerHandle.activeFocus ? Qt.alpha(Kirigami.Theme.focusColor, 0.3) : (rowDragArea.containsMouse || rowDragArea.pressed ? Qt.alpha(Kirigami.Theme.hoverColor, 0.3) : Kirigami.Theme.backgroundColor)
                border.width: 1
                border.color: rowDividerHandle.activeFocus ? Kirigami.Theme.focusColor : (rowDragArea.containsMouse || rowDragArea.pressed ? Kirigami.Theme.hoverColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast))
                visible: previewRoot.width > Math.round(Kirigami.Units.gridUnit * 2.5)

                // Grip dots (horizontal)
                Row {
                    anchors.centerIn: parent
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    Repeater {
                        model: 3

                        Rectangle {
                            width: 2
                            height: 2
                            radius: 1
                            color: Kirigami.Theme.textColor
                            opacity: 0.5
                        }
                    }
                }
            }

            MouseArea {
                id: rowDragArea

                property real dragStartY: 0
                property real dragStartFraction: 0

                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.SplitVCursor
                hoverEnabled: true
                // Keep the drag from being stolen by the enclosing
                // SettingsFlickable — without this, dragging a divider also
                // flicks/scrolls the whole settings page.
                preventStealing: true
                onPressed: function (mouse) {
                    dragStartY = mouse.y + rowDividerHandle.y;
                    dragStartFraction = rowDividerHandle.dividerY / previewRoot.height;
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return;

                    var globalY = mouse.y + rowDividerHandle.y;
                    var deltaFraction = (globalY - dragStartY) / previewRoot.height;
                    var newFraction = dragStartFraction + deltaFraction;
                    previewRoot.rowDividerMoved(rowDividerHandle.index, newFraction);
                }
            }
        }
    }
}
