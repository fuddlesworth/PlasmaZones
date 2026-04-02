// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: root

    title: i18n("What's New in PlasmaZones %1", Qt.application.version)
    preferredWidth: Kirigami.Units.gridUnit * 28
    preferredHeight: Kirigami.Units.gridUnit * 26
    standardButtons: Dialog.NoButton
    padding: Kirigami.Units.largeSpacing
    onOpened: settingsController.markWhatsNewSeen()

    ListView {
        id: listView

        model: settingsController.whatsNewEntries
        spacing: Kirigami.Units.largeSpacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            id: releaseCard

            required property var modelData
            required property int index

            width: listView.width
            implicitHeight: cardColumn.implicitHeight
            radius: Kirigami.Units.smallSpacing * 1.5
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
            border.width: Math.round(Kirigami.Units.devicePixelRatio)
            border.color: cardHover.hovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

            HoverHandler {
                id: cardHover
            }

            ColumnLayout {
                id: cardColumn

                width: parent.width
                spacing: 0

                // Header with version and date
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: headerRow.implicitHeight
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
                    radius: releaseCard.radius

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.radius
                        color: parent.color
                    }

                    RowLayout {
                        id: headerRow

                        width: parent.width
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            text: releaseCard.modelData.version
                            level: 3
                            padding: Kirigami.Units.smallSpacing
                            leftPadding: Kirigami.Units.largeSpacing
                        }

                        Label {
                            text: releaseCard.modelData.date
                            opacity: 0.5
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            Layout.alignment: Qt.AlignBaseline
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                    }

                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                }

                // Highlights
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: releaseCard.modelData.highlights

                        RowLayout {
                            required property string modelData

                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: "\u2022"
                                opacity: 0.4
                                Layout.alignment: Qt.AlignTop
                            }

                            Label {
                                Layout.fillWidth: true
                                text: parent.modelData
                                wrapMode: Text.WordWrap
                            }

                        }

                    }

                }

            }

            Behavior on border.color {
                ColorAnimation {
                    duration: 200
                    easing.type: Easing.OutCubic
                }

            }

            transform: Translate {
                y: cardHover.hovered ? -1 : 0

                Behavior on y {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                }

            }

        }

    }

}
