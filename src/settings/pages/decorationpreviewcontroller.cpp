// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpreviewcontroller.h"

#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "core/types/cavaoptions.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceChainCompose.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorSurface/SurfaceThemeResolve.h>

#include <QGuiApplication>
#include <QPalette>

namespace PlasmaZones {

DecorationPreviewController::DecorationPreviewController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry,
                                                         ISettings* settings, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
    , m_settings(settings)
{
    if (m_settings) {
        // Follow the setting for as long as a host wants capture. Without this
        // a user who turns the visualizer off mid-preview leaves the external
        // CAVA process running until the dialog closes, because the only stop
        // calls are the dialog's own close and focus-loss handlers.
        //
        // The resume side is gated on m_captureRequested rather than on the
        // setting alone: this controller outlives any one dialog, so reacting
        // to the setting turning on while nothing is previewing would spawn
        // CAVA for a preview nobody is looking at.
        connect(m_settings, &ISettings::enableAudioVisualizerChanged, this, [this]() {
            if (!m_captureRequested) {
                return;
            }
            if (audioVisualizerEnabled()) {
                startAudioCapture();
            } else {
                stopCapture();
            }
        });
    }
}

DecorationPreviewController::~DecorationPreviewController()
{
    // Halt the external CAVA process directly rather than leaving it to the
    // provider's destruction: same reasoning as ShaderPreviewController's dtor,
    // no signal emission while we are being torn down.
    if (m_audio && m_audio->isRunning()) {
        m_audio->stop();
    }
}

QVariantList DecorationPreviewController::previewChain(const QString& packId, const QVariantMap& friendlyParams) const
{
    QVariantList chain;
    if (!m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return chain;
    }
    const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
    if (!effect.isValid()) {
        return chain;
    }

    // Theme colours resolved exactly as OverlayService::applyDecoration does,
    // from the live palette plus the user's highlight / inactive settings, so a
    // theme-reactive pack previews in the colours it will actually render in
    // rather than in its raw metadata defaults.
    QVariantMap resolved = friendlyParams;
    const QPalette pal = QGuiApplication::palette();
    const QColor highlight =
        m_settings ? m_settings->highlightColor() : pal.color(QPalette::Active, QPalette::Highlight);
    const QColor inactive =
        m_settings ? m_settings->inactiveColor() : pal.color(QPalette::Disabled, QPalette::WindowText);
    PhosphorSurfaceShaders::resolveThemeParamColors(effect, resolved,
                                                    {highlight, inactive, pal.color(QPalette::Active, QPalette::Window),
                                                     pal.color(QPalette::Active, QPalette::WindowText)});

    chain.append(PhosphorSurfaceShaders::composeStageMap(effect, resolved));
    return chain;
}

double DecorationPreviewController::previewOuterPadding(const QString& packId, const QVariantMap& friendlyParams) const
{
    if (!m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return 0.0;
    }
    const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
    // Same validity gate previewChain applies, so the two agree on what a
    // usable pack is. Without it a registered pack with no resolvable fragment
    // shader yields an empty chain but a non-zero margin, and the stand-in
    // card shrinks to reserve room for an effect that never draws.
    if (!effect.isValid()) {
        return 0.0;
    }
    // Same clamp the daemon and compositor apply — a typo'd or hostile pack
    // must not be able to demand an absurd preview canvas either.
    return qBound(0.0, PhosphorSurfaceShaders::paddingRequest(effect, friendlyParams),
                  static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));
}

QVariantMap DecorationPreviewController::packInfo(const QString& packId) const
{
    QVariantMap info;
    if (!m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return info;
    }
    const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
    if (!effect.isValid()) {
        return info;
    }
    info.insert(QStringLiteral("id"), effect.id);
    info.insert(QStringLiteral("name"), effect.name);
    // Surfaced by the pane: an audio pack drives CAVA capture, and a
    // needsBackdrop pack gets a note explaining that the backdrop it samples
    // is the desktop wallpaper standing in for the real windows (there is no
    // scene behind a settings-app card, the same limit the daemon overlay path
    // works around the same way).
    info.insert(QStringLiteral("audio"), effect.audio);
    info.insert(QStringLiteral("needsBackdrop"), effect.needsBackdrop);

    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.name);
        m.insert(QStringLiteral("type"), p.type);
        m.insert(QStringLiteral("description"), p.description);
        m.insert(QStringLiteral("group"), p.group);
        m.insert(QStringLiteral("default"), p.defaultValue);
        m.insert(QStringLiteral("min"), p.minValue);
        m.insert(QStringLiteral("max"), p.maxValue);
        m.insert(QStringLiteral("step"), p.stepValue);
        params.append(m);
    }
    info.insert(QStringLiteral("parameters"), params);
    return info;
}

QString DecorationPreviewController::wallpaperPath() const
{
    // Same resolver the overlay preview uses for its wallpaper sampler, so both
    // previews agree on what "the desktop" is.
    return PhosphorShaders::ShaderRegistry::wallpaperPath();
}

QImage DecorationPreviewController::wallpaperImage() const
{
    // ShaderRegistry caches the decode, so the per-card calls behind the
    // browser's thumbnails do not each re-read the file.
    return PhosphorShaders::ShaderRegistry::loadWallpaperImage();
}

bool DecorationPreviewController::audioVisualizerEnabled() const
{
    return m_settings && m_settings->enableAudioVisualizer();
}

QVariant DecorationPreviewController::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_spectrum);
}

void DecorationPreviewController::startAudioCapture()
{
    // A host asking for capture is remembered even when the request cannot be
    // honoured right now (visualizer off, CAVA missing), so the setting
    // becoming true later resumes for a preview that is still open.
    m_captureRequested = true;
    if (m_audio && m_audio->isRunning()) {
        return;
    }
    if (!audioVisualizerEnabled()) {
        return;
    }
    if (!PhosphorAudio::CavaSpectrumProvider::isCavaInstalled()) {
        qCDebug(lcCore) << "Decoration preview: CAVA not available, audio-reactive preview disabled";
        return;
    }
    if (!m_audio) {
        m_audio = std::make_unique<PhosphorAudio::CavaSpectrumProvider>();
        connect(m_audio.get(), &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
                [this](const QVector<float>& spectrum) {
                    m_spectrum = spectrum;
                    Q_EMIT audioSpectrumChanged();
                });
    }
    // The user's full Shaders.Audio parameter set, so the preview's bar motion
    // matches what the daemon and the compositor produce.
    m_audio->setOptions(m_settings ? cavaOptionsFromSettings(m_settings) : PhosphorAudio::SpectrumOptions{});
    m_audio->start();
}

void DecorationPreviewController::stopAudioCapture()
{
    // The host is done previewing, so drop the standing request too: a later
    // settings flip must not resurrect capture for a closed dialog.
    m_captureRequested = false;
    stopCapture();
}

void DecorationPreviewController::stopCapture()
{
    if (m_audio && m_audio->isRunning()) {
        m_audio->stop();
    }
    if (!m_spectrum.isEmpty()) {
        m_spectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
