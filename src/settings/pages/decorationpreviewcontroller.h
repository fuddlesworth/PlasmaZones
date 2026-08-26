// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAudio/IAudioSpectrumProvider.h>

#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace PhosphorAudio {
class CavaSpectrumProvider;
}

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PlasmaZones {

class ISettings;

/**
 * @brief QML data source for the live DECORATION (surface-pack) preview.
 *
 * Sibling of ShaderPreviewController, which serves the zone/overlay browser.
 * The two are deliberately separate rather than one controller with a mode
 * flag: an overlay preview is a shader drawn OVER a set of zones, with a zone
 * array, a per-zone label texture and a wallpaper sampler; a decoration
 * preview is a chain composited onto ONE captured card, with none of those and
 * a focus flag instead. Folding both into one class would give every caller a
 * surface where most of it is inapplicable.
 *
 * What this deliberately does NOT do is compose the stage map itself. It
 * delegates to PhosphorSurfaceShaders::composeStageMap, the same builder
 * OverlayService::applyDecoration uses, so the preview describes a stage
 * exactly as the daemon does. A preview that composed its own stage would
 * drift from the thing it claims to predict, which would make it worse than
 * no preview at all.
 *
 * The registry and settings are borrowed; the owner must outlive this object.
 * Both may be null (the unit-test / degraded construction path), in which case
 * every accessor returns an empty result rather than crashing.
 */
class DecorationPreviewController : public QObject
{
    Q_OBJECT

    /// Live CAVA spectrum, forwarded to every chain stage so an audio-reactive
    /// pack (border-audio) reacts in the preview as it does on screen. Empty
    /// while capture is stopped or the audio visualizer is disabled.
    Q_PROPERTY(QVariant audioSpectrum READ audioSpectrumVariant NOTIFY audioSpectrumChanged)

public:
    explicit DecorationPreviewController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry = nullptr,
                                         ISettings* settings = nullptr, QObject* parent = nullptr);
    ~DecorationPreviewController() override;

    /// One-stage chain for @p packId under @p friendlyParams, in the shape
    /// SurfaceDecoration.qml's `decorationChain` expects. Empty list when the
    /// pack is unknown or carries no valid fragment shader, which leaves the
    /// host inert and the card undecorated — the same degraded behaviour the
    /// daemon shows for an unresolvable pack.
    ///
    /// Theme-derived colours are resolved here exactly as the daemon resolves
    /// them, so a pack using useThemeNeutral / useSystemAccent / useThemeTint
    /// previews in the user's actual colours rather than its raw defaults.
    Q_INVOKABLE QVariantList previewChain(const QString& packId, const QVariantMap& friendlyParams) const;

    /// The pack's outer-margin request in logical px, already clamped to
    /// kMaxDecorationOuterPaddingPx. Feeds SurfaceDecoration's
    /// `decorationOuterPadding`, which inflates the capture and stage by it —
    /// the transparent room an outer effect (glow, shadow, motes) draws into.
    /// 0 for a margin-less pack keeps the classic 1:1 geometry.
    Q_INVOKABLE double previewOuterPadding(const QString& packId, const QVariantMap& friendlyParams) const;

    /// Pack metadata for the parameter editor: id / name / parameters[] with
    /// id, name, type, default, min, max, step, plus the `audio` and
    /// `needsBackdrop` flags the preview pane surfaces. Empty map if unknown.
    Q_INVOKABLE QVariantMap packInfo(const QString& packId) const;

    /// Absolute path to the user's current desktop wallpaper, or empty when it
    /// cannot be resolved. The preview draws it behind the stand-in card so a
    /// pack is judged against the surface it will actually sit on: a border or
    /// glow reads completely differently over a photograph than over flat grey,
    /// and the whole glass / blur family is about what shows THROUGH.
    Q_INVOKABLE QString wallpaperPath() const;

    /// The same wallpaper decoded, for feeding a needsBackdrop pack's backdrop
    /// sampler (the glass / blur family). Null image when it cannot be
    /// resolved, which leaves those packs on their fallback appearance.
    /// Separate from wallpaperPath() because the QML background wants a URL to
    /// display and the shader wants pixels to sample.
    Q_INVOKABLE QImage wallpaperImage() const;

    /// Whether the user's audio visualizer setting is on. The pane starts
    /// capture only for an audio pack AND an enabled visualizer, so a plain
    /// border preview never spins up CAVA.
    Q_INVOKABLE bool audioVisualizerEnabled() const;

    Q_INVOKABLE void startAudioCapture();
    Q_INVOKABLE void stopAudioCapture();

    QVariant audioSpectrumVariant() const;

Q_SIGNALS:
    void audioSpectrumChanged();

private:
    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;
    std::unique_ptr<PhosphorAudio::CavaSpectrumProvider> m_audio;
    QVector<float> m_spectrum;
};

} // namespace PlasmaZones
