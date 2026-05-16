// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Step indicator for multi-step wizard dialogs.
 *
 * Displays numbered step dots with labels and connector lines.
 */
RowLayout {
    id: root

    required property var stepLabels
    required property int currentStep

    Layout.alignment: Qt.AlignHCenter
    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: root.stepLabels

        delegate: RowLayout {
            id: stepIndicator

            required property int index
            required property string modelData

            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                width: Kirigami.Units.gridUnit * 1.5
                height: width
                radius: width / 2
                color: {
                    if (stepIndicator.index === root.currentStep)
                        return Kirigami.Theme.highlightColor;

                    if (stepIndicator.index < root.currentStep)
                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4);

                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15);
                }

                Label {
                    anchors.centerIn: parent
                    text: (stepIndicator.index + 1).toString()
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.weight: Font.Bold
                    color: stepIndicator.index <= root.currentStep ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                    opacity: stepIndicator.index <= root.currentStep ? 1 : 0.4
                }

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.progress"
                    }

                }

            }

            Label {
                text: stepIndicator.modelData
                font.weight: stepIndicator.index === root.currentStep ? Font.DemiBold : Font.Normal
                opacity: stepIndicator.index <= root.currentStep ? 1 : 0.4

                Behavior on opacity {
                    PhosphorMotionAnimation {
                        profile: "widget.progress"
                    }

                }

            }

            Rectangle {
                readonly property bool completed: root.currentStep > stepIndicator.index

                visible: stepIndicator.index < root.stepLabels.length - 1
                Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                Layout.preferredHeight: Math.round(Kirigami.Units.devicePixelRatio * 2)
                radius: height / 2
                color: completed ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.progress"
                    }

                }

            }

        }

    }

}
