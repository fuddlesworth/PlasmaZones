// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Shared, bounded chrome for bar popouts.

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
    property real panelWidth: Appearance.panelWidth
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
        implicitHeight: header.implicitHeight + Math.min(body.implicitHeight, root.maxBodyHeight) + layout.spacing + Appearance.padding * 2

        ShellSurface {
            anchors.fill: parent
            railT: root.railT
        }

        ColumnLayout {
            id: layout

            x: Appearance.padding
            y: Appearance.padding
            width: root.panelWidth - Appearance.padding * 2
            spacing: Tokens.spacing_m

            RowLayout {
                id: header
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
                    color: Appearance.text
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        text: root.title
                        color: Appearance.text
                        font.pixelSize: Tokens.font_size_title_s
                        font.family: Tokens.font_family_ui
                        font.weight: Tokens.font_weight_medium
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.subtitle !== ""
                        text: root.subtitle
                        color: Appearance.muted
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
                Layout.preferredHeight: Math.max(0, Math.min(body.implicitHeight, root.maxBodyHeight, root.height - header.implicitHeight - layout.spacing - 2 * Appearance.padding))
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
