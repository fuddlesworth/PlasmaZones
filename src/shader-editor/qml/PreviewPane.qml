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
    readonly property int headerHeight: Math.round(Kirigami.Units.gridUnit * 1.75)
    readonly property int infoBarHeight: Math.round(Kirigami.Units.gridUnit * 1.5)

    // ZoneShaderItem status constants (mirror PreviewController::Status*)
    readonly property int statusReady: 2
    readonly property int statusError: 3

    // ── Dark background ──
    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
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
            if (root.pc && (shaderPreview.status === root.statusReady || shaderPreview.status === root.statusError))
                root.pc.onShaderStatus(shaderPreview.status, shaderPreview.errorLog)
        }
        onErrorLogChanged: {
            if (root.pc && (shaderPreview.status === root.statusReady || shaderPreview.status === root.statusError))
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
        text: i18n("No shader loaded")
        color: Kirigami.Theme.disabledTextColor
        font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
    }

    // ── Header bar (renders ON TOP of shader) ──
    Rectangle {
        id: headerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.headerHeight
        color: Kirigami.Theme.alternateBackgroundColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing * 2
            anchors.rightMargin: Kirigami.Units.smallSpacing * 2
            spacing: Kirigami.Units.smallSpacing

            ToolButton {
                icon.name: (root.pc && root.pc.animating) ? "media-playback-pause" : "media-playback-start"
                onClicked: { if (root.pc) root.pc.animating = !root.pc.animating }
                ToolTip.text: (root.pc && root.pc.animating) ? i18n("Pause") : i18n("Play")
                ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            ToolButton {
                icon.name: "view-refresh"
                onClicked: { if (root.pc) root.pc.resetTime() }
                ToolTip.text: i18n("Reset time")
                ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            Item { Layout.fillWidth: true }

            Label {
                text: (root.pc ? root.pc.fps : 0) + " FPS"
                font.family: root.pc ? root.pc.fixedFontFamily : "monospace"
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                color: Kirigami.Theme.positiveTextColor
                visible: root.pc ? root.pc.animating : false
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
        color: Kirigami.Theme.alternateBackgroundColor

        Label {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing * 2
            anchors.rightMargin: Kirigami.Units.smallSpacing * 2
            verticalAlignment: Text.AlignVCenter
            font.family: root.pc ? root.pc.fixedFontFamily : "monospace"
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            color: Kirigami.Theme.disabledTextColor
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
