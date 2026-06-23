// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common 1.0

/**
 * Shader-mode zone overlay content body — Item version of the legacy
 * RenderNodeOverlay.qml, hosted inside the unified PassiveOverlayShell's
 * mainOverlay slot when shader rendering is enabled for the screen.
 */
Item {
    id: root

    property url shaderSource
    property string paramPreamble: ""
    property string bufferShaderPath: ""
    property var bufferShaderPaths: []
    property bool bufferFeedback: false
    property real bufferScale: 1
    property string bufferWrap: "clamp"
    property var zones: []
    property int zoneCount: 0
    property int highlightedCount: 0
    property string highlightedZoneId: ""
    property var highlightedZoneIds: []
    property var shaderParams: ({})
    property int zoneDataVersion: 0
    property real iTime: 0
    property real iTimeDelta: 0
    property int iFrame: 0
    property point mousePosition: Qt.point(0, 0)
    property bool showNumbers: true
    property color labelFontColor: Kirigami.Theme.textColor
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property var labelsTexture
    property var audioSpectrum: []
    property var wallpaperTexture: null
    property bool useWallpaper: false
    property bool useDepthBuffer: false
    property var bufferWraps: []
    property string bufferFilter: "linear"
    property var bufferFilters: []
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property int borderWidth: Kirigami.Units.smallSpacing
    property int borderRadius: Kirigami.Units.gridUnit
    readonly property int hoveredZoneIndex: {
        if (root.highlightedCount > 0)
            return -1;

        var zones = root.zones || [];
        var mouse = root.mousePosition;
        for (var i = 0; i < zones.length; i++) {
            var z = zones[i];
            var x = (z.x !== undefined) ? z.x : 0;
            var y = (z.y !== undefined) ? z.y : 0;
            var w = (z.width !== undefined) ? z.width : 0;
            var h = (z.height !== undefined) ? z.height : 0;
            if (mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h)
                return i;
        }
        return -1;
    }
    property bool _idled: false

    function reloadShader() {
        zoneShaderRenderer.reloadShader();
    }

    anchors.fill: parent

    Item {
        // Pre-shell, a MouseArea here wrote `mousePosition` from Qt
        // hover events. Post-shell the slot lives inside the unified
        // PassiveOverlayShell which is kbd-None and (when no modal is
        // up) input-region-empty — Qt hover events don't reach this
        // slot. C++ OverlayService::updateMousePosition is now the
        // sole writer, fed from KWin's drag-cursor D-Bus updates.
        // Keeping the MouseArea would race the C++ writes (when input
        // region is non-empty during modal popups) and clobber them
        // with stale Qt-local coordinates.

        id: content

        anchors.fill: parent
        visible: !root._idled
        Accessible.name: i18n("Zone overlay")

        QtObject {
            id: shaderConfig

            property url shaderSource: root.shaderSource
            property string paramPreamble: root.paramPreamble
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
            property var wallpaperTexture: root.wallpaperTexture
            property bool useWallpaper: root.useWallpaper
            property bool useDepthBuffer: root.useDepthBuffer
            property var bufferWraps: root.bufferWraps
            property string bufferFilter: root.bufferFilter
            property var bufferFilters: root.bufferFilters
        }

        ZoneShaderRenderer {
            id: zoneShaderRenderer

            anchors.fill: parent
            visible: root.shaderSource.toString() !== "" && status !== ZoneShaderItem.Error
            config: shaderConfig
            onShaderError: function (log) {
                console.error("RenderNodeOverlayContent: Shader error:", log);
                if (typeof overlayService !== "undefined")
                    overlayService.onShaderError(log);
            }
        }

        Repeater {
            model: root.zones

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
                isHighlighted: modelData.isHighlighted || (root.hoveredZoneIndex === index)
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

                Accessible.name: i18n("Shader error details")
                anchors.centerIn: parent
                text: zoneShaderRenderer.errorLog || i18n("Shader error")
                color: Kirigami.Theme.textColor
                wrapMode: Text.WordWrap
                width: parent.width - Kirigami.Units.gridUnit * 2
            }
        }
    }
}
