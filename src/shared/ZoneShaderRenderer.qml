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

    property alias status: zoneShaderItem.status
    property alias errorLog: zoneShaderItem.errorLog

    signal shaderError(string log)

    ZoneShaderItem {
        id: zoneShaderItem
        anchors.fill: parent

        shaderSource: root.config ? root.config.shaderSource : ""
        bufferShaderPath: root.config ? root.config.bufferShaderPath : ""
        bufferShaderPaths: (root.config && root.config.bufferShaderPaths && root.config.bufferShaderPaths.length > 0)
            ? Array.from(root.config.bufferShaderPaths)
            : (root.config && root.config.bufferShaderPath ? [root.config.bufferShaderPath] : [])
        bufferFeedback: root.config ? (root.config.bufferFeedback || false) : false
        bufferScale: root.config ? (root.config.bufferScale ?? 1.0) : 1.0
        bufferWrap: root.config ? (root.config.bufferWrap || "clamp") : "clamp"

        zones: root.config ? root.config.zones : []
        hoveredZoneIndex: root.config ? (root.config.hoveredZoneIndex ?? -1) : -1
        shaderParams: root.config ? (root.config.shaderParams || {}) : {}

        iTime: root.config ? (root.config.iTime ?? 0) : 0
        iTimeDelta: root.config ? (root.config.iTimeDelta ?? 0) : 0
        iFrame: root.config ? (root.config.iFrame ?? 0) : 0
        iResolution: root.config && root.config.iResolution ? root.config.iResolution : Qt.size(width, height)
        iMouse: root.config && root.config.iMouse ? root.config.iMouse : Qt.point(0, 0)

        audioSpectrum: root.config ? (root.config.audioSpectrum || []) : []

        onStatusChanged: {
            if (status === ZoneShaderItem.Error) {
                root.shaderError(errorLog)
            }
        }
    }

    Binding {
        target: zoneShaderItem
        property: "labelsTexture"
        value: root.config ? root.config.labelsTexture : null
        when: root.config && root.config.labelsTexture
    }
}
