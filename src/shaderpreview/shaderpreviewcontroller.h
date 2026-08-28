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
    ///
    /// @p previewWidth is the logical width the preview renders at. Pass it
    /// and every px-denominated parameter the shader declares is scaled by
    /// how much the preview shrinks the real screen (`previewWidth /
    /// targetScreenSize().width()`), so a 4px border over a 300px pane reads
    /// as the hairline a 4px border is on a 2560px screen instead of the slab
    /// it would otherwise be. Pass 0 for the raw values the daemon uses.
    Q_INVOKABLE QVariantMap translateShaderParams(const QString& shaderId, const QVariantMap& params,
                                                  int previewWidth = 0) const;

    /// The linear reduction a @p previewWidth-wide preview applies to the
    /// backend's target screen, or 1.0 when either is unusable. Exposed
    /// because zone geometry is scaled by this same factor
    /// (zonesForShaderPreview) and a host drawing its own px-denominated
    /// chrome over the preview has to agree with both.
    Q_INVOKABLE double previewPixelScale(int previewWidth) const;

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
    /// The shader's declared parameter metadata, memoized by shader id.
    ///
    /// shaderInfo() is cheap in the settings app (an in-process registry hit)
    /// and expensive in the editor (an uncached blocking D-Bus round-trip to
    /// the daemon), and translateShaderParams needs it on every debounced
    /// slider move and every resize. Cached here rather than per-backend so
    /// both hosts pay the same once-per-shader cost.
    ///
    /// NOTE: this controller observes no registry / effects-changed signal, so
    /// there is nothing to hook invalidation to — the cache is keyed on the
    /// shader id alone and turns over when the previewed shader changes.
    QVariantList parameterInfos(const QString& shaderId) const;

    // Borrowed. In the settings app the owner (a unique_ptr backend declared
    // before the controller) outlives it. In the editor the backend IS the
    // EditorController, which owns the controller as a QObject child — there the
    // IShaderPreviewBackend base subobject is destroyed BEFORE the child
    // controller, so this destructor must never dereference m_backend (it does
    // not: ~ShaderPreviewController only tears down its own CAVA provider).
    IShaderPreviewBackend* m_backend;
    PhosphorAudio::CavaSpectrumProvider* m_audioProvider = nullptr;
    QVector<float> m_audioSpectrum;
    // Mutable: parameterInfos() is called from const preview accessors.
    mutable QString m_paramInfoShaderId;
    mutable QVariantList m_paramInfoCache;
};

} // namespace PlasmaZones
