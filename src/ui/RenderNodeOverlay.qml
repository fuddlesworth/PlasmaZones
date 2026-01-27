// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import PlasmaZones 1.0

/**
 * Zone overlay window with QSGRenderNode-based shader rendering.
 *
 * Uses ZoneShaderItem for custom shader effects with direct OpenGL access.
 */
Window {
    id: root

    // Properties set from C++ OverlayService
    property url shaderSource
    // Note: Using 'var' for zones because it holds a QVariantList from C++ with heterogeneous zone data
    property var zones: []
    property int zoneCount: 0
    property int highlightedCount: 0
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
    property color numberColor: Kirigami.Theme.textColor

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
    property vector4d customParams1: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    property vector4d customParams2: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    property vector4d customParams3: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    property vector4d customParams4: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    property vector4d customColor1: Qt.vector4d(1.0, 1.0, 1.0, 0.0)
    property vector4d customColor2: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    property vector4d customColor3: Qt.vector4d(1.0, 1.0, 1.0, 0.0)
    property vector4d customColor4: Qt.vector4d(0.0, 0.0, 0.0, 0.0)

    // Update custom params when shaderParams changes
    onShaderParamsChanged: {
        customParams1 = internal.buildCustomParams(1)
        customParams2 = internal.buildCustomParams(2)
        customParams3 = internal.buildCustomParams(3)
        customParams4 = internal.buildCustomParams(4)
        customColor1 = internal.buildCustomColor(1)
        customColor2 = internal.buildCustomColor(2)
        customColor3 = internal.buildCustomColor(3)
        customColor4 = internal.buildCustomColor(4)
    }

    // Compatibility properties
    property string highlightedZoneId: ""
    // Note: Using 'var' for highlightedZoneIds because it's a dynamic list from C++
    property var highlightedZoneIds: []

    signal zoneClicked(int index)
    signal zoneHovered(int index)

    flags: Qt.FramelessWindowHint |
           (Qt.platform.pluginName === "wayland" ? Qt.WindowDoesNotAcceptFocus : Qt.WindowStaysOnTopHint)
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
        }

        // ===================================================================
        // ZONE SHADER ITEM - GPU-accelerated render node based rendering
        // ===================================================================
        ZoneShaderItem {
            id: zoneShaderItem
            anchors.fill: parent
            visible: root.shaderSource.toString() !== "" && status !== ZoneShaderItem.Error

            // Shader source
            shaderSource: root.shaderSource

            // Zone data
            zones: root.zones

            // Animation uniforms
            iTime: root.iTime
            iTimeDelta: root.iTimeDelta
            iFrame: root.iFrame

            // Resolution
            iResolution: Qt.size(root.width, root.height)

            // Mouse position
            iMouse: root.mousePosition

            // Custom parameters (16 floats + 4 colors)
            customParams1: root.customParams1
            customParams2: root.customParams2
            customParams3: root.customParams3
            customParams4: root.customParams4
            customColor1: root.customColor1
            customColor2: root.customColor2
            customColor3: root.customColor3
            customColor4: root.customColor4

            // Shader params map
            shaderParams: root.shaderParams

            onStatusChanged: {
                // Only log errors - success is expected, loading is transient
                if (status === ZoneShaderItem.Error) {
                    console.error("RenderNodeOverlay: Shader error:", errorLog)
                    if (typeof overlayService !== "undefined") {
                        overlayService.onShaderError(errorLog)
                    }
                }
            }

            onErrorLogChanged: {
                if (errorLog.length > 0) {
                    console.error("RenderNodeOverlay: Shader error:", errorLog)
                }
            }
        }

        // ===================================================================
        // FALLBACK RENDERING - When no shader or shader failed
        // ===================================================================
        Repeater {
            model: root.zones
            // Note: 'visible' on Repeater has no effect - delegate visibility is controlled below

            delegate: ZoneItem {
                required property var modelData
                required property int index

                visible: !zoneShaderItem.visible

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
                numberColor: root.numberColor
                activeOpacity: (modelData.useCustomColors && modelData.activeOpacity !== undefined) ? modelData.activeOpacity : root.activeOpacity
                inactiveOpacity: (modelData.useCustomColors && modelData.inactiveOpacity !== undefined) ? modelData.inactiveOpacity : root.inactiveOpacity
                borderWidth: (modelData.useCustomColors && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth
                borderRadius: (modelData.useCustomColors && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius
            }
        }

        // ===================================================================
        // ZONE LABELS - On top of everything when shader is active
        // ===================================================================
        Repeater {
            model: root.zones
            // Note: 'visible' on Repeater has no effect - delegate visibility is controlled below

            ZoneLabel {
                required property var modelData
                required property int index

                visible: zoneShaderItem.visible && root.showNumbers && width > 0 && height > 0

                x: modelData.x !== undefined ? modelData.x : 0
                y: modelData.y !== undefined ? modelData.y : 0
                width: modelData.width !== undefined ? modelData.width : 0
                height: modelData.height !== undefined ? modelData.height : 0

                zoneNumber: modelData.zoneNumber || (index + 1)
                zoneName: modelData.name || ""
                numberColor: root.numberColor
            }
        }
    }

    QtObject {
        id: internal

        // Helper to safely get a numeric value from shaderParams
        function getParam(key, defaultValue) {
            var p = root.shaderParams || {}
            if (p.hasOwnProperty(key)) {
                var val = p[key]
                // Handle various numeric types
                if (typeof val === "number") return val
                if (typeof val === "string") {
                    var parsed = parseFloat(val)
                    return isNaN(parsed) ? defaultValue : parsed
                }
            }
            return defaultValue
        }

        // Helper to convert a color value to vec4
        // Handles: QColor objects, color strings ("#rrggbb"), or pre-converted objects
        function colorToVec4(colorValue, defaultR, defaultG, defaultB, defaultA) {
            if (!colorValue) {
                return Qt.vector4d(defaultR, defaultG, defaultB, defaultA)
            }

            // If it's already a color-like object with r,g,b,a properties
            if (typeof colorValue === "object" && colorValue.r !== undefined) {
                return Qt.vector4d(
                    colorValue.r !== undefined ? colorValue.r : defaultR,
                    colorValue.g !== undefined ? colorValue.g : defaultG,
                    colorValue.b !== undefined ? colorValue.b : defaultB,
                    colorValue.a !== undefined ? colorValue.a : defaultA
                )
            }

            // If it's a string (hex color), use Qt.color() to parse
            if (typeof colorValue === "string") {
                try {
                    var c = Qt.color(colorValue)
                    return Qt.vector4d(c.r, c.g, c.b, c.a)
                } catch (e) {
                    console.warn("RenderNodeOverlay: Failed to parse color:", colorValue)
                    return Qt.vector4d(defaultR, defaultG, defaultB, defaultA)
                }
            }

            return Qt.vector4d(defaultR, defaultG, defaultB, defaultA)
        }

        // Build customParams vec4 for slot 1-4
        // Slot 1: params 0-3, Slot 2: params 4-7, Slot 3: params 8-11, Slot 4: params 12-15
        function buildCustomParams(slot) {
            var prefix = "customParams" + slot + "_"
            return Qt.vector4d(
                getParam(prefix + "x", 0.0),
                getParam(prefix + "y", 0.0),
                getParam(prefix + "z", 0.0),
                getParam(prefix + "w", 0.0)
            )
        }

        // Build customColor vec4 for color 1-4
        function buildCustomColor(colorNum) {
            var p = root.shaderParams || {}
            var key = "customColor" + colorNum
            // Default: white with alpha 0 (not set) for colors 1,3; black with alpha 0 for 2,4
            var defaultR = (colorNum === 1 || colorNum === 3) ? 1.0 : 0.0
            var defaultG = (colorNum === 1 || colorNum === 3) ? 1.0 : 0.0
            var defaultB = (colorNum === 1 || colorNum === 3) ? 1.0 : 0.0
            return colorToVec4(p[key], defaultR, defaultG, defaultB, 0.0)
        }
    }
}
