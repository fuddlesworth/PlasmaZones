// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Zone overlay window with shader-based rendering.
 *
 * Uses layer.enabled to capture zones as texture for post-processing.
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

    // Compatibility properties
    property string highlightedZoneId: ""
    // Note: Using 'var' for highlightedZoneIds because it's a dynamic list from C++
    property var highlightedZoneIds: []

    signal zoneClicked(int index)
    signal zoneHovered(int index)

    // Window flags - LayerShellQt handles the overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    // Note: Debug logging removed from onShaderSourceChanged/onZonesChanged to reduce log noise.
    // Monitor ShaderEffect.status for shader load status.

    // Update custom params when shaderParams changes
    onShaderParamsChanged: {
        shaderEffect.customParams1 = internal.buildCustomParams1()
        shaderEffect.customParams2 = internal.buildCustomParams2()
        shaderEffect.customColor1 = internal.buildCustomColor1()
        shaderEffect.customColor2 = internal.buildCustomColor2()
    }

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

        // ═══════════════════════════════════════════════════════════════════
        // BASE ZONE LAYER - Rendered to texture via layer.enabled
        // ═══════════════════════════════════════════════════════════════════
        Item {
            id: zoneBaseLayer
            anchors.fill: parent
            // Use layer to capture to texture - this is more reliable than ShaderEffectSource
            layer.enabled: shaderEffect.visible
            layer.smooth: true
            // Make invisible when shader is active (layer still captures)
            opacity: shaderEffect.visible ? 0 : 1

            Repeater {
                model: root.zones

                Rectangle {
                    required property var modelData
                    required property int index

                    x: modelData.x !== undefined ? modelData.x : 0
                    y: modelData.y !== undefined ? modelData.y : 0
                    width: modelData.width !== undefined ? modelData.width : 0
                    height: modelData.height !== undefined ? modelData.height : 0

                    property bool isHighlighted: modelData.isHighlighted || false
                    property bool useCustom: modelData.useCustomColors === true

                    color: {
                        var baseColor = isHighlighted 
                            ? (useCustom && modelData.highlightColor ? modelData.highlightColor : root.highlightColor)
                            : (useCustom && modelData.inactiveColor ? modelData.inactiveColor : root.inactiveColor);
                        var alpha = isHighlighted
                            ? (useCustom && modelData.activeOpacity !== undefined ? modelData.activeOpacity : root.activeOpacity)
                            : (useCustom && modelData.inactiveOpacity !== undefined ? modelData.inactiveOpacity : root.inactiveOpacity);
                        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha);
                    }

                    border.color: (useCustom && modelData.borderColor) ? modelData.borderColor : root.borderColor
                    border.width: (useCustom && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth
                    radius: (useCustom && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // SHADER EFFECT - Post-processes the zone layer
        // ═══════════════════════════════════════════════════════════════════
        ShaderEffect {
            id: shaderEffect
            anchors.fill: parent
            visible: root.shaderSource.toString() !== "" && status !== ShaderEffect.Error

            fragmentShader: root.shaderSource

            // Source texture from the layer
            property var source: zoneBaseLayer

            // Time uniforms for animation
            property real iTime: root.iTime
            property real iTimeDelta: root.iTimeDelta
            property int iFrame: root.iFrame

            // Resolution
            property size iResolution: Qt.size(root.width, root.height)
            property real iDevicePixelRatio: Screen.devicePixelRatio

            // Mouse
            property point iMouse: root.mousePosition

            // Zone info
            property int zoneCount: root.zoneCount
            property int highlightedCount: root.highlightedCount

            // Custom parameters
            property vector4d customParams1: internal.buildCustomParams1()
            property vector4d customParams2: internal.buildCustomParams2()
            property vector4d customColor1: internal.buildCustomColor1()
            property vector4d customColor2: internal.buildCustomColor2()

            onStatusChanged: {
                // Only log errors - success is expected
                if (status === ShaderEffect.Error) {
                    console.error("ShaderOverlay: Shader error:", log)
                    if (typeof overlayService !== "undefined") {
                        overlayService.onShaderError(log)
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // FALLBACK RENDERING - When no shader or shader failed
        // ═══════════════════════════════════════════════════════════════════
        Repeater {
            model: root.zones
            // Note: 'visible' on Repeater has no effect - delegate visibility is controlled below

            delegate: ZoneItem {
                required property var modelData
                required property int index

                visible: !shaderEffect.visible

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

        // ═══════════════════════════════════════════════════════════════════
        // ZONE LABELS - On top of everything
        // ═══════════════════════════════════════════════════════════════════
        Repeater {
            model: root.zones
            // Note: 'visible' on Repeater has no effect - delegate visibility is controlled below

            ZoneLabel {
                required property var modelData
                required property int index

                visible: shaderEffect.visible && root.showNumbers && width > 0 && height > 0

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

        function buildCustomParams1() {
            var p = root.shaderParams || {}
            return Qt.vector4d(
                p.customParams1_x !== undefined ? p.customParams1_x : 0.5,
                p.customParams1_y !== undefined ? p.customParams1_y : 2.0,
                p.customParams1_z !== undefined ? p.customParams1_z : 0.0,
                p.customParams1_w !== undefined ? p.customParams1_w : 0.0
            )
        }

        function buildCustomParams2() {
            var p = root.shaderParams || {}
            return Qt.vector4d(
                p.customParams2_x !== undefined ? p.customParams2_x : 0.0,
                p.customParams2_y !== undefined ? p.customParams2_y : 0.0,
                p.customParams2_z !== undefined ? p.customParams2_z : 0.0,
                p.customParams2_w !== undefined ? p.customParams2_w : 0.0
            )
        }

        function buildCustomColor1() {
            var p = root.shaderParams || {}
            var c = p.customColor1 || "#ffffff"
            // Parse hex string to color, or use as-is if already a color object
            if (typeof c === "string") {
                try {
                    c = Qt.color(c)
                } catch (e) {
                    c = Qt.rgba(1, 1, 1, 1)
                }
            }
            return Qt.vector4d(c.r !== undefined ? c.r : 1, c.g !== undefined ? c.g : 1, c.b !== undefined ? c.b : 1, c.a !== undefined ? c.a : 1)
        }

        function buildCustomColor2() {
            var p = root.shaderParams || {}
            var c = p.customColor2 || "#000000"
            // Parse hex string to color, or use as-is if already a color object
            if (typeof c === "string") {
                try {
                    c = Qt.color(c)
                } catch (e) {
                    c = Qt.rgba(0, 0, 0, 1)
                }
            }
            return Qt.vector4d(c.r !== undefined ? c.r : 0, c.g !== undefined ? c.g : 0, c.b !== undefined ? c.b : 0, c.a !== undefined ? c.a : 1)
        }
    }
}
