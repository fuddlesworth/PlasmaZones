// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.FocusedApp, the active window's app + title.
//
// Binds to the Toplevels singleton's `activeToplevel`. App glyph and
// title, with a 1 px underline in the rail hue of the chip's own x: the
// chip points at its window through colour rather than an arrow.
// Collapses to zero width when nothing is focused.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Shell
import Phosphor.Widgets

BarWidget {
    id: root

    property real railT: 0.15
    readonly property int maxTitleWidth: 220

    readonly property string _appTitle: Toplevels.activeToplevel ? Toplevels.activeToplevel.title : ""
    readonly property string _appId: Toplevels.activeToplevel ? Toplevels.activeToplevel.appId : ""

    available: root._appTitle.length > 0 || root._appId.length > 0
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root._appTitle.length > 0 ? root._appTitle : root._appId

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            width: 16
            height: 16
            source: root._appId
            visible: root._appId.length > 0
            anchors.verticalCenter: parent.verticalCenter
        }

        Item {
            width: title.width
            height: title.height
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: title

                Accessible.ignored: true
                text: root._appTitle
                color: Theme.on_surface
                font.pixelSize: Tokens.font_size_label_l
                font.weight: Tokens.font_weight_medium
                font.family: Tokens.font_family_ui
                elide: Text.ElideMiddle
                width: Math.min(title.implicitWidth, root.maxTitleWidth)

                // A title change crossfades rather than hard-cuts.
                Behavior on text {
                    SequentialAnimation {
                        NumberAnimation {
                            target: title
                            property: "opacity"
                            to: 0
                            duration: Motion.duration_tick
                        }
                        PropertyAction {}
                        NumberAnimation {
                            target: title
                            property: "opacity"
                            to: 1
                            duration: Motion.duration_enter_content
                            easing: Motion.reveal
                        }
                    }
                }
            }

            SpectrumUnderline {
                anchors.left: parent.left
                anchors.top: parent.bottom
                anchors.topMargin: 1
                height: 1
                length: title.width
                t: root.railT
                opacity: 0.9
            }
        }
    }
}
