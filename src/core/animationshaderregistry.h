// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "baseshaderregistry.h"

#include <QDir>
#include <QHash>

namespace PlasmaZones {

/**
 * @brief Registry for animation shader effects
 *
 * Scans plasmazones/animations/ directories for shader-based animation effects.
 * Animation shaders receive a pz_progress uniform [0..1] and transform the
 * window texture during snap/unsnap animations. They support optional vertex
 * shaders for mesh deformation (wave, ripple, fold, etc.) and configurable
 * polygon subdivision to provide enough vertices for smooth deformation.
 */
class PLASMAZONES_EXPORT AnimationShaderRegistry : public BaseShaderRegistry
{
    Q_OBJECT

public:
    /**
     * @brief Extended animation shader metadata
     *
     * Adds vertex shader path and subdivision hints on top of BaseShaderInfo.
     */
    struct AnimationShaderInfo : public BaseShaderInfo
    {
        QString vertexShaderPath; ///< Optional vertex shader (empty = KWin default)
        QString kwinFragmentShaderPath; ///< KWin-specific fragment shader (GLSL 1.10, individual uniforms)
        int subdivisions = 1; ///< Recommended grid subdivision (1 = single quad)
        QString geometryMode; ///< C++ geometry mode: "morph" (default), "popin", "slidefade"
    };

    explicit AnimationShaderRegistry(QObject* parent = nullptr);
    ~AnimationShaderRegistry() override;

    /// Singleton access (created by Daemon)
    static AnimationShaderRegistry* instance();

    /// Get all animation shaders as variant list for QML
    Q_INVOKABLE QVariantList availableAnimationShadersVariant() const;

    /// Get animation shader info by ID (includes vertexShaderPath + subdivisions)
    Q_INVOKABLE QVariantMap animationShaderInfo(const QString& id) const;

    /// Get extended animation shader info struct by ID
    AnimationShaderInfo animationShader(const QString& id) const;

    /// Reload animation shaders; skipped when shaders are disabled at build time.
    Q_INVOKABLE void refresh();

protected:
    QString systemDirName() const override
    {
        return QStringLiteral("plasmazones/animations");
    }

    QString userDirName() const override
    {
        return QStringLiteral("plasmazones/animations");
    }

    void onShaderLoaded(const QString& id, const QJsonObject& root, const QString& shaderDir,
                        bool isUserShader) override;
    void onShaderRemoved(const QString& id) override;

private:
    QVariantMap animationShaderInfoToVariantMap(const AnimationShaderInfo& info) const;

    QHash<QString, AnimationShaderInfo> m_animationShaders;
    static AnimationShaderRegistry* s_instance;
};

} // namespace PlasmaZones
