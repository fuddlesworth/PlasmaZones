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
        iMouse: root.pc ? root.pc.mousePos : Qt.point(0, 0)
        hoveredZoneIndex: root.pc ? root.pc.hoveredZoneIndex : -1

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

    // ── Mouse tracking — only in the shader render area (between header and info bar) ──
    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerBar.bottom
        anchors.bottom: infoBar.top
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            if (root.pc) root.pc.mousePos = Qt.point(mouse.x, mouse.y + headerBar.height);
        }
        onExited: {
            if (root.pc) root.pc.mousePos = Qt.point(-1, -1);
        }
    }

    Connections {
        target: root.pc
        function onLabelsTextureChanged() {
            shaderPreview.labelsTexture = root.pc.labelsTexture;
        }
        function onAudioSpectrumChanged() {
            shaderPreview.audioSpectrum = root.pc.audioSpectrum;
        }
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

            ToolButton {
                icon.name: "label"
                checkable: true
                checked: root.pc ? root.pc.showLabels : true
                onToggled: { if (root.pc) root.pc.showLabels = checked }
                ToolTip.text: checked ? i18n("Hide zone labels") : i18n("Show zone labels")
                ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            ToolButton {
                icon.name: "audio-volume-high"
                checkable: true
                checked: root.pc ? root.pc.audioEnabled : false
                onToggled: { if (root.pc) root.pc.audioEnabled = checked }
                ToolTip.text: checked ? i18n("Disable test audio") : i18n("Enable test audio")
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
                var s = "t = " + t + "s | " + w + "\u00d7" + h + " | " + zones + " zones";
                var mp = root.pc.mousePos;
                if (mp && mp.x >= 0 && mp.y >= 0)
                    s += " | mouse " + Math.floor(mp.x) + "," + Math.floor(mp.y);
                return s;
            }
        }
    }
}
