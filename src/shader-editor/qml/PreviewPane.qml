// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaZones 1.0

Item {
    id: root

    implicitWidth: 400
    implicitHeight: 400

    readonly property var pc: previewController ?? null

    // Heights of the overlay bars — used by C++ to offset zones
    readonly property int headerHeight: 36
    readonly property int infoBarHeight: 24

    // ── Dark background ──
    Rectangle {
        anchors.fill: parent
        color: "#0a0a1a"
    }

    // ── Shader preview — fills entire widget so the render node's
    //    fullscreen-quad viewport matches the render target exactly.
    //    Header and info bars render on top (declared after). ──
    ZoneShaderItem {
        id: shaderPreview
        anchors.fill: parent
        visible: root.pc && root.pc.shaderSource.toString() !== ""

        zones: root.pc ? root.pc.zones : []
        shaderParams: root.pc ? root.pc.shaderParams : ({})

        iTime: root.pc ? root.pc.iTime : 0
        iTimeDelta: root.pc ? root.pc.iTimeDelta : 0
        iFrame: root.pc ? root.pc.iFrame : 0
        iResolution: Qt.size(width, height)

        shaderSource: root.pc ? root.pc.shaderSource : ""

        onWidthChanged: updatePreviewSize()
        onHeightChanged: updatePreviewSize()

        function updatePreviewSize() {
            if (root.pc && width > 20 && height > 20)
                root.pc.setPreviewSize(Math.floor(width), Math.floor(height));
        }

        onStatusChanged: {
            if (root.pc && (shaderPreview.status === 2 || shaderPreview.status === 3))
                root.pc.onShaderStatus(shaderPreview.status, shaderPreview.errorLog)
        }
        onErrorLogChanged: {
            if (root.pc && shaderPreview.status === 2)
                root.pc.onShaderStatus(shaderPreview.status, shaderPreview.errorLog)
        }
    }

    Binding {
        target: shaderPreview
        property: "labelsTexture"
        value: root.pc ? root.pc.labelsTexture : undefined
        when: root.pc && root.pc.labelsTexture && root.pc.labelsTexture.width > 0
    }

    Label {
        anchors.centerIn: parent
        visible: !root.pc || root.pc.shaderSource.toString() === ""
        text: qsTr("No shader loaded")
        color: "#666666"
        font.pixelSize: 14
    }

    // ── Header bar (renders ON TOP of shader) ──
    Rectangle {
        id: headerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.headerHeight
        color: "#252526"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Label {
                text: "Live Preview"
                font.pixelSize: 13
                font.bold: true
                color: "#cccccc"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: (root.pc ? root.pc.fps : 0) + " FPS"
                font.family: "monospace"
                font.pixelSize: 12
                color: "#6a9955"
                visible: root.pc ? root.pc.animating : false
            }

            ToolButton {
                implicitWidth: Kirigami.Units.gridUnit * 2; implicitHeight: Kirigami.Units.gridUnit * 2
                icon.name: (root.pc && root.pc.animating) ? "media-playback-pause" : "media-playback-start"
                icon.width: Kirigami.Units.iconSizes.smallMedium; icon.height: Kirigami.Units.iconSizes.smallMedium
                onClicked: { if (root.pc) root.pc.animating = !root.pc.animating }
                ToolTip.text: (root.pc && root.pc.animating) ? qsTr("Pause") : qsTr("Play")
                ToolTip.visible: hovered; ToolTip.delay: 500
            }

            ToolButton {
                implicitWidth: Kirigami.Units.gridUnit * 2; implicitHeight: Kirigami.Units.gridUnit * 2
                icon.name: "view-refresh"
                icon.width: Kirigami.Units.iconSizes.smallMedium; icon.height: Kirigami.Units.iconSizes.smallMedium
                onClicked: { if (root.pc) root.pc.resetTime() }
                ToolTip.text: qsTr("Reset time")
                ToolTip.visible: hovered; ToolTip.delay: 500
            }

            ToolButton {
                implicitWidth: Kirigami.Units.gridUnit * 2; implicitHeight: Kirigami.Units.gridUnit * 2
                icon.name: "view-grid"
                icon.width: Kirigami.Units.iconSizes.smallMedium; icon.height: Kirigami.Units.iconSizes.smallMedium
                onClicked: { if (root.pc) root.pc.cycleZoneLayout() }
                ToolTip.text: root.pc ? root.pc.zoneLayoutName : ""
                ToolTip.visible: hovered; ToolTip.delay: 500
            }

            ToolButton {
                implicitWidth: Kirigami.Units.gridUnit * 2; implicitHeight: Kirigami.Units.gridUnit * 2
                icon.name: "run-build"
                icon.width: Kirigami.Units.iconSizes.smallMedium; icon.height: Kirigami.Units.iconSizes.smallMedium
                onClicked: { if (root.pc) root.pc.recompile() }
                ToolTip.text: qsTr("Force recompile")
                ToolTip.visible: hovered; ToolTip.delay: 500
            }
        }
    }

    // ── Info bar (renders ON TOP of shader) ──
    Rectangle {
        id: infoBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.infoBarHeight
        color: "#1a1a2e"

        Label {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            font.family: "monospace"
            font.pixelSize: 11
            color: "#888888"
            text: {
                if (!root.pc) return "";
                var t = root.pc.iTime ? root.pc.iTime.toFixed(2) : "0.00";
                var w = Math.floor(shaderPreview.width);
                var h = Math.floor(shaderPreview.height);
                var zones = root.pc.zones ? root.pc.zones.length : 0;
                return "t = " + t + "s | " + w + "\u00d7" + h + " | " + zones + " zones";
            }
        }
    }
}
