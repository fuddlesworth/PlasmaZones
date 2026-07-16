// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Centered radio-tile mode switch.
 *
 * A centered row of soft-highlight, label-only tiles mirroring
 * MonitorOverviewTile's visual treatment — selected tint + border, hover wash,
 * focus ring, and the shared widget.hover motion — instead of a segmented
 * button group, so pages read consistently with the monitor pickers elsewhere
 * in the app. Radio semantics: exactly one mode is active and clicking the
 * active tile is a no-op. The labels are injected via `modes`, so the same
 * switch is reusable across pages — the Layouts page uses it for its
 * Snapping / Tiling mode switch.
 */
Item {
    id: root

    /// Localized tile labels, one per mode.
    property var modes: []
    /// Selected mode index. Bound by the host page.
    property int currentIndex: 0

    /// Emitted when the user picks a different mode.
    signal indexChanged(int index)

    implicitHeight: tileRow.implicitHeight

    RowLayout {
        id: tileRow

        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: root.modes

            Rectangle {
                id: tile

                required property var modelData
                required property int index
                readonly property bool selected: root.currentIndex === index

                implicitWidth: tileContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                implicitHeight: tileContent.implicitHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.smallSpacing
                color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : tileMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                border.width: 1
                border.color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : tileMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.modelData
                Accessible.checked: tile.selected

                Label {
                    id: tileContent

                    anchors.centerIn: parent
                    text: tile.modelData
                    opacity: tile.selected ? 1 : 0.5
                }

                MouseArea {
                    id: tileMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    activeFocusOnTab: true
                    // Radio semantics — re-selecting the active mode is a no-op.
                    Keys.onSpacePressed: if (!tile.selected)
                        root.indexChanged(tile.index)
                    Keys.onReturnPressed: if (!tile.selected)
                        root.indexChanged(tile.index)
                    onClicked: if (!tile.selected)
                        root.indexChanged(tile.index)
                }

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }

                Behavior on border.color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
            }
        }
    }
}
