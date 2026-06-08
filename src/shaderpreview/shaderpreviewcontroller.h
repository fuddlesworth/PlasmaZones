// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ishaderpreviewbackend.h"
#include "plasmazones_shaderpreview_export.h"

#include <QImage>
#include <QObject>
#include <QVariant>
#include <QVector>

namespace PhosphorAudio {
class CavaSpectrumProvider;
}

namespace PlasmaZones {

/// Shared zone-shader live-preview feed.
///
/// Turns the @ref IShaderPreviewBackend's data into the maps / textures a
/// ZoneShaderItem preview consumes — the zone geometry transform, the generated
/// `p_<id>` preamble, translated uniform params, the zone-label texture, the
/// wallpaper texture — and owns the optional CAVA audio-spectrum capture used by
/// audio-reactive shaders. Both the zone editor and the settings-app shader
/// browser drive their preview through one of these, differing only in the
/// injected backend.
///
/// The backend is borrowed: the caller owns it and must keep it alive for the
/// controller's lifetime.
class PLASMAZONES_SHADERPREVIEW_EXPORT ShaderPreviewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant audioSpectrum READ audioSpectrumVariant NOTIFY audioSpectrumChanged)

public:
    explicit ShaderPreviewController(IShaderPreviewBackend* backend, QObject* parent = nullptr);
    ~ShaderPreviewController() override;

    /// Backend zones scaled into a @p width × @p height preview, with per-zone
    /// fill/border appearance resolved. Falls back to a single inset zone when
    /// the backend has none. Empty on non-positive dimensions.
    Q_INVOKABLE QVariantList zonesForShaderPreview(int width, int height) const;

    /// Translate stored params to uniform names (delegates to the backend).
    Q_INVOKABLE QVariantMap translateShaderParams(const QString& shaderId, const QVariantMap& params) const;

    /// Shader metadata map (delegates to the backend).
    Q_INVOKABLE QVariantMap getShaderInfo(const QString& shaderId) const;

    /// Generated `#define p_<id> ...` preamble for the shader, reconstructed
    /// from the backend's shaderInfo and run through the same generator the
    /// daemon overlay uses. Empty for the none-shader.
    Q_INVOKABLE QString shaderParamPreamble(const QString& shaderId) const;

    /// Zone-number label texture for the preview's label pass. Null on empty
    /// zones or non-positive dimensions.
    Q_INVOKABLE QImage buildLabelsTexture(const QVariantList& zones, int width, int height) const;

    /// Current Plasma wallpaper as a texture, or null if unavailable.
    Q_INVOKABLE QImage loadWallpaperTexture() const;

    QVariant audioSpectrumVariant() const;

    /// Start / stop CAVA audio-spectrum capture (no-op when the visualizer is
    /// disabled or CAVA is not installed). Drives audio-reactive preview.
    Q_INVOKABLE void startAudioCapture();
    Q_INVOKABLE void stopAudioCapture();

    // ── Shader presets (shared by the editor + settings preview) ──────
    // CONTRACT: @p filePath is a trusted, user-chosen absolute path (a
    // FileDialog selection). These methods do NOT sanitize it against directory
    // traversal — callers must never pass an attacker-influenced path.
    /// Writes {name, shaderId, shaderParams} as JSON to @p filePath. Returns
    /// false and emits shaderPresetSaveFailed on any error.
    Q_INVOKABLE bool saveShaderPreset(const QString& filePath, const QString& shaderId, const QVariantMap& shaderParams,
                                      const QString& presetName);

    /// Reads a preset JSON. Returns {name, shaderId, shaderParams} or an empty
    /// map (emitting shaderPresetLoadFailed) on error / unknown shader.
    Q_INVOKABLE QVariantMap loadShaderPreset(const QString& filePath);

    /// The shared user preset directory (created if missing).
    Q_INVOKABLE QString shaderPresetDirectory() const;

Q_SIGNALS:
    void audioSpectrumChanged();
    void shaderPresetSaveFailed(const QString& error);
    void shaderPresetLoadFailed(const QString& error);

private:
    // Borrowed. In the settings app the owner (a unique_ptr backend declared
    // before the controller) outlives it. In the editor the backend IS the
    // EditorController, which owns the controller as a QObject child — there the
    // IShaderPreviewBackend base subobject is destroyed BEFORE the child
    // controller, so this destructor must never dereference m_backend (it does
    // not: ~ShaderPreviewController only tears down its own CAVA provider).
    IShaderPreviewBackend* m_backend;
    PhosphorAudio::CavaSpectrumProvider* m_audioProvider = nullptr;
    QVector<float> m_audioSpectrum;
};

} // namespace PlasmaZones
