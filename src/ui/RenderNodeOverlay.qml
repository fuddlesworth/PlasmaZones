// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common 1.0
import PlasmaZones 1.0

/**
 * Zone overlay window with QSGRenderNode-based shader rendering.
 *
 * Uses ZoneShaderItem for custom shader effects (QRhi, OpenGL 330).
 */
Window {
    id: root

    // Properties set from C++ OverlayService
    property url shaderSource
    property string bufferShaderPath: ""
    property var bufferShaderPaths: []
    property bool bufferFeedback: false
    property real bufferScale: 1.0
    property string bufferWrap: "clamp"
    // Note: Using 'var' for zones because it holds a QVariantList from C++ with heterogeneous zone data
    property var zones: []
    property int zoneCount: 0
    property int highlightedCount: 0
    // Highlight state for proximity snap / paint-to-snap (set by C++ OverlayService)
    property string highlightedZoneId: ""
    property var highlightedZoneIds: []
    // Note: Using 'var' for shaderParams because it's a QVariantMap with dynamic keys
    property var shaderParams: ({})
    property int zoneDataVersion: 0

    // Animated uniforms (updated by C++ via setProperty)
    property real iTime: 0
    property real iTimeDelta: 0
    property int iFrame: 0

    // Mouse tracking
    property point mousePosition: Qt.point(0, 0)

    // Settings properties
    property bool showNumbers: true
    property color labelFontColor: Kirigami.Theme.textColor
    property string fontFamily: ""
    property real fontSizeScale: 1.0
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    // Pre-rendered zone labels texture (set from C++ when shader overlay + showNumbers)
    property var labelsTexture

    // Audio spectrum from CAVA (0-1 per bar). Set from C++ when audio viz enabled. Empty = disabled.
    property var audioSpectrum: []

    // Appearance properties
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property int borderWidth: Kirigami.Units.smallSpacing
    property int borderRadius: Kirigami.Units.gridUnit

    // Custom shader parameters (16 floats in 4 vec4s + 4 colors)
    // These are rebuilt whenever shaderParams changes to ensure reactivity
    // Zone index under cursor for hover highlight (preview mode). -1 = none.
    // Using index avoids full zones array churn on mouse move, preventing shader restart.
    readonly property int hoveredZoneIndex: {
        if (root.highlightedCount > 0) return -1;  // Main overlay uses zone selector
        var zones = root.zones || [];
        var mouse = root.mousePosition;
        for (var i = 0; i < zones.length; i++) {
            var z = zones[i];
            var x = (z.x !== undefined) ? z.x : 0;
            var y = (z.y !== undefined) ? z.y : 0;
            var w = (z.width !== undefined) ? z.width : 0;
            var h = (z.height !== undefined) ? z.height : 0;
            if (mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h) return i;
        }
        return -1;
    }

    // Zones with isHighlighted patched for Repeater (basic zones fallback only).
    readonly property var zonesForDisplay: {
        var zones = root.zones || [];
        if (root.highlightedCount > 0) return zones;
        var idx = root.hoveredZoneIndex;
        if (idx < 0) return zones;
        var result = [];
        for (var i = 0; i < zones.length; i++) {
            var z = zones[i];
            var zone = {};
            for (var k in z) zone[k] = z[k];
            zone.isHighlighted = (i === idx);
            result.push(zone);
        }
        return result;
    }

    // Window flags - LayerShellQt handles the overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    // Note: Debug logging removed from onShaderSourceChanged/onZonesChanged to reduce log noise.
    // Monitor ZoneShaderItem.onStatusChanged for shader load status.

    Item {
        id: content
        anchors.fill: parent

        MouseArea {
            id: mouseTracker
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onPositionChanged: function(mouse) {
                root.mousePosition = Qt.point(mouse.x, mouse.y)
            }
            onExited: {
                root.mousePosition = Qt.point(-1, -1)
            }
        }

        // ===================================================================
        // ZONE SHADER RENDERER - shared with editor preview
        // ===================================================================
        QtObject {
            id: shaderConfig
            property url shaderSource: root.shaderSource
            property string bufferShaderPath: root.bufferShaderPath
            property var bufferShaderPaths: root.bufferShaderPaths
            property bool bufferFeedback: root.bufferFeedback
            property real bufferScale: root.bufferScale
            property string bufferWrap: root.bufferWrap
            property var zones: root.zones
            property int hoveredZoneIndex: root.hoveredZoneIndex
            property var shaderParams: root.shaderParams
            property real iTime: root.iTime
            property real iTimeDelta: root.iTimeDelta
            property int iFrame: root.iFrame
            property size iResolution: Qt.size(root.width, root.height)
            property point iMouse: root.mousePosition
            property var labelsTexture: root.labelsTexture
            property var audioSpectrum: root.audioSpectrum
        }

        ZoneShaderRenderer {
            id: zoneShaderRenderer
            anchors.fill: parent
            visible: root.shaderSource.toString() !== "" && status !== ZoneShaderItem.Error
            config: shaderConfig

            onShaderError: function(log) {
                console.error("RenderNodeOverlay: Shader error:", log)
                if (typeof overlayService !== "undefined") {
                    overlayService.onShaderError(log)
                }
            }
        }

        // ===================================================================
        // BASIC ZONES - When no shader or shader not ready
        // ===================================================================
        Repeater {
            model: root.zonesForDisplay
            // Note: 'visible' on Repeater has no effect - delegate visibility is controlled below

            delegate: ZoneItem {
                required property var modelData
                required property int index

                visible: root.shaderSource.toString() === "" || zoneShaderRenderer.status !== ZoneShaderItem.Ready

                x: modelData.x !== undefined ? modelData.x : 0
                y: modelData.y !== undefined ? modelData.y : 0
                width: modelData.width !== undefined ? modelData.width : 0
                height: modelData.height !== undefined ? modelData.height : 0

                zoneNumber: modelData.zoneNumber || (index + 1)
                zoneName: modelData.name || ""
                isHighlighted: modelData.isHighlighted || false
                showNumber: root.showNumbers

                highlightColor: (modelData.useCustomColors && modelData.highlightColor) ? modelData.highlightColor : root.highlightColor
                inactiveColor: (modelData.useCustomColors && modelData.inactiveColor) ? modelData.inactiveColor : root.inactiveColor
                borderColor: (modelData.useCustomColors && modelData.borderColor) ? modelData.borderColor : root.borderColor
                labelFontColor: root.labelFontColor
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                fontStrikeout: root.fontStrikeout
                activeOpacity: (modelData.useCustomColors && modelData.activeOpacity !== undefined) ? modelData.activeOpacity : root.activeOpacity
                inactiveOpacity: (modelData.useCustomColors && modelData.inactiveOpacity !== undefined) ? modelData.inactiveOpacity : root.inactiveOpacity
                borderWidth: (modelData.useCustomColors && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth
                borderRadius: (modelData.useCustomColors && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius
            }
        }

        // Shader error - fail visibly, don't mask with fallback
        Rectangle {
            visible: root.shaderSource.toString() !== "" && zoneShaderRenderer.status === ZoneShaderItem.Error
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.5, 400)
            height: shaderErrorText.implicitHeight + Kirigami.Units.gridUnit * 2
            color: Kirigami.Theme.backgroundColor
            opacity: 0.95
            radius: Kirigami.Units.smallSpacing
            border.color: Kirigami.Theme.negativeTextColor
            border.width: 1
            Text {
                id: shaderErrorText
                anchors.centerIn: parent
                text: zoneShaderRenderer.errorLog || i18n("Shader error")
                color: Kirigami.Theme.textColor
                wrapMode: Text.WordWrap
                width: parent.width - Kirigami.Units.gridUnit * 2
            }
        }
    }

}
