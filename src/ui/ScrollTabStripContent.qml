// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Tab indicators for tabbed scrolling columns.
 *
 * Display-only and click-through: one compact pill per tabbed column,
 * centered on the column's top edge, listing the column's tabs with the
 * active one highlighted. The model arrives from C++ as `strips` — a list
 * of maps with x / y / width (shell-window coordinates), activeIndex, and
 * tabs (list of {title, active}). Updates are plain property writes; the
 * component is not re-instantiated per relayout.
 */

import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    /// Strip entries pushed by the daemon (see file doc).
    property var strips: []

    anchors.fill: parent

    Repeater {
        model: root.strips

        delegate: Rectangle {
            id: pill

            required property var modelData

            readonly property int columnX: modelData.x
            readonly property int columnY: modelData.y
            readonly property int columnWidth: modelData.width
            readonly property var tabs: modelData.tabs

            x: columnX + (columnWidth - width) / 2
            y: columnY + Kirigami.Units.smallSpacing
            width: Math.min(tabRow.implicitWidth + Kirigami.Units.largeSpacing * 2, columnWidth - Kirigami.Units.largeSpacing * 2)
            height: tabRow.implicitHeight + Kirigami.Units.smallSpacing * 2
            radius: height / 2
            color: Qt.alpha(Kirigami.Theme.backgroundColor, 0.85)
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
            clip: true

            Row {
                id: tabRow

                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: pill.tabs

                    delegate: Rectangle {
                        id: chip

                        required property var modelData

                        readonly property bool active: modelData.active === true

                        width: chipLabel.implicitWidth + Kirigami.Units.largeSpacing
                        height: chipLabel.implicitHeight + Kirigami.Units.smallSpacing
                        radius: height / 2
                        color: chip.active ? Kirigami.Theme.highlightColor : "transparent"

                        Text {
                            id: chipLabel

                            anchors.centerIn: parent
                            text: chip.modelData.title
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, Kirigami.Units.gridUnit * 8)
                            color: chip.active ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }
                }
            }
        }
    }
}
