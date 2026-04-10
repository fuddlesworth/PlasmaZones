// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QFileSystemWatcher>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>

class QJsonObject;

namespace PlasmaZones {

/**
 * @brief Base class for shader registries (overlay and animation)
 *
 * Provides common infrastructure for scanning shader directories, parsing
 * metadata.json, parameter handling, file watching, and QML/D-Bus queries.
 * Subclasses provide directory names and hooks for extended metadata.
 */
class PLASMAZONES_EXPORT BaseShaderRegistry : public QObject
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
        int slot = -1; ///< Uniform slot: 0-31 for floats/int/bool, 0-15 for colors, 0-3 for images
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false;
        QString wrap; ///< Wrap mode for image type: "repeat" or "clamp" (default)

        /// Convert slot to uniform name (e.g., slot 0 -> "customParams1_x", color slot 1 -> "customColor2")
        QString uniformName() const;
    };

    /**
     * @brief Base shader metadata (shared fields only)
     */
    struct BaseShaderInfo
    {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl; ///< file:// URL to fragment shader (.glsl)
        QString sourcePath; ///< Path to fragment shader source
        QString previewPath; ///< Absolute path to preview.png
        QString category; ///< Hierarchical category path (e.g. "Organic", "Transition")
        QList<ParameterInfo> parameters;
        QMap<QString, QVariantMap> presets; ///< Named parameter presets (key=name, value=param ID->value, sorted)
        bool isUserShader = false; ///< True for ~/.local/share shaders

        virtual ~BaseShaderInfo() = default;

        virtual bool isValid() const
        {
            return !id.isEmpty() && shaderUrl.isValid();
        }
    };

    explicit BaseShaderRegistry(QObject* parent = nullptr);
    ~BaseShaderRegistry() override;

    /// Get all available shaders as QVariantList for D-Bus/QML
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    /// Get shader info as QVariantMap for D-Bus/QML
    Q_INVOKABLE QVariantMap shaderInfo(const QString& id) const;

    /// Get shader file:// URL to the fragment shader source
    Q_INVOKABLE QUrl shaderUrl(const QString& id) const;

    /// Get user shader directory path
    Q_INVOKABLE QString userShaderDirectory() const;

    /// Open user shader directory in file manager
    Q_INVOKABLE void openUserShaderDirectory() const;

    /// Get default parameters for a shader
    Q_INVOKABLE QVariantMap defaultParams(const QString& id) const;

    /// Get all presets for a shader as QVariantList for D-Bus/QML
    Q_INVOKABLE QVariantList shaderPresetsVariant(const QString& id) const;

    /// Get list of preset names for a shader
    Q_INVOKABLE QStringList shaderPresetNames(const QString& id) const;

    /// Get named preset parameter values for a shader
    Q_INVOKABLE QVariantMap presetParams(const QString& id, const QString& presetName) const;

    /// Validate and coerce params, returning map with defaults for invalid values
    Q_INVOKABLE QVariantMap validateAndCoerceParams(const QString& id, const QVariantMap& params) const;

    /// Translate stored parameter values (keyed by parameter ID) to shader uniforms
    Q_INVOKABLE QVariantMap translateParamsToUniforms(const QString& id, const QVariantMap& storedParams) const;

    /// Reload shader list (called on file changes, startup)
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void shadersChanged();

protected:
    /// Subclasses provide the subdirectory name under plasmazones/ for system shaders
    virtual QString systemDirName() const = 0;

    /// Subclasses provide the subdirectory name under plasmazones/ for user shaders
    virtual QString userDirName() const = 0;

    /// Hook for subclasses to parse additional metadata fields after base fields are parsed
    virtual void onShaderLoaded(const QString& id, const QJsonObject& root, const QString& shaderDir,
                                bool isUserShader) = 0;

    /// Hook for subclasses to remove their extended info when shader is removed
    virtual void onShaderRemoved(const QString& id)
    {
        Q_UNUSED(id)
    }

    /// Access the base shader map (for subclass queries)
    const QHash<QString, BaseShaderInfo>& baseShaders() const
    {
        return m_shaders;
    }

    /// Check if a shader ID exists in the registry
    bool hasShader(const QString& id) const
    {
        return m_shaders.contains(id);
    }

    void setupFileWatcher();
    void ensureUserShaderDirExists() const;
    QString systemShaderDir() const;
    QString userShaderDir() const;

    /// Watch a single shader directory (and its subdirs/files) for changes
    void watchShaderDirectory(const QString& dir, bool guardDuplicates);

    /// Watch all system + user shader directories
    void watchAllShaderDirectories(bool guardDuplicates);

    /// Convert shader name to deterministic UUID (v5)
    static QString shaderNameToUuid(const QString& name);

private Q_SLOTS:
    void onDirChanged(const QString& path);
    void onFileChanged(const QString& path);
    void performDebouncedRefresh();

private:
    void loadAllShaders();
    void loadShadersFromDir(const QString& dir, bool isUserShader);
    void loadShaderFromDir(const QString& shaderDir, bool isUserShader);
    BaseShaderInfo loadBaseMetadata(const QString& shaderDir, QJsonObject* outRoot = nullptr);
    ParameterInfo parseParameter(const QJsonObject& paramObj);
    void scheduleRefresh();
    QVariantMap shaderInfoToVariantMap(const BaseShaderInfo& info) const;

protected:
    /// Validate a single parameter value against its schema (used by subclass validateParams)
    bool validateParameterValue(const ParameterInfo& param, const QVariant& value) const;

private:
    QVariantMap parameterInfoToVariantMap(const ParameterInfo& param) const;

    QHash<QString, BaseShaderInfo> m_shaders;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;
    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PlasmaZones
