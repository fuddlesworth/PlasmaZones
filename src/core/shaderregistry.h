// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QFileSystemWatcher>
#include <QTimer>

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
    struct ParameterInfo {
        QString id;
        QString name;
        QString type;            ///< "float", "color", "int", "bool"
        int slot = -1;           ///< Uniform slot: 0-15 for floats, 0-3 for colors
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false;

        /// Convert slot to uniform name (e.g., slot 0 → "customParams1_x", color slot 1 → "customColor2")
        QString uniformName() const;
    };


    /**
     * @brief Complete shader metadata
     */
    struct ShaderInfo {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl;          ///< file:// URL to fragment shader (.glsl)
        QString sourcePath;      ///< Path to fragment shader source
        QString vertexShaderPath;///< Path to vertex shader
        QString previewPath;     ///< Absolute path to preview.png
        QList<ParameterInfo> parameters;

        bool isUserShader = false;      ///< True for ~/.local/share shaders

        bool isValid() const {
            return !id.isEmpty() && (ShaderRegistry::isNoneShader(id) || shaderUrl.isValid());
        }
    };

    explicit ShaderRegistry(QObject *parent = nullptr);
    ~ShaderRegistry() override;

    /**
     * Singleton access (created by Daemon)
     */
    static ShaderRegistry *instance();

    /**
     * Returns empty string (the "no shader" value)
     */
    static QString noneShaderUuid();

    /**
     * Check if shader ID means "no effect" (empty, "none", or null UUID)
     */
    static bool isNoneShader(const QString &id);

    /**
     * Get all available shaders (includes "No Effect" entry with empty ID)
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
    ShaderInfo shader(const QString &id) const;

    /**
     * Get shader info as QVariantMap for D-Bus/QML
     */
    Q_INVOKABLE QVariantMap shaderInfo(const QString &id) const;

    /**
     * Get shader .qsb URL (returns empty if not found or "none")
     */
    Q_INVOKABLE QUrl shaderUrl(const QString &id) const;

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
     * Validate shader parameters against schema
     */
    bool validateParams(const QString &id, const QVariantMap &params) const;

    /**
     * Validate and coerce params, returning map with defaults for invalid values
     */
    QVariantMap validateAndCoerceParams(const QString &id, const QVariantMap &params) const;

    /**
     * Get default parameters for a shader
     */
    Q_INVOKABLE QVariantMap defaultParams(const QString &id) const;

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
    Q_INVOKABLE QVariantMap translateParamsToUniforms(const QString &shaderId, const QVariantMap &storedParams) const;

    /**
     * Reload shader list (called on file changes, startup)
     */
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void shadersChanged();
    void shaderCompilationStarted(const QString &shaderId);
    void shaderCompilationFinished(const QString &shaderId, bool success, const QString &error);

private Q_SLOTS:
    void onUserShaderDirChanged(const QString &path);
    void performDebouncedRefresh();

private:
    void loadSystemShaders();
    void loadUserShaders();
    void loadShaderFromDir(const QString &shaderDir, bool isUserShader);
    ShaderInfo loadShaderMetadata(const QString &shaderDir);
    bool validateParameterValue(const ParameterInfo &param, const QVariant &value) const;
    void setupFileWatcher();
    void ensureUserShaderDirExists() const;
    QVariantMap shaderInfoToVariantMap(const ShaderInfo &info) const;
    QVariantMap parameterInfoToVariantMap(const ParameterInfo &param) const;

    QHash<QString, ShaderInfo> m_shaders;
    bool m_shadersEnabled = false;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refreshTimer = nullptr;

    static ShaderRegistry *s_instance;
    static QString systemShaderDir();
    static QString userShaderDir();

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PlasmaZones
