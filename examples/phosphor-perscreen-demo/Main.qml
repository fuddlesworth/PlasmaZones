// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-perscreen-demo Main.qml. PerScreen with a Window delegate;
// one small floating window per monitor. Hot-plug a display → window
// appears; unplug → window disappears.

import Phosphor.Shell
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Non-visual root. The PerScreen helper instantiates Window delegates
// directly, so we don't need a parent ApplicationWindow — the engine
// stays alive as long as the QtObject root + at least one Window
// delegate exists.
QtObject {
    id: root

    property PerScreen perScreen: PerScreen {
        model: screensModel
        delegate: Component {
            Window {
                id: screenWindow

                // PerScreen passes these in as initial properties on
                // creation. `screen` here SETS Window.screen (Window
                // has a built-in `screen` property), which anchors the
                // window to the right monitor without explicit
                // geometry wiring. The other three are required for
                // the content panel.
                required property var screen
                required property string name
                required property int index
                required property bool isPrimary

                width: 320
                height: 180
                visible: true
                title: qsTr("Screen %1: %2").arg(screenWindow.index).arg(screenWindow.name)
                color: Theme.surface_container

                // Land near the top-left of the assigned monitor so a
                // user with multiple monitors sees one window per
                // monitor in the same corner — easy to count
                // visually. `screen.virtualX/virtualY` would be more
                // precise on multi-monitor wall-layouts, but QScreen
                // doesn't expose that as a QML property; the small
                // pixel offsets are fine for a demo.
                x: 40
                y: 40

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Tokens.spacing_l
                    spacing: Tokens.spacing_m

                    Text {
                        color: Theme.on_surface
                        font.family: Tokens.font_family
                        font.pixelSize: Tokens.font_size_title_m
                        font.weight: Tokens.font_weight_medium
                        text: screenWindow.name
                    }

                    RowLayout {
                        spacing: Tokens.spacing_s

                        Text {
                            color: Theme.on_surface_variant
                            font.family: Tokens.font_family
                            font.pixelSize: Tokens.font_size_body_s
                            text: qsTr("index %1").arg(screenWindow.index)
                        }

                        Rectangle {
                            visible: screenWindow.isPrimary
                            Layout.preferredHeight: Tokens.spacing_l
                            color: Theme.primary
                            radius: Tokens.radius_s

                            Text {
                                anchors.centerIn: parent
                                anchors.margins: Tokens.spacing_s
                                color: Theme.on_primary
                                font.family: Tokens.font_family
                                font.pixelSize: Tokens.font_size_label_s
                                font.weight: Tokens.font_weight_medium
                                text: qsTr("PRIMARY")
                            }
                            // Pad the badge horizontally around the
                            // text. Computing implicitWidth from the
                            // Text avoids the badge clipping its
                            // own label on long locales.
                            implicitWidth: badgeLabel.implicitWidth + Tokens.spacing_l * 2

                            Text {
                                id: badgeLabel

                                visible: false
                                font.family: Tokens.font_family
                                font.pixelSize: Tokens.font_size_label_s
                                font.weight: Tokens.font_weight_medium
                                text: qsTr("PRIMARY")
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Text {
                        Layout.fillWidth: true
                        color: Theme.on_surface_variant
                        font.family: Tokens.font_family
                        font.pixelSize: Tokens.font_size_body_s
                        text: qsTr("Plug or unplug a monitor — the window for that monitor appears / disappears, and the windows on monitors that didn't change keep their identity (PerScreen reuses delegates across model resets).")
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
