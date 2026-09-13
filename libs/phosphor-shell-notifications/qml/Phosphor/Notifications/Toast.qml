// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: toast

    property string appName: ""
    property string summary: ""
    property string body: ""
    property url imageSource: ""
    // 0 low, 1 normal, 2 critical. Critical is rose and breathes.
    property int urgency: 1
    property int timeout: 5000
    // Rail-axis hue of the card's stroke, set by the host from its x.
    property real t: 0.5
    // The surface pack on the card (A1 §2.4, `shell.phosphor.notification`),
    // handed down by ToastHost from the composition root. With a chain
    // engaged the card's own stroke steps aside for the pack's.
    property Component decoration: null

    signal dismissed

    readonly property bool hovered: hover.hovered
    readonly property bool critical: toast.urgency >= 2

    implicitWidth: 360
    readonly property bool compact: !critical && appName === "" && body === "" && String(imageSource) === ""
    implicitHeight: card.implicitHeight

    Accessible.role: Accessible.Notification
    Accessible.name: [toast.appName, toast.summary].filter(s => s !== "").join(", ")

    HoverHandler {
        id: hover
    }

    // The band: purple pending, rose hot. Enters from its centre outward
    // (the host animates `bandReveal`), and breathes while critical.
    property real bandReveal: 1

    // Swipe to dismiss (A3 §3): a two-finger or one-finger drag to the
    // right past the threshold dismisses; short of it the card settles
    // back. Only the x axis, so the stack above never scrolls by mistake.
    readonly property int swipeThreshold: 72
    readonly property bool swiping: swipe.active

    DecorationSlot {
        id: decorationSlot

        anchors.fill: parent
        component: toast.decoration
        contentItem: card
        surfacePath: "shell.phosphor.notification"
        focused: toast.hovered || toast.critical
    }

    Item {
        id: card

        // The pack's capture item: the card with its text, re-rendered
        // through the chain.
        property bool shaderAnchor: true

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: swipe.active ? Math.max(0, swipe.translation.x) : 0
        anchors.top: parent.top
        anchors.topMargin: 0
        width: Math.min(toast.width, toast.compact ? summaryMetrics.advanceWidth + 40 : 360)
        implicitHeight: row.implicitHeight + 24
        height: implicitHeight
        // The card fades as it travels, and settles back on release.
        opacity: swipe.active ? Math.max(0.2, 1 - Math.max(0, swipe.translation.x) / (2 * toast.swipeThreshold)) : 1

        Behavior on anchors.horizontalCenterOffset {
            enabled: !swipe.active
            SettleAnimation {}
        }

        DragHandler {
            id: swipe

            target: null
            yAxis.enabled: false
            xAxis.minimum: 0
            onActiveChanged: {
                if (!active && translation.x >= toast.swipeThreshold)
                    toast.dismissed();
            }
        }

        ShellSurface {
            anchors.fill: parent
            railT: toast.t
            border.color: toast.critical ? Appearance.stops[3] : Appearance.outline
        }

        RowLayout {
            id: row

            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            spacing: 12

            Rectangle {
                visible: String(toast.imageSource) !== ""
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                Layout.alignment: Qt.AlignTop
                radius: Tokens.radius_edge
                clip: true
                color: Appearance.card

                Image {
                    anchors.fill: parent
                    source: toast.imageSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Tokens.spacing_xxs

                Text {
                    visible: toast.appName !== ""
                    text: toast.appName
                    // Sender-controlled. The fdo spec allows the markup subset
                    // in the body only, so everything else stays plain.
                    textFormat: Text.PlainText
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 9
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: toast.summary !== ""
                    text: toast.summary
                    // Sender-controlled, and plain text per the fdo spec.
                    textFormat: Text.PlainText
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                    font.weight: Font.Normal
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: toast.body !== ""
                    text: toast.body
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                    textFormat: Text.StyledText
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Item {
                id: closeButton

                visible: !toast.compact
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignTop

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Dismiss notification")
                Accessible.onPressAction: toast.dismissed()

                Shape {
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    preferredRendererType: Shape.CurveRenderer
                    opacity: closeHover.hovered ? 1 : 0.6

                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: Appearance.text
                        strokeWidth: 1.4
                        capStyle: ShapePath.RoundCap

                        PathSvg {
                            path: "M 1 1 L 11 11 M 11 1 L 1 11"
                        }
                    }
                }

                HoverHandler {
                    id: closeHover
                }

                TapHandler {
                    onTapped: toast.dismissed()
                }
            }
        }
    }

    TextMetrics {
        id: summaryMetrics
        font.family: Tokens.font_family_ui
        font.pixelSize: 11
        text: toast.summary
    }

    Timer {
        interval: Math.max(0, toast.timeout)
        running: toast.timeout > 0 && !toast.hovered
        repeat: false
        onTriggered: toast.dismissed()
    }
}
