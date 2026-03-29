// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable template card for wizard dialogs.
 *
 * Provides hover/selection chrome, animations, and a selected badge.
 * The preview area is supplied via the default content property.
 */
Item {
    id: root

    required property string templateName
    required property string templateDesc
    required property bool selected
    default property alias previewContent: previewArea.data
    property bool isHovered: false

    signal clicked()
    signal doubleClicked()

    Layout.fillWidth: true
    Layout.preferredHeight: Kirigami.Units.gridUnit * 10
    Accessible.name: root.templateName
    Accessible.description: root.templateDesc
    Accessible.role: Accessible.Button

    HoverHandler {
        onHoveredChanged: root.isHovered = hovered
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: false
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onDoubleClicked: root.doubleClicked()
    }

    Rectangle {
        id: templateCard

        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing * 2
        color: {
            if (root.selected)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15);

            if (root.isHovered)
                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03);
        }
        border.width: root.selected ? Math.round(Kirigami.Units.devicePixelRatio * 2) : Math.round(Kirigami.Units.devicePixelRatio)
        border.color: {
            if (root.selected)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6);

            if (root.isHovered)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
        }
        transform: [
            Scale {
                origin.x: templateCard.width / 2
                origin.y: templateCard.height / 2
                xScale: root.isHovered ? 1.02 : 1
                yScale: root.isHovered ? 1.02 : 1

                Behavior on xScale {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                }

                Behavior on yScale {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                }

            },
            Translate {
                y: root.isHovered ? -1 : 0

                Behavior on y {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                }

            }
        ]

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing * 2
            spacing: Kirigami.Units.smallSpacing

            Item {
                id: previewArea

                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Label {
                Layout.fillWidth: true
                text: root.templateName
                font.weight: root.selected ? Font.Bold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: root.templateDesc
                font: Kirigami.Theme.smallFont
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.5
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }

        }

        // Selected badge
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Kirigami.Units.smallSpacing
            width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
            height: width
            radius: width / 2
            color: Kirigami.Theme.highlightColor
            visible: root.selected

            Kirigami.Icon {
                anchors.centerIn: parent
                source: "checkmark"
                width: Kirigami.Units.iconSizes.small
                height: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.highlightedTextColor
            }

        }

        Behavior on color {
            ColorAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        Behavior on border.color {
            ColorAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        Behavior on border.width {
            NumberAnimation {
                duration: 150
            }

        }

    }

}
