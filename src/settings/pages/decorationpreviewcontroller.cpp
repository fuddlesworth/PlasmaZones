// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpreviewcontroller.h"

#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "core/types/cavaoptions.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
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
    // needsBackdrop pack gets a note that the preview cannot show its backdrop
    // (there is no scene behind a settings-app card to sample, the same limit
    // the daemon overlay path has).
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
    if (m_audio && m_audio->isRunning()) {
        m_audio->stop();
    }
    if (!m_spectrum.isEmpty()) {
        m_spectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
