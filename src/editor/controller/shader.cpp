// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/UpdateShaderIdCommand.h"
#include "../undo/commands/UpdateShaderParamsCommand.h"
#include "../helpers/ShaderDbusQueries.h"
#include "../helpers/SettingsDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/shaderregistry.h"
#include "../../core/logging.h"
#include "../../daemon/rendering/zonelabeltexturebuilder.h"
#include "../../daemon/cavaservice.h"

#include "pz_i18n.h"
#include <QColor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace PlasmaZones {

QVariantList EditorController::zonesForShaderPreview(int width, int height) const
{
    QVariantList result;
    if (width <= 0 || height <= 0) {
        return result;
    }

    const qreal resW = static_cast<qreal>(width);
    const qreal resH = static_cast<qreal>(height);

    // Use the real layout zones so the preview matches what the user is editing.
    const QVariantList zones = m_zoneManager ? m_zoneManager->zones() : QVariantList();

    if (zones.isEmpty()) {
        // Fallback: single zone filling the preview area
        QVariantMap out;
        out[QLatin1String(JsonKeys::Id)] = QStringLiteral("{00000000-0000-0000-0000-000000000001}");
        out[QLatin1String(JsonKeys::X)] = 4.0;
        out[QLatin1String(JsonKeys::Y)] = 4.0;
        out[QLatin1String(JsonKeys::Width)] = resW - 8.0;
        out[QLatin1String(JsonKeys::Height)] = resH - 8.0;
        out[QLatin1String(JsonKeys::ZoneNumber)] = 1;
        out[QLatin1String(JsonKeys::IsHighlighted)] = false;
        const QColor fc = Defaults::HighlightColor;
        const qreal a = Defaults::Opacity;
        out[QLatin1String("fillR")] = fc.redF() * a;
        out[QLatin1String("fillG")] = fc.greenF() * a;
        out[QLatin1String("fillB")] = fc.blueF() * a;
        out[QLatin1String("fillA")] = a;
        const QColor bc = Defaults::BorderColor;
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
    const QSize screenSz = targetScreenSize();
    const qreal screenW = qMax(1.0, static_cast<qreal>(screenSz.width()));
    const qreal screenH = qMax(1.0, static_cast<qreal>(screenSz.height()));

    // Scale zone coordinates to preview pixel dimensions.
    // Relative zones: fractional 0-1 * preview size.
    // Fixed zones: pixel coords / screen size * preview size.
    for (const QVariant& zoneVar : zones) {
        const QVariantMap zone = zoneVar.toMap();
        const bool isFixed = ZoneManager::isFixedMode(zone);

        qreal px, py, pw, ph;
        if (isFixed) {
            // Fixed geometry: pixel coords relative to screen, scale to preview
            px = zone.value(JsonKeys::FixedX, 0.0).toReal() / screenW * resW;
            py = zone.value(JsonKeys::FixedY, 0.0).toReal() / screenH * resH;
            pw = zone.value(JsonKeys::FixedWidth, 100.0).toReal() / screenW * resW;
            ph = zone.value(JsonKeys::FixedHeight, 100.0).toReal() / screenH * resH;
        } else {
            // Relative geometry: fractional 0-1, scale to preview
            px = zone.value(JsonKeys::X).toReal() * resW;
            py = zone.value(JsonKeys::Y).toReal() * resH;
            pw = zone.value(JsonKeys::Width).toReal() * resW;
            ph = zone.value(JsonKeys::Height).toReal() * resH;
        }

        QVariantMap out;
        out[QLatin1String(JsonKeys::Id)] = zone.value(JsonKeys::Id);
        out[QLatin1String(JsonKeys::X)] = px;
        out[QLatin1String(JsonKeys::Y)] = py;
        out[QLatin1String(JsonKeys::Width)] = pw;
        out[QLatin1String(JsonKeys::Height)] = ph;
        out[QLatin1String(JsonKeys::ZoneNumber)] = zone.value(JsonKeys::ZoneNumber);
        out[QLatin1String(JsonKeys::IsHighlighted)] = zone.value(JsonKeys::IsHighlighted, false);

        // Fill color from zone appearance (or defaults)
        const bool useCustom = zone.value(JsonKeys::UseCustomColors).toBool();
        QColor fillColor(zone.value(JsonKeys::HighlightColor).toString());
        if (!useCustom || !fillColor.isValid())
            fillColor = Defaults::HighlightColor;
        const qreal alpha =
            useCustom ? zone.value(JsonKeys::ActiveOpacity, Defaults::Opacity).toReal() : Defaults::Opacity;
        out[QLatin1String("fillR")] = fillColor.redF() * alpha;
        out[QLatin1String("fillG")] = fillColor.greenF() * alpha;
        out[QLatin1String("fillB")] = fillColor.blueF() * alpha;
        out[QLatin1String("fillA")] = alpha;

        // Border color
        QColor borderColor(zone.value(JsonKeys::BorderColor).toString());
        if (!useCustom || !borderColor.isValid())
            borderColor = Defaults::BorderColor;
        out[QLatin1String("borderR")] = borderColor.redF();
        out[QLatin1String("borderG")] = borderColor.greenF();
        out[QLatin1String("borderB")] = borderColor.blueF();
        out[QLatin1String("borderA")] = borderColor.alphaF();

        // Border dimensions
        out[QLatin1String("shaderBorderRadius")] = useCustom
            ? zone.value(JsonKeys::BorderRadius, Defaults::BorderRadius).toReal()
            : static_cast<qreal>(Defaults::BorderRadius);
        out[QLatin1String("shaderBorderWidth")] = useCustom
            ? zone.value(JsonKeys::BorderWidth, Defaults::BorderWidth).toReal()
            : static_cast<qreal>(Defaults::BorderWidth);

        result.append(out);
    }

    return result;
}

QVariantMap EditorController::translateShaderParams(const QString& shaderId, const QVariantMap& params) const
{
    return ShaderDbusQueries::queryTranslateShaderParams(shaderId, params);
}

bool EditorController::saveShaderPreset(const QString& filePath, const QString& shaderId,
                                        const QVariantMap& shaderParams, const QString& presetName)
{
    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetSaveFailed(PzI18n::tr("File path cannot be empty", "@info"));
        return false;
    }

    if (ShaderRegistry::isNoneShader(shaderId)) {
        Q_EMIT shaderPresetSaveFailed(PzI18n::tr("No shader selected to save", "@info"));
        return false;
    }

    QString name = presetName;
    if (name.isEmpty()) {
        name = QFileInfo(filePath).completeBaseName();
    }

    QJsonObject obj;
    obj[QLatin1String(JsonKeys::Name)] = name;
    obj[QLatin1String(JsonKeys::ShaderId)] = shaderId;
    obj[QLatin1String(JsonKeys::ShaderParams)] = QJsonObject::fromVariantMap(shaderParams);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString error = PzI18n::tr("Failed to save preset: %1", "@info").arg(file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcEditor) << error;
        return false;
    }

    QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        QString error = PzI18n::tr("Failed to write preset file: %1", "@info").arg(file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcEditor) << error;
        return false;
    }

    return true;
}

QVariantMap EditorController::loadShaderPreset(const QString& filePath)
{
    QVariantMap result;

    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetLoadFailed(PzI18n::tr("File path cannot be empty", "@info"));
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString error = PzI18n::tr("Failed to open preset file: %1", "@info").arg(file.errorString());
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QString error = PzI18n::tr("Invalid preset file: %1", "@info").arg(parseError.errorString());
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    if (!doc.isObject()) {
        QString error = PzI18n::tr("Preset file must contain a JSON object", "@info");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QJsonObject obj = doc.object();
    QString shaderId = obj[QLatin1String(JsonKeys::ShaderId)].toString();
    if (shaderId.isEmpty()) {
        QString error = PzI18n::tr("Preset file missing shader ID", "@info");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    // Validate that shader exists in available shaders
    bool shaderFound = false;
    for (const QVariant& shaderVar : m_availableShaders) {
        QVariantMap shaderMap = shaderVar.toMap();
        if (shaderMap[QLatin1String("id")].toString() == shaderId) {
            shaderFound = true;
            break;
        }
    }
    if (!shaderFound) {
        QString error = PzI18n::tr("Shader in preset is no longer available", "@info");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QVariantMap shaderParams;
    if (obj.contains(QLatin1String(JsonKeys::ShaderParams))) {
        shaderParams = obj[QLatin1String(JsonKeys::ShaderParams)].toObject().toVariantMap();
    }

    result[QLatin1String(JsonKeys::Name)] = obj[QLatin1String(JsonKeys::Name)].toString();
    result[QLatin1String(JsonKeys::ShaderId)] = shaderId;
    result[QLatin1String(JsonKeys::ShaderParams)] = shaderParams;

    return result;
}

QString EditorController::shaderPresetDirectory()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shader-presets");

    QDir dir(path);
    if (!dir.exists()) {
        if (dir.mkpath(QStringLiteral("."))) {
            qCDebug(lcEditor) << "Created shader preset directory:" << dir.absolutePath();
        } else {
            qCWarning(lcEditor) << "Failed to create shader preset directory:" << path;
        }
    }

    return path;
}

// =============================================================================
// SHADER SUPPORT
// =============================================================================

QString EditorController::currentShaderId() const
{
    return m_currentShaderId;
}

QVariantMap EditorController::currentShaderParams() const
{
    return m_currentShaderParams;
}

QVariantList EditorController::currentShaderParameters() const
{
    // Return cached parameters - populated when shader is selected
    // This avoids D-Bus calls on every QML property access
    return m_cachedShaderParameters;
}

QString EditorController::noneShaderUuid() const
{
    return ShaderRegistry::noneShaderUuid();
}

void EditorController::setCurrentShaderId(const QString& id)
{
    // Validate: must be empty (no shader) or exist in available shaders
    bool isValid = id.isEmpty();
    if (!isValid) {
        for (const QVariant& shader : m_availableShaders) {
            if (shader.toMap().value(QLatin1String("id")).toString() == id) {
                isValid = true;
                break;
            }
        }
    }

    if (!isValid) {
        qCWarning(lcEditor) << "Invalid shader ID:" << id;
        return;
    }

    if (m_currentShaderId != id) {
        auto* cmd = new UpdateShaderIdCommand(this, m_currentShaderId, id);
        m_undoController->push(cmd);
    }
}

void EditorController::setCurrentShaderIdDirect(const QString& id)
{
    if (m_currentShaderId != id) {
        m_currentShaderId = id;

        // Update cached shader parameters
        if (id.isEmpty()) {
            m_cachedShaderParameters.clear();
        } else {
            QVariantMap info = getShaderInfo(id);
            m_cachedShaderParameters = info.value(QLatin1String("parameters")).toList();
        }

        markUnsaved();
        Q_EMIT currentShaderIdChanged();
        Q_EMIT currentShaderParametersChanged();
    }
}

void EditorController::setCurrentShaderParams(const QVariantMap& params)
{
    if (m_currentShaderParams != params) {
        // Create undo command for batch params change
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, params);
        m_undoController->push(cmd);
    }
}

void EditorController::setCurrentShaderParamsDirect(const QVariantMap& params)
{
    if (m_currentShaderParams != params) {
        m_currentShaderParams = params;
        markUnsaved();
        Q_EMIT currentShaderParamsChanged();
    }
}

void EditorController::setShaderParameter(const QString& key, const QVariant& value)
{
    if (m_currentShaderParams.value(key) != value) {
        // Create undo command for single param change (supports merging)
        QVariant oldValue = m_currentShaderParams.value(key);
        auto* cmd = new UpdateShaderParamsCommand(this, key, oldValue, value);
        m_undoController->push(cmd);
    }
}

void EditorController::setShaderParameterDirect(const QString& key, const QVariant& value)
{
    if (m_currentShaderParams.value(key) != value) {
        m_currentShaderParams[key] = value;
        markUnsaved();
        Q_EMIT currentShaderParamsChanged();
    }
}

void EditorController::resetShaderParameters()
{
    if (!m_currentShaderParams.isEmpty()) {
        // Create undo command for reset (batch change to empty)
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, QVariantMap(),
                                                  PzI18n::tr("Reset Shader Parameters", "@action"));
        m_undoController->push(cmd);
    }
}

void EditorController::switchShader(const QString& id, const QVariantMap& params)
{
    if (m_currentShaderId == id && m_currentShaderParams == params) {
        return;
    }

    m_undoController->beginMacro(PzI18n::tr("Switch Shader Effect", "@action"));
    setCurrentShaderId(id);
    setCurrentShaderParams(params);
    m_undoController->endMacro();
}

QVariantMap EditorController::stripStaleShaderParams(const QVariantMap& params) const
{
    if (params.isEmpty() || m_cachedShaderParameters.isEmpty()) {
        return params;
    }

    // Build set of valid param IDs for the current shader
    QSet<QString> validIds;
    for (const QVariant& paramVar : m_cachedShaderParameters) {
        QVariantMap paramDef = paramVar.toMap();
        QString id = paramDef.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            validIds.insert(id);
        }
    }

    if (validIds.isEmpty()) {
        return params;
    }

    // Keep only params that belong to the current shader
    QVariantMap result;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (validIds.contains(it.key())) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void EditorController::refreshAvailableShaders()
{
    m_shadersEnabled = ShaderDbusQueries::queryShadersEnabled();
    m_availableShaders = ShaderDbusQueries::queryAvailableShaders();

    Q_EMIT availableShadersChanged();
    Q_EMIT shadersEnabledChanged();
}

QVariantMap EditorController::getShaderInfo(const QString& shaderId) const
{
    return ShaderDbusQueries::queryShaderInfo(shaderId);
}

QImage EditorController::buildLabelsTexture(const QVariantList& zones, int width, int height) const
{
    if (zones.isEmpty() || width <= 0 || height <= 0) {
        return QImage();
    }
    return ZoneLabelTextureBuilder::build(zones, QSize(width, height), Qt::white, true);
}

QImage EditorController::loadWallpaperTexture() const
{
    return ShaderRegistry::loadWallpaperImage();
}

QVariant EditorController::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void EditorController::startAudioCapture()
{
    // Already running — nothing to do (settings were validated on initial start)
    if (m_cavaService && m_cavaService->isRunning()) {
        return;
    }
    // Respect the KCM setting — don't start CAVA if audio visualizer is disabled
    if (!SettingsDbusQueries::queryBoolSetting(QStringLiteral("enableAudioVisualizer"), false)) {
        return;
    }
    if (!CavaService::isAvailable()) {
        qCDebug(lcEditor) << "Audio spectrum: CAVA not available, disabled";
        return;
    }
    if (!m_cavaService) {
        m_cavaService = new CavaService(this);
        connect(m_cavaService, &CavaService::spectrumUpdated, this, [this](const QVector<float>& spectrum) {
            m_audioSpectrum = spectrum;
            Q_EMIT audioSpectrumChanged();
        });
    }
    // Sync bar count from KCM settings (default 64)
    const int barCount = SettingsDbusQueries::queryIntSetting(QStringLiteral("audioSpectrumBarCount"), 64);
    m_cavaService->setBarCount(barCount);
    m_cavaService->start();
}

void EditorController::stopAudioCapture()
{
    if (m_cavaService && m_cavaService->isRunning()) {
        m_cavaService->stop();
    }
    if (!m_audioSpectrum.isEmpty()) {
        m_audioSpectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
