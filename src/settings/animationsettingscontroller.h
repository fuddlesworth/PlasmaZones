// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"
#include "../pz_i18n.h"

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>
#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>
#include <PhosphorAnimationShaders/ShaderProfile.h>
#include <PhosphorAnimationShaders/ShaderProfileTree.h>

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

class Settings;

class AnimationSettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList availableTransitionEffects READ availableTransitionEffects NOTIFY effectsChanged)
    Q_PROPERTY(QStringList eventPaths READ eventPaths CONSTANT)

public:
    explicit AnimationSettingsController(Settings* settings,
                                         PhosphorAnimationShaders::AnimationShaderRegistry* registry,
                                         QObject* parent = nullptr);

    QVariantList availableTransitionEffects() const;
    QStringList eventPaths() const;

    Q_INVOKABLE QString effectForPath(const QString& eventPath) const;
    Q_INVOKABLE void setEffectForPath(const QString& eventPath, const QString& effectId);
    Q_INVOKABLE void clearEffectForPath(const QString& eventPath);

    Q_INVOKABLE QVariantMap parametersForPath(const QString& eventPath) const;
    Q_INVOKABLE void setParameterForPath(const QString& eventPath, const QString& paramId, const QVariant& value);

    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;
    Q_INVOKABLE QVariantMap shaderDefaults(const QString& effectId) const;

    Q_INVOKABLE QVariantMap effectMetadata(const QString& effectId) const;

    Q_INVOKABLE QString parentChainForEvent(const QString& eventPath) const;
    Q_INVOKABLE QString inheritSummaryForEvent(const QString& eventPath) const;

Q_SIGNALS:
    void effectsChanged();

private:
    Settings* m_settings = nullptr;
    PhosphorAnimationShaders::AnimationShaderRegistry* m_registry = nullptr;
};

} // namespace PlasmaZones
