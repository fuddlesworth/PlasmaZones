// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

/// App-specific data source for ShaderPreviewController.
///
/// The only part of the zone-shader live preview that differs between the
/// editor and the settings app is *where the shader metadata comes from* and
/// *which zones / screen / audio config to feed*. The editor backs this with
/// D-Bus to the daemon's registry and the edited layout; the settings app backs
/// it with its local ShaderRegistry and a sensible default screen. Everything
/// downstream (geometry transform, p_<id> preamble, label/wallpaper textures,
/// CAVA audio capture) is shared in the controller.
///
/// Implementations are borrowed by the controller — the owner must keep the
/// backend alive for the controller's lifetime.
class IShaderPreviewBackend
{
public:
    virtual ~IShaderPreviewBackend() = default;

    /// Shader metadata map matching PhosphorShaders::ShaderRegistry::shaderInfo
    /// (id / name / parameters[] with id/type/slot/min/max/default, source url,
    /// buffer paths). Empty map if the id is unknown.
    virtual QVariantMap shaderInfo(const QString& shaderId) const = 0;

    /// Stored {paramId: value} → {uniformName: value}, matching
    /// PhosphorShaders::ShaderRegistry::translateParamsToUniforms so the
    /// preview uploads to the exact lanes the generated p_<id> defines read.
    virtual QVariantMap translateParams(const QString& shaderId, const QVariantMap& params) const = 0;

    /// Raw zone maps the preview renders over. The editor returns the live
    /// layout so the preview matches what the user is editing; the settings app
    /// returns the shipped master-stack layout so the preview matches the baked
    /// preview.png thumbnails. An empty list makes the controller fall back to a
    /// single full-area zone.
    virtual QVariantList previewZones() const = 0;

    /// Target screen size used to convert fixed-geometry pixel coordinates into
    /// preview space. The editor returns the edited screen; the settings app a
    /// default.
    virtual QSize targetScreenSize() const = 0;

    /// Whether audio-reactive preview is enabled (CAVA spectrum). The editor
    /// reads this over D-Bus; the settings app from ISettings directly.
    virtual bool audioVisualizerEnabled() const = 0;

    /// Number of spectrum bars to request from CAVA (only consulted when
    /// audioVisualizerEnabled()).
    virtual int audioBarCount() const = 0;
};

} // namespace PlasmaZones
