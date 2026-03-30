// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick

/**
 * Shared ZoneShaderItem wrapper for overlay and editor preview.
 * Accepts a config object and delegates to ZoneShaderItem with consistent bindings.
 * Single source of truth for bufferShaderPaths, shader params, and zone data.
 */
Item {
    id: root

    required property var config
    // Default to empty object when config is null (callers may not always pass valid config)
    readonly property var safeConfig: config || ({
    })
    property alias status: zoneShaderItem.status
    property alias errorLog: zoneShaderItem.errorLog

    signal shaderError(string log)

    // Force re-read of shader source from disk (hot-reload)
    function loadShader() {
        zoneShaderItem.loadShader();
    }

    ZoneShaderItem {
        id: zoneShaderItem

        anchors.fill: parent
        // Render to a private layer FBO so multipass shaders' buffer passes
        // get an isolated rendering context. Without this, the scene graph's
        // batch renderer internal pass-tracking state desynchronizes when the
        // render node manages its own passes. Matches the working editor
        // preview pattern (ShaderSettingsDialog.qml layer.enabled).
        layer.enabled: shaderSource.toString() !== ""
        layer.textureMirroring: ShaderEffectSource.NoMirroring
        shaderSource: root.safeConfig.shaderSource || ""
        bufferShaderPath: root.safeConfig.bufferShaderPath || ""
        bufferShaderPaths: (root.safeConfig.bufferShaderPaths && root.safeConfig.bufferShaderPaths.length > 0) ? Array.from(root.safeConfig.bufferShaderPaths) : (root.safeConfig.bufferShaderPath ? [root.safeConfig.bufferShaderPath] : [])
        bufferFeedback: root.safeConfig.bufferFeedback || false
        bufferScale: root.safeConfig.bufferScale ?? 1
        bufferWrap: root.safeConfig.bufferWrap || "clamp"
        zones: root.safeConfig.zones || []
        hoveredZoneIndex: root.safeConfig.hoveredZoneIndex ?? -1
        shaderParams: root.safeConfig.shaderParams || {
        }
        iTime: root.safeConfig.iTime ?? 0
        iTimeDelta: root.safeConfig.iTimeDelta ?? 0
        iFrame: root.safeConfig.iFrame ?? 0
        iResolution: root.safeConfig.iResolution || Qt.size(width, height)
        iMouse: root.safeConfig.iMouse || Qt.point(0, 0)
        useWallpaper: root.safeConfig.useWallpaper ?? false
        onStatusChanged: {
            if (status === ZoneShaderItem.Error)
                root.shaderError(errorLog);

        }
    }

    // Use Binding with `when` guard to avoid passing undefined to the C++ setter
    // when config is null. Without this, undefined hits the slow QVariantList path
    // in setAudioSpectrumVariant instead of preserving QVector<float> type identity.
    Binding {
        target: zoneShaderItem
        property: "audioSpectrum"
        value: root.safeConfig.audioSpectrum
        when: root.safeConfig.audioSpectrum !== undefined
    }

    Binding {
        target: zoneShaderItem
        property: "labelsTexture"
        value: root.safeConfig.labelsTexture || null
        when: root.safeConfig.labelsTexture !== undefined && root.safeConfig.labelsTexture !== null
    }

    // wallpaperTexture uses Binding with explicit null guard because QImage
    // truthiness evaluation is unreliable in QML (can crash during teardown).
    Binding {
        target: zoneShaderItem
        property: "wallpaperTexture"
        value: root.safeConfig.wallpaperTexture
        when: root.safeConfig.wallpaperTexture !== undefined && root.safeConfig.wallpaperTexture !== null
    }

}
