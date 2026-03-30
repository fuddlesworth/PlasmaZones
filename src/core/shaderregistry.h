// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "wallpaperprovider.h"
#include <QObject>
#include <QHash>
#include <QImage>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QFileSystemWatcher>
#include <QMutex>
#include <QTimer>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Registry of available shader effects
 *
 * Singleton owned by Daemon, exposed to editor via SettingsAdaptor D-Bus.
 * Loads system shaders (raw GLSL for QSGRenderNode) and user shaders.
 * Watches user shader directory for changes.
 */
class PLASMAZONES_EXPORT ShaderRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Shader parameter metadata
     */
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString group; ///< Optional group for UI organization (collapsible sections)
        QString type; ///< "float", "color", "int", "bool", "image"
        int slot = -1; ///< Uniform slot: 0-15 for floats, 0-7 for colors, 0-3 for images
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false;
        QString wrap; ///< Wrap mode for image type: "repeat" or "clamp" (default)

        /// Convert slot to uniform name (e.g., slot 0 → "customParams1_x", color slot 1 → "customColor2")
        QString uniformName() const;
    };

    /**
     * @brief Complete shader metadata
     */
    struct ShaderInfo
    {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl; ///< file:// URL to fragment shader (.glsl)
        QString sourcePath; ///< Path to fragment shader source
        QString vertexShaderPath; ///< Path to vertex shader
        QString bufferShaderPath; ///< Path to first buffer pass shader (backward compat)
        QStringList bufferShaderPaths; ///< Up to 4 buffer pass fragment shaders (A→B→C→D order)
        QString previewPath; ///< Absolute path to preview.png
        QString category; ///< Hierarchical category path (e.g. "Organic", "Audio Visualizer")
        QList<ParameterInfo> parameters;

        QHash<QString, QVariantMap> presets; ///< Named parameter presets (key=name, value=param ID→value)

        bool isUserShader = false; ///< True for ~/.local/share shaders
        bool isMultipass = false; ///< True if multipass and bufferShader are set
        bool useWallpaper = false; ///< True if shader subscribes to desktop wallpaper texture (binding 11)
        bool bufferFeedback = false; ///< True to enable ping-pong (buffer pass samples its own previous frame)
        qreal bufferScale = 1.0; ///< Buffer resolution scale (e.g. 0.5 = half size); clamped 0.125–1.0
        QString bufferWrap = QStringLiteral("clamp"); ///< "clamp" or "repeat" for iChannel0 sampler
        QStringList bufferWraps; ///< Per-channel wrap modes (up to 4); empty = all use bufferWrap
        bool useDepthBuffer = false; ///< True if shader writes to a depth (R32F) attachment at location 1

        QString computeShaderPath; ///< Optional compute shader (.comp) for particle systems
        int particleCount = 0; ///< Number of particles (0 = no compute shader)

        bool isValid() const
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
     * Get all available shaders
     * @note Returns by value since internal storage is QHash
     */
    QList<ShaderInfo> availableShaders() const;

    /**
     * Get shader list as QVariantList for D-Bus/QML
     */
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    /**
     * Get specific shader info (returns invalid ShaderInfo if not found)
     */
    ShaderInfo shader(const QString& id) const;

    /**
     * Get shader info as QVariantMap for D-Bus/QML
     */
    Q_INVOKABLE QVariantMap shaderInfo(const QString& id) const;

    /**
     * Get shader file:// URL to the fragment shader source (returns empty if not found or "none")
     */
    Q_INVOKABLE QUrl shaderUrl(const QString& id) const;

    /**
     * Check if shaders are available (Qt6::ShaderTools was found at build)
     */
    Q_INVOKABLE bool shadersEnabled() const;

    /**
     * Check if user can create custom shaders
     */
    Q_INVOKABLE bool userShadersEnabled() const;

    /**
     * Get user shader directory path
     */
    Q_INVOKABLE QString userShaderDirectory() const;

    /**
     * Open user shader directory in file manager
     */
    Q_INVOKABLE void openUserShaderDirectory() const;

    /**
     * Get named preset parameter values for a shader
     * @param shaderId Shader UUID
     * @param presetName Name of the preset
     * @return Parameter values (keyed by param ID), validated and filled with defaults
     */
    Q_INVOKABLE QVariantMap presetParams(const QString& shaderId, const QString& presetName) const;

    /**
     * Get list of preset names for a shader
     */
    Q_INVOKABLE QStringList shaderPresetNames(const QString& shaderId) const;

    /**
     * Get all presets for a shader as QVariantList for D-Bus/QML
     * Each entry: {name: "Blue", params: {speed: 0.2, ...}}
     */
    Q_INVOKABLE QVariantList shaderPresetsVariant(const QString& shaderId) const;

    /**
     * Validate shader parameters against schema
     */
    bool validateParams(const QString& id, const QVariantMap& params) const;

    /**
     * Validate and coerce params, returning map with defaults for invalid values
     */
    QVariantMap validateAndCoerceParams(const QString& id, const QVariantMap& params) const;

    /**
     * Get default parameters for a shader
     */
    Q_INVOKABLE QVariantMap defaultParams(const QString& id) const;

    /**
     * Translate stored parameter values (keyed by parameter ID) to shader uniforms (keyed by mapsTo)
     *
     * Shader parameters are stored with their semantic IDs (e.g., "glowIntensity")
     * but the shader expects uniform names (e.g., "customParams1_x").
     * This method performs the translation using the shader's parameter definitions.
     *
     * @param shaderId The shader to translate parameters for
     * @param storedParams Parameters with ID keys (from layout's shaderParams)
     * @return Parameters with mapsTo keys (for shader uniforms)
     */
    Q_INVOKABLE QVariantMap translateParamsToUniforms(const QString& shaderId, const QVariantMap& storedParams) const;

    /**
     * Reload shader list (called on file changes, startup)
     */
    Q_INVOKABLE void refresh();

    /**
     * Report shader bake started (e.g. before cache warming). Emits shaderCompilationStarted.
     */
    void reportShaderBakeStarted(const QString& shaderId);
    /**
     * Report shader bake result (e.g. from cache warming). Emits shaderCompilationFinished.
     * Call from the main thread when a background bake completes.
     */
    void reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error);

    /**
     * Resolve the current desktop wallpaper image path via the
     * pluggable IWallpaperProvider.  Auto-detects KDE, Hyprland,
     * Sway, GNOME.  Result is cached; returns empty on failure.
     */
    static QString wallpaperPath();

    /**
     * Load the current Plasma wallpaper as an RGBA8888 QImage.
     * Caches the decoded image; invalidates if the file path or mtime changes.
     * Returns a null QImage if no wallpaper is available.
     */
    static QImage loadWallpaperImage();

    /**
     * Clear cached wallpaper path and image so the next call re-reads from config.
     */
    static void invalidateWallpaperCache();

Q_SIGNALS:
    void shadersChanged();
    void shaderCompilationStarted(const QString& shaderId);
    void shaderCompilationFinished(const QString& shaderId, bool success, const QString& error);

private Q_SLOTS:
    void onUserShaderDirChanged(const QString& path);
    void onShaderFileChanged(const QString& path);
    void performDebouncedRefresh();

private:
    void loadSystemShaders();
    void loadUserShaders();
    void loadShaderFromDir(const QString& shaderDir, bool isUserShader);
    ShaderInfo loadShaderMetadata(const QString& shaderDir);
    bool validateParameterValue(const ParameterInfo& param, const QVariant& value) const;
    void setupFileWatcher();
    void scheduleRefresh();
    void reWatchShaderFiles();
    void ensureUserShaderDirExists() const;
    QVariantMap shaderInfoToVariantMap(const ShaderInfo& info) const;
    QVariantMap parameterInfoToVariantMap(const ParameterInfo& param) const;

    QHash<QString, ShaderInfo> m_shaders;
    bool m_shadersEnabled = false;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;

    static ShaderRegistry* s_instance;
    static QString systemShaderDir();
    static QString userShaderDir();
    static std::unique_ptr<IWallpaperProvider> s_wallpaperProvider;
    static QString s_cachedWallpaperPath;
    static QImage s_cachedWallpaperImage;
    static qint64 s_cachedWallpaperMtime;
    static QMutex s_wallpaperCacheMutex;

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PlasmaZones
