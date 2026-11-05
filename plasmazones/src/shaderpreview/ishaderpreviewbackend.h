// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAudio/IAudioSpectrumProvider.h>

#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

/// App-specific data source for ShaderPreviewController.
///
/// This is the whole host-specific surface of the zone-shader live preview:
/// *where the shader metadata comes from* and *which zones / screen / audio config
/// to feed*. Everything downstream (geometry transform, p_<id> preamble,
/// label/wallpaper textures, CAVA audio capture) is shared in the controller.
///
/// Currently implemented only by the settings app, whose
/// RegistryShaderPreviewBackend reads its local ShaderRegistry and a default
/// screen. The per-method notes below describe what the interface promises, not a
/// second implementation.
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

    /// Raw zone maps the preview renders over. The settings app returns the shipped
    /// master-stack layout as a representative stand-in. An empty list makes the
    /// controller fall back to a single full-area zone.
    virtual QVariantList previewZones() const = 0;

    /// Target screen size used to convert fixed-geometry pixel coordinates into
    /// preview space. The settings app returns a default.
    virtual QSize targetScreenSize() const = 0;

    /// Whether audio-reactive preview is enabled (CAVA spectrum). The settings app
    /// reads it from ISettings directly.
    virtual bool audioVisualizerEnabled() const = 0;

    /// The full CAVA analysis parameter set the preview's capture runs with
    /// (only consulted when audioVisualizerEnabled()). Backed by the user's
    /// configured Shaders.Audio settings so the preview's bar motion matches
    /// the live daemon and effect output. The settings app reads it from ISettings
    /// directly.
    virtual PhosphorAudio::SpectrumOptions audioOptions() const = 0;
};

} // namespace PlasmaZones
