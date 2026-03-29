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
    // Extracted color constants (DRY — shared across card states)
    readonly property color _highlightBg: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
    readonly property color _hoverBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06)
    readonly property color _defaultBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
    readonly property color _selectedBorder: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6)
    readonly property color _hoverBorder: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3)
    readonly property color _defaultBorder: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

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
        color: root.selected ? root._highlightBg : root.isHovered ? root._hoverBg : root._defaultBg
        border.width: root.selected ? Math.round(Kirigami.Units.devicePixelRatio * 2) : Math.round(Kirigami.Units.devicePixelRatio)
        border.color: root.selected ? root._selectedBorder : root.isHovered ? root._hoverBorder : root._defaultBorder
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
