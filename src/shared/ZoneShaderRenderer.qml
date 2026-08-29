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
    readonly property var safeConfig: config || ({})
    // Idle-quiesce park. Only the daemon overlay host binds this (to its
    // idleParked latch); the settings/editor dialog consumers leave it false
    // and reclaim by deactivating their Loader instead. While parked, the
    // private layer FBO below is dropped along with the render node's
    // resources — for the overlay host that layer is a screen-sized RGBA8
    // texture which otherwise survives every releaseIdleGraphicsResources()
    // call, because it belongs to Qt's QQuickItemLayer, not to the shader
    // node. The host MUST clear this on every wake path before the next
    // painted frame — the layer is correctness-relevant for multipass packs
    // (see the layer.enabled note).
    property bool parked: false
    property alias status: zoneShaderItem.status
    property alias errorLog: zoneShaderItem.errorLog

    signal shaderError(string log)

    // Force re-read of shader source from disk (hot-reload). Delegates to the
    // underlying ZoneShaderItem, which inherits reloadShader() as a Q_INVOKABLE
    // from PhosphorRendering::ShaderEffect.
    function reloadShader() {
        zoneShaderItem.reloadShader();
    }

    // Drop the shader item's GPU resources while the overlay sits idle
    // (called by the daemon's idle quiesce, after the grace window). They
    // rebuild lazily on the next painted frame.
    function releaseIdleGraphicsResources() {
        zoneShaderItem.releaseIdleGraphicsResources();
    }

    ZoneShaderItem {
        id: zoneShaderItem

        anchors.fill: parent
        // Render to a private layer FBO so multipass shaders' buffer passes
        // get an isolated rendering context. Without this, the scene graph's
        // batch renderer internal pass-tracking state desynchronizes when the
        // render node manages its own passes.
        //
        // Released while parked (idle quiesce): the FBO is screen-sized and
        // otherwise lives for the window's whole lifetime. Safe only because
        // a parked item never paints — the host hides the shader content
        // while idle and gates the renderer's visible binding on the same
        // park state — including during the one composited frame the C++
        // release forces (ShaderEffect::releaseIdleGraphicsResources calls
        // win->update(); that frame is what flushes this layer drop on an
        // otherwise idle window). So no multipass frame can render unlayered.
        layer.enabled: shaderSource.toString() !== "" && !root.parked
        layer.textureMirroring: ShaderEffectSource.NoMirroring
        shaderSource: root.safeConfig.shaderSource || ""
        bufferShaderPath: root.safeConfig.bufferShaderPath || ""
        bufferShaderPaths: (root.safeConfig.bufferShaderPaths && root.safeConfig.bufferShaderPaths.length > 0) ? Array.from(root.safeConfig.bufferShaderPaths) : (root.safeConfig.bufferShaderPath ? [root.safeConfig.bufferShaderPath] : [])
        bufferFeedback: root.safeConfig.bufferFeedback || false
        bufferScale: root.safeConfig.bufferScale ?? 1
        halfFloatBuffers: root.safeConfig.halfFloatBuffers ?? true
        bufferWrap: root.safeConfig.bufferWrap || "clamp"
        zones: root.safeConfig.zones || []
        hoveredZoneIndex: root.safeConfig.hoveredZoneIndex ?? -1
        shaderParams: root.safeConfig.shaderParams || {}
        // T1.1 (zone): generated `#define p_<id> ...` block the node splices
        // after #version so packs read params by name. Empty = no-op.
        paramPreamble: root.safeConfig.paramPreamble || ""
        iTime: root.safeConfig.iTime ?? 0
        iTimeDelta: root.safeConfig.iTimeDelta ?? 0
        iFrame: root.safeConfig.iFrame ?? 0
        iResolution: root.safeConfig.iResolution || Qt.size(width, height)
        iMouse: root.safeConfig.iMouse || Qt.point(0, 0)
        useWallpaper: root.safeConfig.useWallpaper ?? false
        // Both of these are assigned DIRECTLY, deliberately not through a
        // Binding element like audioSpectrum below. See the note under this
        // item.
        wallpaperTexture: root.safeConfig.wallpaperTexture
        labelsTexture: root.safeConfig.labelsTexture
        useDepthBuffer: root.safeConfig.useDepthBuffer ?? false
        bufferWraps: root.safeConfig.bufferWraps || []
        bufferFilter: root.safeConfig.bufferFilter || "linear"
        bufferFilters: root.safeConfig.bufferFilters || []
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

    // wallpaperTexture and labelsTexture are assigned DIRECTLY on the item
    // above, deliberately not through a Binding element like audioSpectrum.
    //
    // A `Binding { property: "..."; value: <a QImage> }` delivers an INVALID
    // QVariant to the setter. The image does not survive the trip through
    // Binding's own `value` property, and nothing is logged. That silently
    // emptied the wallpaper for every pack that samples it, and emptied the
    // labels for the settings and editor previews, which hand a full QImage to
    // labelsTexture and rely on the registered converter. The daemon's own
    // labels were spared only because it passes the ZoneLabelTexture payload,
    // which does survive.
    //
    // audioSpectrum is fine where it is: a QVariantList and a QVector<float>
    // both come through a Binding intact. Both properties here take a QVariant
    // and convert (setWallpaperTextureVariant / setLabelsTextureVariant), so an
    // absent config key means "nothing to show" rather than a warning.
}
