// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <QObject>
#include <QStandardPaths>
#include <QStringLiteral>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Effects" settings page.
///
/// CONSTANT bounds for the shader-framerate and audio-spectrum bar-count
/// sliders, plus a one-shot `cavaAvailable` check (runs `which cava` at
/// controller construction). Live effect toggles are on Settings and
/// bind directly via `appSettings.X`.
class SnappingEffectsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int shaderFrameRateMin READ shaderFrameRateMin CONSTANT)
    Q_PROPERTY(int shaderFrameRateMax READ shaderFrameRateMax CONSTANT)
    Q_PROPERTY(int audioSpectrumBarCountMin READ audioSpectrumBarCountMin CONSTANT)
    Q_PROPERTY(int audioSpectrumBarCountMax READ audioSpectrumBarCountMax CONSTANT)
    Q_PROPERTY(bool cavaAvailable READ cavaAvailable CONSTANT)

public:
    using QObject::QObject;

    int shaderFrameRateMin() const
    {
        return ConfigDefaults::shaderFrameRateMin();
    }
    int shaderFrameRateMax() const
    {
        return ConfigDefaults::shaderFrameRateMax();
    }
    int audioSpectrumBarCountMin() const
    {
        return ConfigDefaults::audioSpectrumBarCountMin();
    }
    int audioSpectrumBarCountMax() const
    {
        return ConfigDefaults::audioSpectrumBarCountMax();
    }
    bool cavaAvailable() const
    {
        return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
    }
};

} // namespace PlasmaZones
