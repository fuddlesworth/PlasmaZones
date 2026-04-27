// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Custom exclusive button group with accent-colored active state.
 *
 * A row of options where one is selected at a time, with a sliding
 * accent background indicator. Matches the SettingsSwitch aesthetic.
 *
 * Usage:
 *   SettingsButtonGroup {
 *       model: [i18n("Snapping"), i18n("Tiling")]
 *       currentIndex: 0
 *       onIndexChanged: (index) => { ... }
 *   }
 */
Row {
    id: root

    property var model: []
    property int currentIndex: 0

    signal indexChanged(int index)

    spacing: Kirigami.Units.smallSpacing / 2

    Repeater {
        model: root.model

        delegate: Rectangle {
            id: optionDelegate

            required property int index
            required property string modelData
            readonly property bool isActive: root.currentIndex === index

            width: optionMetrics.width + Kirigami.Units.largeSpacing * 2
            height: Kirigami.Units.gridUnit * 2
            radius: Kirigami.Units.smallSpacing
            color: {
                if (isActive)
                    return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2);

                if (optionMouse.containsMouse)
                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);

                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04);
            }
            border.width: Math.round(Kirigami.Units.devicePixelRatio)
            border.color: isActive ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            Accessible.role: Accessible.RadioButton
            Accessible.name: optionDelegate.modelData
            Accessible.checked: optionDelegate.isActive

            TextMetrics {
                id: optionMetrics

                text: optionDelegate.modelData
            }

            Label {
                anchors.centerIn: parent
                text: optionDelegate.modelData
                font.weight: Font.Normal
                opacity: optionDelegate.isActive ? 1 : 0.5

                Behavior on opacity {
                    PhosphorMotionAnimation {
                        profile: "widget.press"
                        durationOverride: 150
                    }

                }

            }

            MouseArea {
                id: optionMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: {
                    if (root.currentIndex !== optionDelegate.index) {
                        root.currentIndex = optionDelegate.index;
                        root.indexChanged(optionDelegate.index);
                    }
                }
            }

            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.press"
                    durationOverride: 150
                }

            }

            Behavior on border.color {
                PhosphorMotionAnimation {
                    profile: "widget.press"
                    durationOverride: 150
                }

            }

        }

    }

}
