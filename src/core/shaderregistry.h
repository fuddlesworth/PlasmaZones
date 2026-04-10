// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "baseshaderregistry.h"
#include "wallpaperprovider.h"
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QStringList>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Registry of available overlay shader effects
 *
 * Singleton owned by Daemon, exposed to editor via SettingsAdaptor D-Bus.
 * Extends BaseShaderRegistry with overlay-specific metadata: multipass,
 * wallpaper subscription, buffer feedback, vertex shaders, depth buffers.
 */
class PLASMAZONES_EXPORT ShaderRegistry : public BaseShaderRegistry
{
    Q_OBJECT

public:
    // Re-export ParameterInfo from base for backward compatibility
    using ParameterInfo = BaseShaderRegistry::ParameterInfo;

    /**
     * @brief Complete overlay shader metadata (extends BaseShaderInfo)
     */
    struct ShaderInfo : public BaseShaderInfo
    {
        QString vertexShaderPath; ///< Path to vertex shader
        QStringList bufferShaderPaths; ///< Up to 4 buffer pass fragment shaders (A->B->C->D order)

        bool isMultipass = false; ///< True if multipass and bufferShader are set
        bool useWallpaper = false; ///< True if shader subscribes to desktop wallpaper texture (binding 11)
        bool bufferFeedback = false; ///< True to enable ping-pong (buffer pass samples its own previous frame)
        qreal bufferScale = 1.0; ///< Buffer resolution scale (e.g. 0.5 = half size); clamped 0.125-1.0
        QString bufferWrap = QStringLiteral("clamp"); ///< "clamp" or "repeat" for iChannel0 sampler
        QStringList bufferWraps; ///< Per-channel wrap modes (up to 4); empty = all use bufferWrap
        QString bufferFilter = QStringLiteral("linear"); ///< "nearest", "linear", or "mipmap"
        QStringList bufferFilters; ///< Per-channel filter modes (up to 4); empty = all use bufferFilter
        bool useDepthBuffer = false; ///< True if shader writes to a depth (R32F) attachment at location 1

        bool isValid() const override
        {
            return !id.isEmpty() && (ShaderRegistry::isNoneShader(id) || shaderUrl.isValid());
        }
    };

    explicit ShaderRegistry(QObject* parent = nullptr);
    ~ShaderRegistry() override;

    /**
     * Singleton access (created by Daemon)
     */
    static ShaderRegistry* instance();

    /**
     * Returns empty string (no shader)
     */
    static QString noneShaderUuid();

    /**
     * Check if shader ID is empty (no effect)
     */
    static bool isNoneShader(const QString& id);

    /**
     * Get all available shaders (overlay-specific ShaderInfo)
     */
    QList<ShaderInfo> availableShaders() const;

    /**
     * Get specific shader info (returns invalid ShaderInfo if not found)
     */
    ShaderInfo shader(const QString& id) const;

    /**
     * Get shader info as QVariantMap for D-Bus/QML (overlay-enriched)
     */
    Q_INVOKABLE QVariantMap shaderInfo(const QString& id) const;

    /**
     * Get shader list as QVariantList for D-Bus/QML (overlay-enriched)
     */
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    /**
     * Check if shaders are available (Qt6::ShaderTools was found at build)
     */
    Q_INVOKABLE bool shadersEnabled() const;

    /**
     * Check if user can create custom shaders
     */
    Q_INVOKABLE bool userShadersEnabled() const;

    /**
     * Reload shader list; skipped when shaders are disabled at build time.
     */
    Q_INVOKABLE void refresh();

    /**
     * Validate shader parameters against schema
     */
    bool validateParams(const QString& id, const QVariantMap& params) const;

    /**
     * Report shader bake started (e.g. before cache warming). Emits shaderCompilationStarted.
     */
    void reportShaderBakeStarted(const QString& shaderId);
    /**
     * Report shader bake result (e.g. from cache warming). Emits shaderCompilationFinished.
     */
    void reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error);

    /**
     * Resolve the current desktop wallpaper image path via the
     * pluggable IWallpaperProvider.
     */
    static QString wallpaperPath();

    /**
     * Load the current Plasma wallpaper as an RGBA8888 QImage.
     */
    static QImage loadWallpaperImage();

    /**
     * Clear cached wallpaper path and image so the next call re-reads from config.
     */
    static void invalidateWallpaperCache();

Q_SIGNALS:
    void shaderCompilationStarted(const QString& shaderId);
    void shaderCompilationFinished(const QString& shaderId, bool success, const QString& error);

protected:
    QString systemDirName() const override
    {
        return QStringLiteral("plasmazones/shaders");
    }

    QString userDirName() const override
    {
        return QStringLiteral("plasmazones/shaders");
    }

    void onShaderLoaded(const QString& id, const QJsonObject& root, const QString& shaderDir,
                        bool isUserShader) override;
    void onShaderRemoved(const QString& id) override;

private:
    QVariantMap overlayShaderInfoToVariantMap(const ShaderInfo& info) const;

    QHash<QString, ShaderInfo> m_overlayShaders; ///< Extended overlay-specific info
    bool m_shadersEnabled = false;

    static ShaderRegistry* s_instance;
    static std::unique_ptr<IWallpaperProvider> s_wallpaperProvider;
    static QString s_cachedWallpaperPath;
    static QImage s_cachedWallpaperImage;
    static qint64 s_cachedWallpaperMtime;
    static QMutex s_wallpaperCacheMutex;
};

} // namespace PlasmaZones
