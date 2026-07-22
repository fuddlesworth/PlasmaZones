// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/UpdateShaderIdCommand.h"
#include "../undo/commands/UpdateShaderParamsCommand.h"
#include "../helpers/ShaderDbusQueries.h"
#include "../helpers/SettingsDbusQueries.h"
#include "../../config/configdefaults.h"
#include "core/interfaces/shaderregistry.h"
#include "core/platform/logging.h"
#include "../../shaderpreview/shaderpreviewcontroller.h"

#include "phosphor_i18n.h"

namespace PlasmaZones {

QVariantList EditorController::zonesForShaderPreview(int width, int height) const
{
    return m_shaderPreview->zonesForShaderPreview(width, height);
}

// ── IShaderPreviewBackend: the editor's preview data source ──
// D-Bus shader metadata from the daemon registry + the live edited layout's
// zones + the audio-visualizer config. (targetScreenSize() lives in
// controller/gaps.cpp and is the sixth backend method.)

QVariantList EditorController::previewZones() const
{
    return m_zoneManager ? m_zoneManager->zones() : QVariantList();
}

QVariantMap EditorController::shaderInfo(const QString& shaderId) const
{
    return ShaderDbusQueries::queryShaderInfo(shaderId);
}

QVariantMap EditorController::translateParams(const QString& shaderId, const QVariantMap& params) const
{
    return ShaderDbusQueries::queryTranslateShaderParams(shaderId, params);
}

bool EditorController::audioVisualizerEnabled() const
{
    return SettingsDbusQueries::queryBoolSetting(QStringLiteral("enableAudioVisualizer"),
                                                 ConfigDefaults::enableAudioVisualizer());
}

PhosphorAudio::SpectrumOptions EditorController::audioOptions() const
{
    // One batch round-trip for the full Shaders.Audio parameter set; an older
    // daemon omits unknown keys from the reply, and the type-checked
    // extractors below fall back to the compile defaults for those (same
    // skew handling as the KWin effect's loaders).
    static const QStringList kAudioKeys = {QStringLiteral("audioSpectrumBarCount"),
                                           QStringLiteral("shaderFrameRate"),
                                           QStringLiteral("audioAutosens"),
                                           QStringLiteral("audioSensitivity"),
                                           QStringLiteral("audioNoiseReduction"),
                                           QStringLiteral("audioLowerCutoffHz"),
                                           QStringLiteral("audioHigherCutoffHz"),
                                           QStringLiteral("audioMonstercat"),
                                           QStringLiteral("audioWaves"),
                                           QStringLiteral("audioChannelMode"),
                                           QStringLiteral("audioReverse"),
                                           QStringLiteral("audioExtraSmoothing"),
                                           QStringLiteral("audioInputMethod"),
                                           QStringLiteral("audioInputSource")};
    const QVariantMap values = SettingsDbusQueries::querySettingsBatch(kAudioKeys);
    const auto intOr = [&values](const QString& key, int fallback) {
        bool ok = false;
        const int value = values.value(key).toInt(&ok);
        return ok ? value : fallback;
    };
    const auto boolOr = [&values](const QString& key, bool fallback) {
        const QVariant value = values.value(key);
        return value.typeId() == QMetaType::Bool ? value.toBool() : fallback;
    };
    const auto stringOr = [&values](const QString& key, const QString& fallback) {
        const QString value = values.value(key).toString();
        return value.isEmpty() ? fallback : value;
    };

    PhosphorAudio::SpectrumOptions opts;
    opts.barCount = intOr(QStringLiteral("audioSpectrumBarCount"), ConfigDefaults::audioSpectrumBarCount());
    opts.framerate = intOr(QStringLiteral("shaderFrameRate"), ConfigDefaults::shaderFrameRate());
    opts.autosens = boolOr(QStringLiteral("audioAutosens"), ConfigDefaults::audioAutosens());
    opts.sensitivity = intOr(QStringLiteral("audioSensitivity"), ConfigDefaults::audioSensitivity());
    opts.noiseReduction = intOr(QStringLiteral("audioNoiseReduction"), ConfigDefaults::audioNoiseReduction());
    opts.lowerCutoffHz = intOr(QStringLiteral("audioLowerCutoffHz"), ConfigDefaults::audioLowerCutoffHz());
    opts.higherCutoffHz = intOr(QStringLiteral("audioHigherCutoffHz"), ConfigDefaults::audioHigherCutoffHz());
    opts.monstercat = boolOr(QStringLiteral("audioMonstercat"), ConfigDefaults::audioMonstercat());
    opts.waves = boolOr(QStringLiteral("audioWaves"), ConfigDefaults::audioWaves());
    opts.channelMode = PhosphorAudio::channelModeFromString(
        stringOr(QStringLiteral("audioChannelMode"), ConfigDefaults::audioChannelMode()));
    opts.reverse = boolOr(QStringLiteral("audioReverse"), ConfigDefaults::audioReverse());
    opts.extraSmoothing = PhosphorAudio::extraSmoothingFromPercent(
        intOr(QStringLiteral("audioExtraSmoothing"), ConfigDefaults::audioExtraSmoothing()));
    opts.inputMethod = PhosphorAudio::inputMethodFromSetting(
        stringOr(QStringLiteral("audioInputMethod"), ConfigDefaults::audioInputMethod()));
    opts.inputSource = stringOr(QStringLiteral("audioInputSource"), ConfigDefaults::audioInputSource());
    return opts;
}

QVariantMap EditorController::translateShaderParams(const QString& shaderId, const QVariantMap& params) const
{
    return m_shaderPreview->translateShaderParams(shaderId, params);
}

bool EditorController::saveShaderPreset(const QString& filePath, const QString& shaderId,
                                        const QVariantMap& shaderParams, const QString& presetName)
{
    return m_shaderPreview->saveShaderPreset(filePath, shaderId, shaderParams, presetName);
}

QVariantMap EditorController::loadShaderPreset(const QString& filePath)
{
    return m_shaderPreview->loadShaderPreset(filePath);
}

QString EditorController::shaderPresetDirectory() const
{
    return m_shaderPreview->shaderPresetDirectory();
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

QVariantMap EditorController::presetParams(const QString& shaderId, const QString& presetName) const
{
    if (shaderId.isEmpty() || presetName.isEmpty())
        return {};
    for (const QVariant& shaderVar : m_availableShaders) {
        const QVariantMap shader = shaderVar.toMap();
        if (shader.value(QLatin1String("id")).toString() != shaderId)
            continue;
        const QVariantList presets = shader.value(QLatin1String("presets")).toList();
        for (const QVariant& presetVar : presets) {
            const QVariantMap preset = presetVar.toMap();
            if (preset.value(QLatin1String("name")).toString() == presetName)
                return preset.value(QLatin1String("params")).toMap();
        }
        break;
    }
    return {};
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
        if (m_undoController) {
            m_undoController->push(cmd);
        } else {
            delete cmd;
        }
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
            if (info.contains(QLatin1String("parameters"))) {
                m_cachedShaderParameters = info.value(QLatin1String("parameters")).toList();
            } else {
                m_cachedShaderParameters.clear();
            }
        }

        markUnsaved();
        Q_EMIT currentShaderIdChanged();
        Q_EMIT currentShaderParametersChanged();
    }
}

void EditorController::setCurrentShaderParams(const QVariantMap& params)
{
    if (m_currentShaderParams != params) {
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, params);
        if (m_undoController) {
            m_undoController->push(cmd);
        } else {
            delete cmd;
        }
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
        QVariant oldValue = m_currentShaderParams.value(key);
        auto* cmd = new UpdateShaderParamsCommand(this, key, oldValue, value);
        if (m_undoController) {
            m_undoController->push(cmd);
        } else {
            delete cmd;
        }
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
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, QVariantMap(),
                                                  PhosphorI18n::tr("Reset Shader Parameters", "@action"));
        if (m_undoController) {
            m_undoController->push(cmd);
        } else {
            delete cmd;
        }
    }
}

void EditorController::switchShader(const QString& id, const QVariantMap& params)
{
    if (m_currentShaderId == id && m_currentShaderParams == params) {
        return;
    }

    if (m_undoController) {
        m_undoController->beginMacro(PhosphorI18n::tr("Switch Shader Effect", "@action"));
    }
    setCurrentShaderId(id);
    setCurrentShaderParams(params);
    if (m_undoController) {
        m_undoController->endMacro();
    }
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
    return m_shaderPreview->getShaderInfo(shaderId);
}

QString EditorController::shaderParamPreamble(const QString& shaderId) const
{
    return m_shaderPreview->shaderParamPreamble(shaderId);
}

QImage EditorController::buildLabelsTexture(const QVariantList& zones, int width, int height) const
{
    return m_shaderPreview->buildLabelsTexture(zones, width, height);
}

QImage EditorController::loadWallpaperTexture() const
{
    return m_shaderPreview->loadWallpaperTexture();
}

QVariant EditorController::audioSpectrumVariant() const
{
    return m_shaderPreview->audioSpectrumVariant();
}

void EditorController::startAudioCapture()
{
    m_shaderPreview->startAudioCapture();
}

void EditorController::stopAudioCapture()
{
    m_shaderPreview->stopAudioCapture();
}

} // namespace PlasmaZones
