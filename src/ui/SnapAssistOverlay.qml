// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Snap Assist Overlay - Aero Snap style window picker
 *
 * Displays empty zones with candidate window cards positioned inside each zone.
 * Each zone shows all available candidates (unsnapped including floated windows).
 * User clicks a candidate in a zone to snap that window to that zone.
 */
Window {
    id: root

    property var emptyZones: []
    property var candidates: []
    property int screenWidth: 1920
    property int screenHeight: 1080

    signal windowSelected(string windowId, string zoneId, string geometryJson)

    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"

    // Dismiss on Escape
    Shortcut {
        sequence: "Escape"
        onActivated: root.close()
    }

    // Backdrop - semi-transparent dim, click outside to close
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.25)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    // Each zone shows all candidates; user picks any window to snap to any zone
    Repeater {
        model: root.emptyZones

        Item {
            id: zoneContainer

            property var zone: modelData

            x: zone ? zone.x : 0
            y: zone ? zone.y : 0
            width: zone ? zone.width : 0
            height: zone ? zone.height : 0

            visible: zone && zone.zoneId && root.candidates.length > 0

            // Zone background - matches main overlay (no inner margins, use zone borderRadius)
            Rectangle {
                id: zoneBg
                anchors.fill: parent
                radius: (zone && zone.borderRadius !== undefined) ? zone.borderRadius : Kirigami.Units.gridUnit
                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g,
                              Kirigami.Theme.backgroundColor.b, 0.6)
                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                    Kirigami.Theme.textColor.b, 0.3)
                border.width: (zone && zone.borderWidth !== undefined) ? zone.borderWidth : 1
            }

            // Grid of candidate cards inside zone - centered, scaled like zone numbers
            Flow {
                id: candidateFlow

                readonly property real zoneSize: Math.min(zoneContainer.width, zoneContainer.height) || 1
                readonly property real cardScale: 0.35 / Math.max(1, Math.sqrt(root.candidates.length))
                readonly property real cardBaseSize: zoneSize * cardScale
                readonly property real iconSize: Math.max(24, cardBaseSize * 0.6)
                readonly property real cardWidth: Math.max(100, cardBaseSize * 2.2)
                // Scale font like ZoneItem zone name: base on theme, scale with zone size (200px reference)
                readonly property real fontPixelSize: {
                    var baseSize = Kirigami.Theme.defaultFont.pixelSize
                    var scaleFactor = zoneSize / 200
                    var scaledSize = baseSize * Math.max(0.4, Math.min(1, scaleFactor))
                    return Math.max(8, Math.round(scaledSize))
                }

                readonly property real flowWidth: zoneContainer.width - Kirigami.Units.smallSpacing * 2
                readonly property real cardTotalWidth: cardWidth + Kirigami.Units.smallSpacing * 2
                readonly property real flowSpacing: Math.max(2, Math.min(8, zoneSize * 0.02))
                readonly property int itemsPerRow: Math.max(1, Math.floor((flowWidth + flowSpacing) / (cardTotalWidth + flowSpacing)))
                readonly property real contentWidth: {
                    var n = root.candidates.length
                    if (n <= 0) return 0
                    var perRow = itemsPerRow
                    if (n <= perRow) {
                        return n * cardTotalWidth + (n - 1) * flowSpacing
                    }
                    return perRow * cardTotalWidth + (perRow - 1) * flowSpacing
                }
                readonly property real centerPadding: Math.max(0, (flowWidth - contentWidth) / 2)

                width: flowWidth
                leftPadding: centerPadding
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Math.max(Kirigami.Units.smallSpacing,
                    (zoneContainer.height - candidateFlow.implicitHeight) / 2)
                spacing: flowSpacing

                Repeater {
                    model: root.candidates.length

                    Item {
                        id: candidateCard

                        property var candidate: root.candidates[index]
                        property bool hovered: cardMouse.containsMouse

                        width: candidateFlow.cardWidth + Kirigami.Units.smallSpacing * 2
                        height: cardContent.height + Kirigami.Units.smallSpacing * 2

                        Rectangle {
                            anchors.fill: parent
                            radius: Math.max(2, candidateFlow.zoneSize * 0.01)
                            color: candidateCard.hovered
                                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                                          Kirigami.Theme.highlightColor.b, 0.2)
                                : Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g,
                                          Kirigami.Theme.backgroundColor.b, 0.5)
                            border.color: candidateCard.hovered ? Kirigami.Theme.highlightColor
                                : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                    Kirigami.Theme.textColor.b, 0.2)
                            border.width: candidateCard.hovered ? 2 : 1

                            Behavior on color { ColorAnimation { duration: 150 } }
                            Behavior on border.color { ColorAnimation { duration: 150 } }
                        }

                        Row {
                            id: cardContent
                            anchors.centerIn: parent
                            width: candidateFlow.cardWidth
                            spacing: Kirigami.Units.smallSpacing

                            Item {
                                width: candidateFlow.iconSize
                                height: width

                                Image {
                                    anchors.fill: parent
                                    visible: !!(candidate && candidate.thumbnail)
                                    fillMode: Image.PreserveAspectFit
                                    source: (candidate && candidate.thumbnail) ? candidate.thumbnail : ""
                                    cache: false
                                }
                                Image {
                                    anchors.fill: parent
                                    visible: !(candidate && candidate.thumbnail) && !!(candidate && candidate.iconPng)
                                    fillMode: Image.PreserveAspectFit
                                    source: (candidate && candidate.iconPng) ? candidate.iconPng : ""
                                    cache: false
                                }
                                Kirigami.Icon {
                                    anchors.fill: parent
                                    visible: !(candidate && candidate.thumbnail) && !(candidate && candidate.iconPng)
                                    source: candidate ? (candidate.icon || "application-x-executable") : "application-x-executable"
                                }
                            }

                            Label {
                                width: parent.width - candidateFlow.iconSize - Kirigami.Units.smallSpacing
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                text: candidate ? (candidate.caption || "") : ""
                                font.pixelSize: candidateFlow.fontPixelSize
                                color: Kirigami.Theme.textColor
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.name: candidate && candidate.caption
                                ? i18n("Snap %1 to this zone", candidate.caption)
                                : i18n("Snap window to this zone")
                            ToolTip.visible: cardMouse.containsMouse
                            ToolTip.text: candidate ? candidate.caption : ""
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: {
                                const wId = candidate ? candidate.windowId : ""
                                const zoneId = zoneContainer.zone ? (zoneContainer.zone.zoneId || "") : ""
                                if (!zoneContainer.zone || !wId || !zoneId) {
                                    root.close()
                                    return
                                }
                                const z = zoneContainer.zone
                                const geo = z && z.x !== undefined && z.y !== undefined
                                    ? JSON.stringify({
                                        x: z.x,
                                        y: z.y,
                                        width: z.width,
                                        height: z.height
                                    })
                                    : "{}"
                                root.windowSelected(wId, zoneId, geo)
                                root.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
