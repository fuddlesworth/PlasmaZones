// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpreviewcontroller.h"

#include "../daemon/rendering/zonelabeltexturebuilder.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QColor>
#include <QLoggingCategory>

namespace PlasmaZones {

namespace {
Q_LOGGING_CATEGORY(lcShaderPreview, "plasmazones.shaderpreview")

using ShaderInfo = PhosphorShaders::ShaderRegistry::ShaderInfo;
using ParameterInfo = PhosphorShaders::ShaderRegistry::ParameterInfo;

// Mirror ZoneManager::isFixedMode without depending on the editor service: a
// zone is fixed-geometry when its GeometryMode key equals ZoneGeometryMode::Fixed.
bool zoneIsFixedMode(const QVariantMap& zone)
{
    return zone.value(::PhosphorZones::ZoneJsonKeys::GeometryMode, 0).toInt()
        == static_cast<int>(::PhosphorZones::ZoneGeometryMode::Fixed);
}
} // namespace

ShaderPreviewController::ShaderPreviewController(IShaderPreviewBackend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
{
}

ShaderPreviewController::~ShaderPreviewController() = default;

QVariantList ShaderPreviewController::zonesForShaderPreview(int width, int height) const
{
    QVariantList result;
    if (width <= 0 || height <= 0) {
        return result;
    }

    const qreal resW = static_cast<qreal>(width);
    const qreal resH = static_cast<qreal>(height);

    // Use the backend's zones so the editor preview matches the edited layout;
    // the settings app has none and falls through to the single-zone fallback.
    const QVariantList zones = m_backend ? m_backend->previewZones() : QVariantList();

    if (zones.isEmpty()) {
        // Fallback: single zone filling the preview area
        QVariantMap out;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] =
            QStringLiteral("{00000000-0000-0000-0000-000000000001}");
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = 4.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = 4.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = resW - 8.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = resH - 8.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] = 1;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted)] = false;
        const QColor fc = ::PhosphorZones::ZoneDefaults::HighlightColor;
        const qreal a = ::PhosphorZones::ZoneDefaults::Opacity;
        out[QLatin1String("fillR")] = fc.redF() * a;
        out[QLatin1String("fillG")] = fc.greenF() * a;
        out[QLatin1String("fillB")] = fc.blueF() * a;
        out[QLatin1String("fillA")] = a;
        const QColor bc = ::PhosphorZones::ZoneDefaults::BorderColor;
        out[QLatin1String("borderR")] = bc.redF();
        out[QLatin1String("borderG")] = bc.greenF();
        out[QLatin1String("borderB")] = bc.blueF();
        out[QLatin1String("borderA")] = bc.alphaF();
        out[QLatin1String("shaderBorderRadius")] = 8.0;
        out[QLatin1String("shaderBorderWidth")] = 2.0;
        result.append(out);
        return result;
    }

    // Screen size for converting fixed-geometry pixel coords to fractional
    const QSize screenSz = m_backend->targetScreenSize();
    const qreal screenW = qMax(1.0, static_cast<qreal>(screenSz.width()));
    const qreal screenH = qMax(1.0, static_cast<qreal>(screenSz.height()));

    // Scale zone coordinates to preview pixel dimensions.
    // Relative zones: fractional 0-1 * preview size.
    // Fixed zones: pixel coords / screen size * preview size.
    for (const QVariant& zoneVar : zones) {
        const QVariantMap zone = zoneVar.toMap();
        const bool isFixed = zoneIsFixedMode(zone);

        qreal px, py, pw, ph;
        if (isFixed) {
            // Fixed geometry: pixel coords relative to screen, scale to preview
            px = zone.value(::PhosphorZones::ZoneJsonKeys::FixedX, 0.0).toReal() / screenW * resW;
            py = zone.value(::PhosphorZones::ZoneJsonKeys::FixedY, 0.0).toReal() / screenH * resH;
            pw = zone.value(::PhosphorZones::ZoneJsonKeys::FixedWidth, 100.0).toReal() / screenW * resW;
            ph = zone.value(::PhosphorZones::ZoneJsonKeys::FixedHeight, 100.0).toReal() / screenH * resH;
        } else {
            // Relative geometry: fractional 0-1, scale to preview
            px = zone.value(::PhosphorZones::ZoneJsonKeys::X).toReal() * resW;
            py = zone.value(::PhosphorZones::ZoneJsonKeys::Y).toReal() * resH;
            pw = zone.value(::PhosphorZones::ZoneJsonKeys::Width).toReal() * resW;
            ph = zone.value(::PhosphorZones::ZoneJsonKeys::Height).toReal() * resH;
        }

        QVariantMap out;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = zone.value(::PhosphorZones::ZoneJsonKeys::Id);
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = px;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = py;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = pw;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = ph;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] =
            zone.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber);
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted)] =
            zone.value(::PhosphorZones::ZoneJsonKeys::IsHighlighted, false);

        // Fill color from zone appearance (or defaults)
        const bool useCustom = zone.value(::PhosphorZones::ZoneJsonKeys::UseCustomColors).toBool();
        QColor fillColor(zone.value(::PhosphorZones::ZoneJsonKeys::HighlightColor).toString());
        if (!useCustom || !fillColor.isValid())
            fillColor = ::PhosphorZones::ZoneDefaults::HighlightColor;
        const qreal alpha = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::ActiveOpacity, ::PhosphorZones::ZoneDefaults::Opacity).toReal()
            : ::PhosphorZones::ZoneDefaults::Opacity;
        out[QLatin1String("fillR")] = fillColor.redF() * alpha;
        out[QLatin1String("fillG")] = fillColor.greenF() * alpha;
        out[QLatin1String("fillB")] = fillColor.blueF() * alpha;
        out[QLatin1String("fillA")] = alpha;

        // Border color
        QColor borderColor(zone.value(::PhosphorZones::ZoneJsonKeys::BorderColor).toString());
        if (!useCustom || !borderColor.isValid())
            borderColor = ::PhosphorZones::ZoneDefaults::BorderColor;
        out[QLatin1String("borderR")] = borderColor.redF();
        out[QLatin1String("borderG")] = borderColor.greenF();
        out[QLatin1String("borderB")] = borderColor.blueF();
        out[QLatin1String("borderA")] = borderColor.alphaF();

        // Border dimensions
        out[QLatin1String("shaderBorderRadius")] = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderRadius, ::PhosphorZones::ZoneDefaults::BorderRadius)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderRadius);
        out[QLatin1String("shaderBorderWidth")] = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderWidth, ::PhosphorZones::ZoneDefaults::BorderWidth)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderWidth);

        result.append(out);
    }

    return result;
}

QVariantMap ShaderPreviewController::translateShaderParams(const QString& shaderId, const QVariantMap& params) const
{
    return m_backend ? m_backend->translateParams(shaderId, params) : QVariantMap();
}

QVariantMap ShaderPreviewController::getShaderInfo(const QString& shaderId) const
{
    return m_backend ? m_backend->shaderInfo(shaderId) : QVariantMap();
}

QString ShaderPreviewController::shaderParamPreamble(const QString& shaderId) const
{
    if (PhosphorShaders::ShaderRegistry::isNoneShader(shaderId)) {
        return QString();
    }
    // Reconstruct the parameter declarations from the backend's shaderInfo (which
    // carries each param's id/type/slot as the daemon's registry resolved them)
    // and run the same generator the daemon overlay uses, so the preview's
    // `p_<id>` defines land on the exact lanes translateShaderParams uploads to.
    const QVariantMap info = getShaderInfo(shaderId);
    ShaderInfo si;
    const QVariantList params = info.value(QStringLiteral("parameters")).toList();
    for (const QVariant& pv : params) {
        const QVariantMap pm = pv.toMap();
        ParameterInfo pi;
        pi.id = pm.value(QStringLiteral("id")).toString();
        pi.type = pm.value(QStringLiteral("type")).toString();
        pi.slot = pm.value(QStringLiteral("slot"), -1).toInt();
        si.parameters.append(pi);
    }
    return PhosphorShaders::ShaderRegistry::paramPreamble(si);
}

QImage ShaderPreviewController::buildLabelsTexture(const QVariantList& zones, int width, int height) const
{
    if (zones.isEmpty() || width <= 0 || height <= 0) {
        return QImage();
    }
    return ZoneLabelTextureBuilder::build(zones, QSize(width, height), Qt::white, true);
}

QImage ShaderPreviewController::loadWallpaperTexture() const
{
    return PhosphorShaders::ShaderRegistry::loadWallpaperImage();
}

QVariant ShaderPreviewController::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void ShaderPreviewController::startAudioCapture()
{
    if (m_audioProvider && m_audioProvider->isRunning()) {
        return;
    }
    if (!m_backend || !m_backend->audioVisualizerEnabled()) {
        return;
    }
    if (!PhosphorAudio::CavaSpectrumProvider::isCavaInstalled()) {
        qCDebug(lcShaderPreview) << "Audio spectrum: CAVA not available, disabled";
        return;
    }
    if (!m_audioProvider) {
        m_audioProvider = new PhosphorAudio::CavaSpectrumProvider(this);
        connect(m_audioProvider, &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
                [this](const QVector<float>& spectrum) {
                    m_audioSpectrum = spectrum;
                    Q_EMIT audioSpectrumChanged();
                });
    }
    m_audioProvider->setBarCount(m_backend->audioBarCount());
    m_audioProvider->start();
}

void ShaderPreviewController::stopAudioCapture()
{
    if (m_audioProvider && m_audioProvider->isRunning()) {
        m_audioProvider->stop();
    }
    if (!m_audioSpectrum.isEmpty()) {
        m_audioSpectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
