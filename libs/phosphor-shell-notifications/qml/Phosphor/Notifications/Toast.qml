// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Notifications.Toast, a single notification card under its band.
//
// A toast is a 2 px spectrum band on the top edge of the window it
// concerns, or of the work area when it has none (purple; rose while
// critical, breathing), with the card hanging 8 px under it (A3 §3). The
// band spans the toast's full width and the card is at most 360 px,
// centred, so a host that sizes the toast to a window's width gets the
// band across the window's edge with the card under its middle. The
// card is abyss glass with a 1 px spectrum stroke, no shadow. It owns its
// auto-dismiss timer, which pauses while the pointer hovers. Position
// and stacking are the host's job (ToastHost).
//
//   Toast {
//       appName: "Mail"; summary: "New message"
//       body: "From <b>Ada</b>"; imageSource: "file:///.../avatar.png"
//       urgency: 1; timeout: 5000
//       onDismissed: ...
//   }

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

    signal dismissed

    readonly property bool hovered: hover.hovered
    readonly property bool critical: toast.urgency >= 2

    implicitWidth: 360
    implicitHeight: 2 + Tokens.spacing_s + card.implicitHeight

    Accessible.role: Accessible.Notification
    Accessible.name: [toast.appName, toast.summary].filter(s => s !== "").join(", ")

    HoverHandler {
        id: hover
    }

    // The band: purple pending, rose hot. Enters from its centre outward
    // (the host animates `bandReveal`), and breathes while critical.
    property real bandReveal: 1

    Rectangle {
        id: band

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: parent.width * Math.max(0, Math.min(1, toast.bandReveal))
        height: 2
        color: toast.critical ? Spectrum.hot : Spectrum.pending

        SequentialAnimation on opacity {
            running: toast.critical && !Motion.reducedMotion
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.55
                duration: 600
                easing: Motion.release
            }
            NumberAnimation {
                to: 1
                duration: 600
                easing: Motion.reveal
            }
        }
    }

    // Swipe to dismiss (A3 §3): a two-finger or one-finger drag to the
    // right past the threshold dismisses; short of it the card settles
    // back. Only the x axis, so the stack above never scrolls by mistake.
    readonly property int swipeThreshold: 72
    readonly property bool swiping: swipe.active

    Item {
        id: card

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: swipe.active ? Math.max(0, swipe.translation.x) : 0
        anchors.top: parent.top
        anchors.topMargin: 2 + Tokens.spacing_s
        width: Math.min(360, toast.width)
        implicitHeight: Math.max(64, row.implicitHeight + 2 * Tokens.spacing_m)
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

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius_container
            color: Theme.surface_container
            opacity: 0.92
        }
        SpectrumStroke {
            anchors.fill: parent
            radius: Tokens.radius_container
            t: toast.t
            active: toast.hovered
        }

        RowLayout {
            id: row

            anchors.fill: parent
            anchors.margins: Tokens.spacing_m
            spacing: Tokens.spacing_m

            Rectangle {
                visible: String(toast.imageSource) !== ""
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                Layout.alignment: Qt.AlignTop
                radius: Tokens.radius_edge
                clip: true
                color: Theme.surface_variant

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
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_label_s
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: toast.summary !== ""
                    text: toast.summary
                    color: Theme.on_surface
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_body_l
                    font.weight: Tokens.font_weight_demibold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: toast.body !== ""
                    text: toast.body
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_body_m
                    textFormat: Text.StyledText
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Item {
                id: closeButton

                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
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
                        strokeColor: Theme.on_surface
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

    Timer {
        interval: Math.max(0, toast.timeout)
        running: toast.timeout > 0 && !toast.hovered
        repeat: false
        onTriggered: toast.dismissed()
    }
}
