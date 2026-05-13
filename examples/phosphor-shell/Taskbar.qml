// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// wlr-foreign-toplevel-management demo. Hidden when the compositor
// doesn't advertise the protocol.
PanelWindow {
    id: root

    edge: PanelWindow.Bottom
    thickness: 44
    alignment: PanelWindow.Center
    panelLength: 0
    implicitWidth: Toplevels.supported ? Math.max(taskbarRow.implicitWidth, 200) : 200
    visible: Toplevels.supported

    margins {
        bottom: 6
    }

    ShaderBackground {
        // tintOpacity
        // noiseAmount
        // noiseScale
        // animSpeed
        // cornerRadius (px)

        anchors.fill: parent
        // Stop ticking when the panel is hidden (Toplevels not supported,
        // or the wayland surface isn't visible). ShaderEffect already
        // gates per-frame work on isVisible/zero-size, but unbinding
        // playing here also short-circuits the afterAnimating connection
        // and avoids dirtying the scene at all.
        playing: root.visible
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        // Canonical slot keys (friendly names are silently dropped by
        // ShaderEffect::setShaderParams). See frosted_glass.frag for
        // the slot layout: customParams[0] = tint/noise/anim,
        // customParams[1].x = cornerRadius.
        shaderParams: {
            "customParams1_x": 0.8,
            "customParams1_y": 0.15,
            "customParams1_z": 20,
            "customParams1_w": 0.5,
            "customParams2_x": 12
        }
        customColor1: "#1e1e2e"
    }

    Row {
        id: taskbarRow

        anchors.centerIn: parent
        height: parent.height
        spacing: 6
        leftPadding: 12
        rightPadding: 12

        Repeater {
            // `Toplevels.model` is a real QAbstractListModel that emits
            // begin/endInsertRows / RemoveRows on change, so the Repeater
            // patches one delegate in/out per add/remove. Binding to
            // `Toplevels.toplevels` (the QList) would re-evaluate the
            // whole list and rebuild every delegate on every change.
            model: Toplevels.model

            delegate: Rectangle {
                // `toplevel` is the role exposed by ToplevelListModel
                // (PhosphorWayland::ForeignToplevel*).
                required property var toplevel

                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(180, Math.max(60, taskLabel.implicitWidth + 24))
                height: 32
                radius: 6
                color: toplevel.activated ? "#89b4fa" : (taskMouse.containsMouse ? "#45475a" : "#313244")
                opacity: toplevel.minimized ? 0.5 : 1

                Text {
                    id: taskLabel

                    anchors.centerIn: parent
                    anchors.margins: 8
                    width: parent.width - 16
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: toplevel.title || toplevel.appId || "(unnamed)"
                    color: toplevel.activated ? "#1e1e2e" : "#cdd6f4"
                    font.pixelSize: 11
                    font.weight: toplevel.activated ? Font.Medium : Font.Normal
                }

                MouseArea {
                    id: taskMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    Accessible.role: Accessible.Button
                    Accessible.name: toplevel.title || toplevel.appId || "(unnamed window)"
                    Accessible.description: toplevel.activated ? "Active window" : "Inactive window"
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.MiddleButton) {
                            toplevel.close();
                        } else if (toplevel.activated) {
                            toplevel.setMinimized(true);
                        } else {
                            toplevel.setMinimized(false);
                            toplevel.activate();
                        }
                    }
                }

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }

                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }

                }

            }

        }

    }

}
