// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Snapping / Tiling mode switch styled as the WindowRulesPage monitor
 *        switcher.
 *
 * A centered row of soft-highlight tiles (icon + label) mirroring
 * MonitorOverviewTile's visual treatment — selected tint + border, hover wash,
 * focus ring, and the shared widget.hover motion — instead of a segmented
 * button group, so the Layouts page reads consistently with the monitor pickers
 * elsewhere in the app. Radio semantics: exactly one mode is active and clicking
 * the active tile is a no-op (icons are the Snapping/Tiling sidebar icons).
 */
Item {
    id: root

    // Selected mode index (0 = Snapping, 1 = Tiling). Bound by the host page.
    property int currentIndex: 0

    // Emitted when the user picks a different mode.
    signal indexChanged(int index)

    readonly property var _modes: [
        {
            "icon": "view-split-left-right",
            "label": i18n("Snapping")
        },
        {
            "icon": "window-duplicate",
            "label": i18n("Tiling")
        }
    ]

    implicitHeight: tileRow.implicitHeight

    RowLayout {
        id: tileRow

        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: root._modes

            Rectangle {
                id: tile

                required property var modelData
                required property int index
                readonly property bool selected: root.currentIndex === index

                implicitWidth: tileContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                implicitHeight: tileContent.implicitHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.smallSpacing
                color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : tileMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                border.width: Math.round(Screen.devicePixelRatio)
                border.color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : tileMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.modelData.label
                Accessible.checked: tile.selected

                ColumnLayout {
                    id: tileContent

                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: tile.modelData.icon
                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        Layout.alignment: Qt.AlignHCenter
                        opacity: tile.selected ? 1 : 0.5
                    }

                    Label {
                        text: tile.modelData.label
                        font: Kirigami.Theme.smallFont
                        Layout.alignment: Qt.AlignHCenter
                        opacity: tile.selected ? 1 : 0.5
                    }
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
