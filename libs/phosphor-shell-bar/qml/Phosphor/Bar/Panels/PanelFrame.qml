// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PanelFrame, the shared chrome for a bar widget's panel.
//
// Every status chip that opens something opens the same shape: a card
// hanging under the bar, a titled header with an optional trailing
// control, and a scrolling body. This type is that shape, so the six
// panels carry only their own content.
//
// Sizing is the part worth reading. A popout's content item is measured by
// its implicit size (PopoutHost binds the content frame to it), so a panel
// whose body is a ListView — which has no implicit height at all — would
// collapse to its header. The frame therefore takes an explicit
// `panelWidth` and derives its height from the content, capped at
// `maxBodyHeight` so a hundred access points scroll rather than growing a
// card taller than the screen.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    /// Header title. A noun phrase naming what the panel is, since it sits
    /// under a chip the user just pressed and does not need to re-explain
    /// the gesture.
    property string title: ""
    /// Freedesktop icon name shown before the title, or "" for none.
    property string iconName: ""
    /// Secondary line under the title: the one-glance summary (what is
    /// connected, what is playing). "" hides it.
    property string subtitle: ""

    /// Fixed content width. Bar panels are all one width so a person moving
    /// between them is not re-reading a differently shaped card each time.
    property real panelWidth: 320
    /// Height the body may reach before it scrolls instead of growing.
    property real maxBodyHeight: 420

    /// Optional control in the header's trailing corner: the master switch
    /// a panel's whole content depends on (the Wi-Fi radio, the Bluetooth
    /// adapter). A Component, instantiated into the header row.
    property Component headerAction: null

    /// Body content. Declared children land in the scrolling area.
    default property alias content: body.data

    implicitWidth: root.panelWidth
    implicitHeight: card.implicitHeight

    PhosphorCard {
        id: card

        anchors.fill: parent
        elevation: 3

        ColumnLayout {
            width: root.panelWidth - card.padding * 2
            spacing: Tokens.spacing_m

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_s

                Kirigami.Icon {
                    visible: root.iconName !== ""
                    // Layout.preferredWidth, not width: an Icon in a
                    // RowLayout has its width managed by the layout, and a
                    // plain width binding is overwritten on the next pass.
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

            // The body's own height, capped. Flickable does not derive one
            // from its contentItem, so this is stated rather than implied:
            // an uncapped body would let a long device list push the card
            // off the bottom of the screen, and a Flickable with no height
            // at all would show nothing.
            Flickable {
                id: scroller

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(body.implicitHeight, root.maxBodyHeight)
                contentWidth: width
                contentHeight: body.implicitHeight
                clip: true
                // Only scrollable when there is something to scroll to, so
                // a short panel does not rubber-band under the pointer.
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
