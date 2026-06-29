// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-bar-canvas-demo, the Phase 3.2 acceptance demo.
//
// A floating bar with a "Control Center" button. Toggling it opens a
// popout that grows out of the bar as one continuous painted surface,
// with the concave/inverted corner join from the mockups
// (docs/phosphor-shell-design/mockups/control-center.svg). The toggle
// routes through a real PopoutController (PopoutService, Phase 1.2): the
// socket animation binds to the controller's open-state, and the popout
// content reuses the Phase 3.1 atoms.

import Phosphor.Theme
import Phosphor.Widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    readonly property real margin: 16

    width: 960
    height: 640
    visible: true
    title: qsTr("Phosphor Bar Canvas, Connected Corner")

    // A wallpaper-like backdrop (darkened brand gradient) so the
    // surface_container bar clearly floats on it and the connected-corner
    // shape reads. A real shell sits on a photo wallpaper; a flat navy
    // backdrop here would be near-identical to surface_container and make
    // the painted pocket invisible.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.darker(Theme.brand_stop_0, 1.6)
            }
            GradientStop {
                position: 0.55
                color: Qt.darker(Theme.brand_stop_1, 1.4)
            }
            GradientStop {
                position: 1.0
                color: Qt.darker(Theme.brand_stop_2, 1.25)
            }
        }
    }

    BarCanvas {
        id: bar

        // Socket placement: a centred pocket the Control Center fills.
        readonly property real socketW: 360
        readonly property real socketX: (width - socketW) / 2
        // Animated pocket depth, driven by the PopoutService open-state.
        property real ccDepth: barController.controlCenterOpen ? 380 : 0

        x: root.margin
        y: root.margin
        width: parent.width - root.margin * 2
        height: barHeight + Math.max(0, ccDepth)
        barHeight: 48
        cornerRadius: 16
        connectorRadius: 18
        color: Theme.surface_container
        // Below ~0.5 the socket reads as closed (flat edge), so the
        // animation grows the pocket out of nothing and removes it cleanly.
        sockets: ccDepth > 0.5 ? [
            {
                "x": socketX,
                "width": socketW,
                "depth": ccDepth
            }
        ] : []

        Behavior on ccDepth {
            NumberAnimation {
                duration: Motion.duration_long_2
                easing: Motion.emphasized
            }
        }

        // Bar strip widgets (default children land in the strip band).
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 12
            spacing: 12

            Label {
                text: Qt.formatTime(new Date(), "HH:mm")
                color: Theme.on_surface
                font.pixelSize: 15
                font.weight: Font.Medium
            }

            Item {
                Layout.fillWidth: true
            }

            PhosphorPill {
                text: qsTr("Control Center")
                selected: barController.controlCenterOpen
                onClicked: barController.toggleControlCenter()
            }
        }
    }

    // Control Center popout content, drawn over the bar's pocket so the
    // painted socket and the content read as one surface. Clipped to the
    // pocket and faded in as it opens.
    Item {
        x: bar.x + bar.socketX
        y: bar.y + bar.barHeight
        width: bar.socketW
        height: Math.max(0, bar.ccDepth)
        clip: true
        opacity: barController.controlCenterOpen ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                text: qsTr("Control Center")
                color: Theme.on_surface
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10

                PhosphorPill {
                    property bool checked: true

                    text: qsTr("Wi-Fi")
                    selected: checked
                    onToggled: checked = !checked
                }

                PhosphorPill {
                    property bool checked: false

                    text: qsTr("Bluetooth")
                    selected: checked
                    onToggled: checked = !checked
                }

                PhosphorPill {
                    property bool checked: false

                    text: qsTr("Night light")
                    selected: checked
                    onToggled: checked = !checked
                }
            }

            Label {
                text: qsTr("Brightness")
                color: Theme.on_surface_variant
                font.pixelSize: 12
            }

            PhosphorSlider {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: 65
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
