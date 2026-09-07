// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PanelFrame, the shared chrome for a bar widget's transient.
//
// These panels are the `transient` class of A2 §4.7, not panes: they are
// what a status chip opens for a glance, they dismiss on outside click,
// and they never move anyone's windows. The control center is the pane
// (A2 §4.1); this is deliberately the lighter surface beside it.
//
// The material is the five-layer stack of 05 §5, and the two layers that
// are easiest to drop are the two that matter most:
//
//   1. ground        abyss over blur, since a floating card sits on the
//                    desktop rather than in it
//   2. inset stroke  SpectrumStroke, sampling the RAIL AXIS at the
//                    panel's own screen position — so the same panel is
//                    cyan-leaning on the left of the screen and
//                    rose-leaning on the right, and moving it re-samples
//   3. gleam         carried by the top band's SpectrumRail, which is the
//                    same travelling highlight that runs on the bar's own
//                    rail directly above (A2 §4.3 step 3)
//   4. margin dust   deliberately OFF: A1 §45 turns it off for bar,
//                    panes and transients, and a surface summoned to
//                    answer a question should not have weather
//   5. content       the rows
//
// The first cut of this file was a PhosphorCard with `elevation: 3`,
// which is an M3 tint slab: a filled ground with no stroke, no gleam and
// no relationship to the rail. R1 forbids the fill and R2 forbids the
// shadow it implies. Do not reach for a card here.
//
// Sizing: a popout's content is measured by its IMPLICIT size, so a body
// that is a ListView (no implicit height) would collapse the panel to its
// header. The frame therefore takes an explicit `panelWidth` and caps the
// body at `maxBodyHeight` so a long list scrolls rather than growing past
// the bottom of the output.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    /// Header title. A noun phrase naming what the panel is, since it sits
    /// under a chip the user just pressed.
    property string title: ""
    /// Freedesktop icon name shown before the title, or "" for none.
    property string iconName: ""
    /// Secondary line: the one-glance summary. "" hides it.
    property string subtitle: ""

    /// Fixed content width, so moving between panels is not re-reading a
    /// differently shaped surface each time.
    property real panelWidth: 320
    /// Height the body may reach before it scrolls instead of growing.
    property real maxBodyHeight: 420

    /// Where this panel sits along the screen, as 0..1. Drives the stroke
    /// and the top band's slice, which is what binds the surface to the
    /// shared diagonal field instead of giving it a private colour.
    /// The shell sets it from the summoning chip; the default centres.
    property real railT: 0.5

    /// Optional control in the header's trailing corner: the master switch
    /// the panel's content depends on (the Wi-Fi radio, the BT adapter).
    property Component headerAction: null

    /// Body content. Declared children land in the scrolling area.
    default property alias content: body.data

    implicitWidth: root.panelWidth
    implicitHeight: shell.implicitHeight

    Item {
        id: shell

        anchors.fill: parent
        implicitHeight: layout.implicitHeight + Tokens.spacing_l * 2

        // Ground. Abyss over blur, per the floating-card row of 05 §5:
        // the windows behind stay legible, which is what makes the panel
        // read as placed on the desktop rather than pasted over it.
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius_l
            color: Theme.isDark ? Qt.rgba(0.027, 0.059, 0.133, 0.94) : Qt.rgba(0.96, 0.976, 1, 0.94)
        }

        // Inset stroke, sampling the rail axis at this panel's position.
        SpectrumStroke {
            anchors.fill: parent
            radius: Tokens.radius_l
            t: root.railT
        }

        // The top band IS the rail over this panel's x-range, so the panel
        // and the bar directly above it match hue for hue. It also carries
        // the gleam, which is the layer that ties this surface to every
        // other one the same engine paints.
        SpectrumRail {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Tokens.radius_l
            anchors.rightMargin: Tokens.radius_l
            thickness: 2
            gleam: true
            // A narrow window of the ramp centred on where this panel is,
            // rather than the whole cyan→rose axis: the band is a slice of
            // one screen-wide gradient, never a gradient of its own (R1).
            sliceStart: Math.max(0, root.railT - 0.09)
            sliceEnd: Math.min(1, root.railT + 0.09)
        }

        ColumnLayout {
            id: layout

            x: Tokens.spacing_l
            y: Tokens.spacing_l
            width: root.panelWidth - Tokens.spacing_l * 2
            spacing: Tokens.spacing_m

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_s

                Kirigami.Icon {
                    visible: root.iconName !== ""
                    // Layout.preferredWidth, not width: a layout owns its
                    // items' geometry and a plain width binding is
                    // overwritten on the next pass.
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    source: root.iconName
                    isMask: true
                    color: Theme.on_surface
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        text: root.title
                        color: Theme.on_surface
                        font.pixelSize: Tokens.font_size_title_s
                        font.family: Tokens.font_family_ui
                        font.weight: Tokens.font_weight_medium
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.subtitle !== ""
                        text: root.subtitle
                        color: Theme.on_surface_variant
                        font.pixelSize: Tokens.font_size_body_s
                        font.family: Tokens.font_family_ui
                        elide: Text.ElideRight
                    }
                }

                Loader {
                    Layout.alignment: Qt.AlignVCenter
                    active: root.headerAction !== null
                    sourceComponent: root.headerAction
                }
            }

            // The body's height is stated, not implied: a Flickable derives
            // none from its contents, so an unstated one shows nothing and
            // an uncapped one pushes the panel off the bottom of the output.
            Flickable {
                id: scroller

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(body.implicitHeight, root.maxBodyHeight)
                contentWidth: width
                contentHeight: body.implicitHeight
                clip: true
                // Only scrollable when there is something to scroll to, so a
                // short panel does not rubber-band under the pointer.
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: body

                    width: scroller.width
                    spacing: Tokens.spacing_xs
                }
            }
        }
    }
}
