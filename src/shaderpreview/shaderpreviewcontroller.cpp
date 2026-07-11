// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpreviewcontroller.h"

#include "../daemon/rendering/zonelabeltexturebuilder.h"
#include "../phosphor_i18n.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

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

// Single source of truth for the premultiplied fill/border appearance channels
// a ZoneShaderItem preview consumes — keeps the C++→QML key strings spelled once
// rather than duplicated across the fallback and per-zone branches below.
void writeZoneAppearance(QVariantMap& out, const QColor& fill, qreal fillAlpha, const QColor& border,
                         qreal borderRadius, qreal borderWidth)
{
    out[QLatin1String("fillR")] = fill.redF() * fillAlpha;
    out[QLatin1String("fillG")] = fill.greenF() * fillAlpha;
    out[QLatin1String("fillB")] = fill.blueF() * fillAlpha;
    out[QLatin1String("fillA")] = fillAlpha;
    out[QLatin1String("borderR")] = border.redF();
    out[QLatin1String("borderG")] = border.greenF();
    out[QLatin1String("borderB")] = border.blueF();
    out[QLatin1String("borderA")] = border.alphaF();
    out[QLatin1String("shaderBorderRadius")] = borderRadius;
    out[QLatin1String("shaderBorderWidth")] = borderWidth;
}
} // namespace

ShaderPreviewController::ShaderPreviewController(IShaderPreviewBackend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
{
}

ShaderPreviewController::~ShaderPreviewController()
{
    // Symmetric teardown: stop CAVA capture explicitly rather than relying on
    // ~CavaSpectrumProvider (a QObject child destroyed after this body) to halt
    // the external process. Stop the provider directly — no signal emission
    // during destruction.
    if (m_audioProvider && m_audioProvider->isRunning()) {
        m_audioProvider->stop();
    }
}

QVariantList ShaderPreviewController::zonesForShaderPreview(int width, int height) const
{
    QVariantList result;
    if (width <= 0 || height <= 0) {
        return result;
    }
    // No backend → nothing to preview, and targetScreenSize() below would deref
    // it; bail symmetrically with translateShaderParams / getShaderInfo.
    if (!m_backend) {
        return result;
    }

    const qreal resW = static_cast<qreal>(width);
    const qreal resH = static_cast<qreal>(height);

    // Use the backend's zones so the preview matches what each backend wants:
    // the editor returns the live edited layout, the settings app the shipped
    // master-stack. A backend with no zones falls through to the single-zone
    // fallback below.
    const QVariantList zones = m_backend->previewZones();

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
        writeZoneAppearance(out, ::PhosphorZones::ZoneDefaults::HighlightColor, ::PhosphorZones::ZoneDefaults::Opacity,
                            ::PhosphorZones::ZoneDefaults::BorderColor,
                            static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderRadius),
                            static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderWidth));
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

        // Border color
        QColor borderColor(zone.value(::PhosphorZones::ZoneJsonKeys::BorderColor).toString());
        if (!useCustom || !borderColor.isValid())
            borderColor = ::PhosphorZones::ZoneDefaults::BorderColor;

        // Border dimensions
        const qreal borderRadius = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderRadius, ::PhosphorZones::ZoneDefaults::BorderRadius)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderRadius);
        const qreal borderWidth = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderWidth, ::PhosphorZones::ZoneDefaults::BorderWidth)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderWidth);

        writeZoneAppearance(out, fillColor, alpha, borderColor, borderRadius, borderWidth);

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
    return ZoneLabelTextureBuilder::build(zones, QSize(width, height), Qt::white, true).toImage();
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
    // The preview deliberately runs the default analysis parameters: the
    // editor is a separate process and IShaderPreviewBackend only carries the
    // bar count and the enable toggle. Bar count is the one knob shader packs
    // read structurally (iAudioSpectrumSize / texture width), so matching it
    // keeps the preview faithful; the remaining DSP knobs only shape bar
    // motion and stay at PhosphorAudio::Defaults here.
    PhosphorAudio::SpectrumOptions audioOpts = m_audioProvider->options();
    audioOpts.barCount = m_backend->audioBarCount();
    m_audioProvider->setOptions(audioOpts);
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

bool ShaderPreviewController::saveShaderPreset(const QString& filePath, const QString& shaderId,
                                               const QVariantMap& shaderParams, const QString& presetName)
{
    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetSaveFailed(PhosphorI18n::tr("File path cannot be empty", "@info"));
        return false;
    }
    if (PhosphorShaders::ShaderRegistry::isNoneShader(shaderId)) {
        Q_EMIT shaderPresetSaveFailed(PhosphorI18n::tr("No shader selected to save", "@info"));
        return false;
    }

    QString name = presetName;
    if (name.isEmpty()) {
        name = QFileInfo(filePath).completeBaseName();
    }

    QJsonObject obj;
    obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] = name;
    obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderId)] = shaderId;
    obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderParams)] = QJsonObject::fromVariantMap(shaderParams);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString error = PhosphorI18n::tr("Failed to save preset: %1", "@info").arg(file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcShaderPreview) << error;
        return false;
    }
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    // flush() forces the buffered write out so a deferred failure (e.g. disk
    // full) surfaces here rather than silently on close, leaving a truncated
    // preset that later fails to parse with no error reported.
    if (file.write(json) != json.size() || !file.flush()) {
        const QString error = PhosphorI18n::tr("Failed to write preset file: %1", "@info").arg(file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcShaderPreview) << error;
        file.remove(); // don't leave a half-written preset that later fails to parse
        return false;
    }
    return true;
}

QVariantMap ShaderPreviewController::loadShaderPreset(const QString& filePath)
{
    QVariantMap result;

    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetLoadFailed(PhosphorI18n::tr("File path cannot be empty", "@info"));
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT shaderPresetLoadFailed(
            PhosphorI18n::tr("Failed to open preset file: %1", "@info").arg(file.errorString()));
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        Q_EMIT shaderPresetLoadFailed(
            PhosphorI18n::tr("Invalid preset file: %1", "@info").arg(parseError.errorString()));
        return result;
    }
    if (!doc.isObject()) {
        Q_EMIT shaderPresetLoadFailed(PhosphorI18n::tr("Preset file must contain a JSON object", "@info"));
        return result;
    }

    const QJsonObject obj = doc.object();
    const QString shaderId = obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderId)].toString();
    if (shaderId.isEmpty()) {
        Q_EMIT shaderPresetLoadFailed(PhosphorI18n::tr("Preset file missing shader ID", "@info"));
        return result;
    }
    // Validate the shader still exists via the backend's metadata.
    if (!m_backend || m_backend->shaderInfo(shaderId).isEmpty()) {
        Q_EMIT shaderPresetLoadFailed(PhosphorI18n::tr("Shader in preset is no longer available", "@info"));
        return result;
    }

    QVariantMap shaderParams;
    if (obj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderParams))) {
        const QJsonValue paramsValue = obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderParams)];
        // A present-but-non-object params field is a corrupt/hand-edited file —
        // fail loudly rather than silently dropping the user's saved values.
        if (!paramsValue.isObject()) {
            Q_EMIT shaderPresetLoadFailed(PhosphorI18n::tr("Preset file has malformed parameters", "@info"));
            return result;
        }
        shaderParams = paramsValue.toObject().toVariantMap();
    }

    result[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] =
        obj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)].toString();
    result[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderId)] = shaderId;
    result[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShaderParams)] = shaderParams;
    return result;
}

QString ShaderPreviewController::shaderPresetDirectory() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shader-presets");
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qCWarning(lcShaderPreview) << "Failed to create shader-preset directory:" << path;
    }
    return path;
}

} // namespace PlasmaZones
