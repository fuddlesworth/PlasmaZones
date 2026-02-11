// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import PlasmaZones 1.0

/**
 * Shared ZoneShaderItem wrapper for overlay and editor preview.
 * Accepts a config object and delegates to ZoneShaderItem with consistent bindings.
 * Single source of truth for bufferShaderPaths, shader params, and zone data.
 */
Item {
    id: root

    required property var config
    // Default to empty object when config is null (callers may not always pass valid config)
    readonly property var safeConfig: config || ({})

    property alias status: zoneShaderItem.status
    property alias errorLog: zoneShaderItem.errorLog

    signal shaderError(string log)

    ZoneShaderItem {
        id: zoneShaderItem
        anchors.fill: parent

        shaderSource: root.safeConfig.shaderSource || ""
        bufferShaderPath: root.safeConfig.bufferShaderPath || ""
        bufferShaderPaths: (root.safeConfig.bufferShaderPaths && root.safeConfig.bufferShaderPaths.length > 0)
            ? Array.from(root.safeConfig.bufferShaderPaths)
            : (root.safeConfig.bufferShaderPath ? [root.safeConfig.bufferShaderPath] : [])
        bufferFeedback: root.safeConfig.bufferFeedback || false
        bufferScale: root.safeConfig.bufferScale ?? 1.0
        bufferWrap: root.safeConfig.bufferWrap || "clamp"

        zones: root.safeConfig.zones || []
        hoveredZoneIndex: root.safeConfig.hoveredZoneIndex ?? -1
        shaderParams: root.safeConfig.shaderParams || {}

        iTime: root.safeConfig.iTime ?? 0
        iTimeDelta: root.safeConfig.iTimeDelta ?? 0
        iFrame: root.safeConfig.iFrame ?? 0
        iResolution: root.safeConfig.iResolution || Qt.size(width, height)
        iMouse: root.safeConfig.iMouse || Qt.point(0, 0)

        audioSpectrum: root.safeConfig.audioSpectrum || []

        onStatusChanged: {
            if (status === ZoneShaderItem.Error) {
                root.shaderError(errorLog)
            }
        }
    }

    Binding {
        target: zoneShaderItem
        property: "labelsTexture"
        value: root.safeConfig.labelsTexture || null
        when: root.safeConfig.labelsTexture
    }
}
