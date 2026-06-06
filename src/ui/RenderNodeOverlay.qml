// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Window wrapper for the shader preview surface (editor's Shader Settings
 * dialog). The unified PassiveOverlayShell migration moved the in-overlay
 * shader path to RenderNodeOverlayContent (an Item slot inside the shell);
 * the editor preview keeps its own dedicated layer-shell window because
 * (a) it lives in its own surface to avoid multi-pass clear interference
 * with the live overlay's render pass, and (b) the editor's per-frame
 * property writes target a single QQuickWindow without coordinating with
 * the shell's per-screen slot mapping.
 *
 * This wrapper exposes the same property surface the C++
 * `OverlayService::showShaderPreview` / `updateShaderPreview` paths write
 * to, then forwards those properties verbatim into a hosted
 * RenderNodeOverlayContent. Keep this file thin — every change to the
 * shader-preview state flow should add the property here AND in
 * RenderNodeOverlayContent.qml.
 */
Window {
    id: root

    // Properties set from C++ OverlayService — mirror RenderNodeOverlayContent's
    // property surface 1:1 so writeQmlProperty calls land on the matching name.
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
    // Sentinel set by createShaderPreviewWindow's windowProperties so the
    // shader hot-reload path (settings.cpp::shadersChanged handler) can
    // distinguish a preview window from the main overlay's shell window.
    property bool isShaderOverlay: false

    /// Forward to the hosted content's reloadShader() — invoked by the
    /// hot-reload path via QMetaObject::invokeMethod on the window root.
    function reloadShader() {
        contentItem.reloadShader();
    }

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    width: Kirigami.Units.gridUnit * 15
    height: Kirigami.Units.gridUnit * 4
    visible: false

    RenderNodeOverlayContent {
        id: contentItem

        anchors.fill: parent
        shaderSource: root.shaderSource
        paramPreamble: root.paramPreamble
        bufferShaderPath: root.bufferShaderPath
        bufferShaderPaths: root.bufferShaderPaths
        bufferFeedback: root.bufferFeedback
        bufferScale: root.bufferScale
        bufferWrap: root.bufferWrap
        zones: root.zones
        zoneCount: root.zoneCount
        highlightedCount: root.highlightedCount
        highlightedZoneId: root.highlightedZoneId
        highlightedZoneIds: root.highlightedZoneIds
        shaderParams: root.shaderParams
        zoneDataVersion: root.zoneDataVersion
        iTime: root.iTime
        iTimeDelta: root.iTimeDelta
        iFrame: root.iFrame
        mousePosition: root.mousePosition
        showNumbers: root.showNumbers
        labelFontColor: root.labelFontColor
        fontFamily: root.fontFamily
        fontSizeScale: root.fontSizeScale
        fontWeight: root.fontWeight
        fontItalic: root.fontItalic
        fontUnderline: root.fontUnderline
        fontStrikeout: root.fontStrikeout
        labelsTexture: root.labelsTexture
        audioSpectrum: root.audioSpectrum
        wallpaperTexture: root.wallpaperTexture
        useWallpaper: root.useWallpaper
        useDepthBuffer: root.useDepthBuffer
        bufferWraps: root.bufferWraps
        bufferFilter: root.bufferFilter
        bufferFilters: root.bufferFilters
        highlightColor: root.highlightColor
        inactiveColor: root.inactiveColor
        borderColor: root.borderColor
        activeOpacity: root.activeOpacity
        inactiveOpacity: root.inactiveOpacity
        borderWidth: root.borderWidth
        borderRadius: root.borderRadius
    }
}
