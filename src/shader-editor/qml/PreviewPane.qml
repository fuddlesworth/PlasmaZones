// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PlasmaZones 1.0

Item {
    id: root

    // Bound from C++ context property; null until QQuickWidget finishes loading
    readonly property var pc: previewController ?? null

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.pc !== null

        // Preview header bar
        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    text: qsTr("Live Preview")
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: (root.pc ? root.pc.fps : 0) + " FPS"
                    font.family: "monospace"
                    opacity: 0.7
                    visible: root.pc ? root.pc.animating : false
                }

                ToolButton {
                    icon.name: (root.pc && root.pc.animating) ? "media-playback-pause" : "media-playback-start"
                    onClicked: { if (root.pc) root.pc.animating = !root.pc.animating }
                    ToolTip.text: (root.pc && root.pc.animating) ? qsTr("Pause") : qsTr("Play")
                    ToolTip.visible: hovered
                }

                ToolButton {
                    icon.name: "view-refresh"
                    onClicked: { if (root.pc) root.pc.resetTime() }
                    ToolTip.text: qsTr("Reset time")
                    ToolTip.visible: hovered
                }

                ToolButton {
                    icon.name: "view-grid"
                    onClicked: { if (root.pc) root.pc.cycleZoneLayout() }
                    ToolTip.text: root.pc ? root.pc.zoneLayoutName : ""
                    ToolTip.visible: hovered
                }

                ToolButton {
                    icon.name: "view-refresh-symbolic"
                    onClicked: { if (root.pc) root.pc.recompile() }
                    ToolTip.text: qsTr("Force recompile")
                    ToolTip.visible: hovered
                }
            }
        }

        // Preview area
        Rectangle {
            id: previewBackground
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0a0a1a"

            onWidthChanged: if (root.pc && width > 0 && height > 0) root.pc.setPreviewSize(Math.floor(width), Math.floor(height))
            onHeightChanged: if (root.pc && width > 0 && height > 0) root.pc.setPreviewSize(Math.floor(width), Math.floor(height))

            ZoneShaderItem {
                id: shaderPreview
                anchors.fill: parent
                visible: root.pc && root.pc.shaderSource.toString() !== ""

                // Render to a private layer FBO so multipass shaders work correctly
                layer.enabled: true
                // Our render node outputs correct top-down orientation;
                // disable default MirrorVertically to prevent double-flip.
                layer.textureMirroring: ShaderEffectSource.NoMirroring

                zones: root.pc ? root.pc.zones : []
                shaderParams: root.pc ? root.pc.shaderParams : ({})

                iTime: root.pc ? root.pc.iTime : 0
                iTimeDelta: root.pc ? root.pc.iTimeDelta : 0
                iFrame: root.pc ? root.pc.iFrame : 0
                iResolution: Qt.size(width, height)

                // shaderSource bound last (ZoneShaderItem compiles on shaderSource change
                // and reads other properties at that point)
                shaderSource: root.pc ? root.pc.shaderSource : ""

                onStatusChanged: {
                    // Only forward terminal states — skip Loading (1) to avoid
                    // flooding from updatePaintNode retrying broken shaders every frame
                    if (root.pc && (shaderPreview.status === 2 || shaderPreview.status === 3))
                        root.pc.onShaderStatus(shaderPreview.status, shaderPreview.errorLog)
                }
                onErrorLogChanged: {
                    // Re-forward when errorLog clears after status already transitioned to Ready
                    // (status and errorLog are separate property notifications)
                    if (root.pc && shaderPreview.status === 2)
                        root.pc.onShaderStatus(shaderPreview.status, shaderPreview.errorLog)
                }
            }

            // Bind labelsTexture separately with a guard
            Binding {
                target: shaderPreview
                property: "labelsTexture"
                value: root.pc ? root.pc.labelsTexture : undefined
                when: root.pc && root.pc.labelsTexture && root.pc.labelsTexture.width > 0
            }

            // Status overlay when no shader loaded
            Label {
                anchors.centerIn: parent
                visible: !root.pc || root.pc.shaderSource.toString() === ""
                text: qsTr("No shader loaded")
                color: "#666666"
                font.pixelSize: 14
            }
        }
    }
}
