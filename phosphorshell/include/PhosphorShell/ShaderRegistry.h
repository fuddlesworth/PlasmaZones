// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <memory>

namespace PhosphorShell {

class IWallpaperProvider;

/// Registry of available shader effects.
///
/// Discovers shaders from configured search paths, validates metadata,
/// manages parameter presets, and watches for file changes.
///
/// Unlike PlasmaZones' singleton, consumers create their own instances
/// and register search paths explicitly.
class PHOSPHORSHELL_EXPORT ShaderRegistry : public QObject
{
    Q_OBJECT

public:
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString group;
        QString type; ///< "float", "color", "int", "bool", "image"
        int slot = -1;
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false; ///< Hint: consumer may bind to app-specific color
        QString wrap;

        /// Convert slot to uniform name (e.g., slot 0 → "customParams1_x")
        QString uniformName() const;
    };

    struct ShaderInfo
    {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl;
        QString sourcePath;
        QString vertexShaderPath;
        QStringList bufferShaderPaths;
        QString previewPath;
        QString category;
        QList<ParameterInfo> parameters;
        QMap<QString, QVariantMap> presets;
        bool isUserShader = false;
        bool isMultipass = false;
        bool useWallpaper = false;
        bool bufferFeedback = false;
        qreal bufferScale = 1.0;
        QString bufferWrap = QStringLiteral("clamp");
        QStringList bufferWraps;
        QString bufferFilter = QStringLiteral("linear");
        QStringList bufferFilters;
        bool useDepthBuffer = false;

        bool isValid() const
        {
            return !id.isEmpty() && (ShaderRegistry::isNoneShader(id) || shaderUrl.isValid());
        }
    };

    explicit ShaderRegistry(QObject* parent = nullptr);
    ~ShaderRegistry() override;

    // ── Search paths ──────────────────────────────────────────────────

    /// Add a directory to search for shader subdirectories.
    void addSearchPath(const QString& path);

    /// Remove a previously added search path.
    void removeSearchPath(const QString& path);

    /// Current search paths.
    QStringList searchPaths() const;

    // ── Shader discovery ──────────────────────────────────────────────

    static QString noneShaderUuid();
    static bool isNoneShader(const QString& id);

    QList<ShaderInfo> availableShaders() const;
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    ShaderInfo shader(const QString& id) const;
    Q_INVOKABLE QVariantMap shaderInfo(const QString& id) const;
    Q_INVOKABLE QUrl shaderUrl(const QString& id) const;
    Q_INVOKABLE bool shadersEnabled() const;

    // ── Parameters & presets ──────────────────────────────────────────

    Q_INVOKABLE QVariantMap defaultParams(const QString& id) const;
    bool validateParams(const QString& id, const QVariantMap& params) const;
    QVariantMap validateAndCoerceParams(const QString& id, const QVariantMap& params) const;
    Q_INVOKABLE QVariantMap translateParamsToUniforms(const QString& shaderId, const QVariantMap& storedParams) const;
    Q_INVOKABLE QVariantMap presetParams(const QString& shaderId, const QString& presetName) const;
    Q_INVOKABLE QStringList shaderPresetNames(const QString& shaderId) const;
    Q_INVOKABLE QVariantList shaderPresetsVariant(const QString& shaderId) const;

    // ── Lifecycle ─────────────────────────────────────────────────────

    Q_INVOKABLE void refresh();

    void reportShaderBakeStarted(const QString& shaderId);
    void reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error);

    // ── Wallpaper ─────────────────────────────────────────────────────

    static QString wallpaperPath();
    static QImage loadWallpaperImage();
    static void invalidateWallpaperCache();

Q_SIGNALS:
    void shadersChanged();
    void shaderCompilationStarted(const QString& shaderId);
    void shaderCompilationFinished(const QString& shaderId, bool success, const QString& error);

private Q_SLOTS:
    void onShaderDirChanged(const QString& path);
    void onShaderFileChanged(const QString& path);
    void performDebouncedRefresh();

private:
    void loadShadersFromPath(const QString& searchPath, bool isUserShader);
    void loadShaderFromDir(const QString& shaderDir, bool isUserShader);
    ShaderInfo loadShaderMetadata(const QString& shaderDir);
    bool validateParameterValue(const ParameterInfo& param, const QVariant& value) const;
    void setupFileWatcher();
    void scheduleRefresh();
    QVariantMap shaderInfoToVariantMap(const ShaderInfo& info) const;
    QVariantMap parameterInfoToVariantMap(const ParameterInfo& param) const;

    QStringList m_searchPaths;
    QHash<QString, ShaderInfo> m_shaders;
    bool m_shadersEnabled = false;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;

    static std::unique_ptr<IWallpaperProvider> s_wallpaperProvider;
    static QString s_cachedWallpaperPath;
    static QImage s_cachedWallpaperImage;
    static qint64 s_cachedWallpaperMtime;
    static QMutex s_wallpaperCacheMutex;

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PhosphorShell
