// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Notifications.Toast, a single notification toast card.
//
// An elevated card with an optional image/avatar, the app name, a
// summary, and a rich-text body, plus a close affordance. It owns its
// own auto-dismiss timer, which pauses while the pointer hovers (so a
// toast the user is reading does not vanish). Position/stacking is the
// host's job (ToastHost); this is just the card.
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
    // Rich text (StyledText subset), matching the freedesktop notification
    // body markup.
    property string body: ""
    property url imageSource: ""
    // 0 low, 1 normal, 2 critical. Critical gets an error accent stripe.
    property int urgency: 1
    // Auto-dismiss after this many ms; 0 disables (sticky).
    property int timeout: 5000

    signal dismissed

    // Exposed so a host can coordinate; also gates the dismiss timer.
    readonly property bool hovered: hover.hovered

    implicitWidth: 360
    implicitHeight: Math.max(72, row.implicitHeight + 2 * Tokens.spacing_m)

    HoverHandler {
        id: hover
    }

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_l
        color: Theme.surface_container_high
        layer.enabled: true
        layer.effect: ElevationShadow {
            level: 3
        }
    }

    // Critical-urgency accent stripe.
    Rectangle {
        visible: toast.urgency >= 2
        width: Tokens.spacing_xs
        radius: width / 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.spacing_s
        color: Theme.error
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.margins: Tokens.spacing_m
        anchors.leftMargin: toast.urgency >= 2 ? Tokens.spacing_xl : Tokens.spacing_m
        spacing: Tokens.spacing_m

        // Image / avatar, shown only when a source is set.
        Rectangle {
            visible: String(toast.imageSource) !== ""
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Layout.alignment: Qt.AlignTop
            radius: Tokens.radius_s
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
                font.pixelSize: Tokens.font_size_label_s
                font.weight: Tokens.font_weight_medium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                visible: toast.summary !== ""
                text: toast.summary
                color: Theme.on_surface
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
                font.pixelSize: Tokens.font_size_body_m
                textFormat: Text.StyledText
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        // Close affordance.
        Item {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            Layout.alignment: Qt.AlignTop

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.on_surface
                opacity: closeHover.hovered ? StateLayer.hover : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: Motion.duration_short_2
                    }
                }
            }

            Shape {
                anchors.centerIn: parent
                width: 12
                height: 12
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.on_surface_variant
                    strokeWidth: 1.6
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

    // Auto-dismiss. Hovering stops the timer; leaving restarts the full
    // timeout (a simple, lossless-enough "hover keeps it open"). A zero
    // timeout is sticky (the user must close it).
    Timer {
        interval: toast.timeout
        running: toast.timeout > 0 && !toast.hovered
        repeat: false
        onTriggered: toast.dismissed()
    }
}
