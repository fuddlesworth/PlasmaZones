// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-control-center-demo, the Phase 4.4 acceptance demo.
//
// A control-center surface driven by real services: the tiles here talk
// to NetworkManager, BlueZ, PipeWire, logind and the idle ladder on this
// machine, so toggling Wi-Fi in this window toggles Wi-Fi.
//
// The backdrop is a darkened brand gradient rather than flat navy, for
// the reason the bar-canvas demo documents: a flat surface-coloured
// backdrop is near-identical to surface_container and makes a correctly
// painted tile read as a hole. A real shell sits on a wallpaper.

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.ControlCenter

Window {
    id: window

    width: 520
    height: 620
    visible: true
    title: qsTr("Phosphor Control Center")
    color: "#050916"

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#0B1730"
            }

            GradientStop {
                position: 1
                color: "#050916"
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Tokens.spacing_xl
            spacing: Tokens.spacing_l

            Text {
                text: qsTr("Control Center")
                color: Theme.on_surface
                font.family: Tokens.font_family
                font.pixelSize: Tokens.font_size_title_l
                font.weight: Tokens.font_weight_medium
            }

            // The surface under test. Elevated on its own container so the
            // grid reads as a popout body rather than as loose tiles on the
            // wallpaper.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: controlCenter.implicitHeight
                radius: Tokens.radius_xl
                color: Theme.surface_container

                layer.enabled: true
                layer.effect: ElevationShadow {
                    level: 2
                }

                ControlCenter {
                    id: controlCenter

                    anchors.fill: parent
                    provider: controlCenterController
                    tileIds: controlCenterController.tileIds
                    columns: 2

                    onTileResolved: (tileId, created) => {
                        if (!created)
                            log.append(qsTr("%1: unavailable on this machine").arg(tileId));
                    }
                    onDetailOpened: tileId => log.append(qsTr("%1: detail opened").arg(tileId))
                    onDetailClosed: tileId => log.append(qsTr("%1: detail closed").arg(tileId))
                }
            }

            // Surfaces what the registry did, so a tile that reported
            // itself unavailable is visible rather than merely absent.
            Text {
                id: log

                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.on_surface_variant
                font.family: Tokens.font_family
                font.pixelSize: Tokens.font_size_body_s
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignBottom

                function append(line: string): void {
                    text = text === "" ? line : text + "\n" + line;
                }
            }
        }
    }
}
