// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsschema.h"

#include "configdefaults.h"
#include "configmigration.h"

#include <QtGlobal>

namespace PlasmaZones {

PhosphorConfig::Schema buildSettingsSchema()
{
    PhosphorConfig::Schema s;
    s.version = ConfigSchemaVersion;
    s.versionKey = ConfigKeys::versionKey();

    appendShadersSchema(s);

    return s;
}

// ─── Shaders ────────────────────────────────────────────────────────────────
// Controls: overall effect toggle, frame rate, audio visualizer, bar count.

void appendShadersSchema(PhosphorConfig::Schema& schema)
{
    const QString group = ConfigDefaults::shadersGroup();

    PhosphorConfig::KeyDef enabled;
    enabled.key = ConfigDefaults::enabledKey();
    enabled.defaultValue = ConfigDefaults::enableShaderEffects();
    enabled.expectedType = QMetaType::Bool;

    PhosphorConfig::KeyDef frameRate;
    frameRate.key = ConfigDefaults::frameRateKey();
    frameRate.defaultValue = ConfigDefaults::shaderFrameRate();
    frameRate.expectedType = QMetaType::Int;
    frameRate.validator = [](const QVariant& v) {
        return QVariant(qBound(ConfigDefaults::shaderFrameRateMin(), v.toInt(), ConfigDefaults::shaderFrameRateMax()));
    };

    PhosphorConfig::KeyDef audioVisualizer;
    audioVisualizer.key = ConfigDefaults::audioVisualizerKey();
    audioVisualizer.defaultValue = ConfigDefaults::enableAudioVisualizer();
    audioVisualizer.expectedType = QMetaType::Bool;

    PhosphorConfig::KeyDef barCount;
    barCount.key = ConfigDefaults::audioSpectrumBarCountKey();
    barCount.defaultValue = ConfigDefaults::audioSpectrumBarCount();
    barCount.expectedType = QMetaType::Int;
    barCount.validator = [](const QVariant& v) {
        return QVariant(
            qBound(ConfigDefaults::audioSpectrumBarCountMin(), v.toInt(), ConfigDefaults::audioSpectrumBarCountMax()));
    };

    schema.groups[group] = {enabled, frameRate, audioVisualizer, barCount};
}

} // namespace PlasmaZones
