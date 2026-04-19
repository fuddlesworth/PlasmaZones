// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QVariant>

namespace PlasmaZones {

class ShaderRegistry;

/**
 * @brief D-Bus adaptor for shader management
 *
 * Provides D-Bus interface: org.plasmazones.Shader
 *
 * Exposes shader metadata, compilation lifecycle, and user shader directory
 * monitoring. All methods delegate to ShaderRegistry.
 *
 * Shader assignment remains at the PhosphorZones::Layout level (via PhosphorZones::LayoutManager interface).
 * This interface provides shader discovery, parameter introspection, and
 * compilation feedback for the Shader Editor and other consumers.
 */
class PLASMAZONES_EXPORT ShaderAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Shader")

public:
    explicit ShaderAdaptor(ShaderRegistry* registry, QObject* parent = nullptr);
    ~ShaderAdaptor() override = default;

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Shader discovery and metadata
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get list of available shader effects
     * @return List of shader metadata (id, name, description, author, category, etc.)
     */
    QVariantList availableShaders();

    /**
     * @brief Get detailed information about a specific shader
     * @param shaderId UUID of the shader
     * @return Shader metadata map, or empty map if not found
     */
    QVariantMap shaderInfo(const QString& shaderId);

    /**
     * @brief Get default parameter values for a shader
     * @param shaderId UUID of the shader
     * @return Map of parameter IDs to default values
     */
    QVariantMap defaultShaderParams(const QString& shaderId);

    /**
     * @brief Translate shader params from param IDs to uniform names
     * @param shaderId UUID of the shader
     * @param params Map of param IDs to values (e.g. {"intensity": 0.5})
     * @return Map of uniform names to values (e.g. {"customParams1_x": 0.5})
     */
    QVariantMap translateShaderParams(const QString& shaderId, const QVariantMap& params);

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader capability queries
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if shader effects are enabled (compiled with shader support)
     */
    bool shadersEnabled();

    /**
     * @brief Check if user-installed shaders are supported
     */
    bool userShadersEnabled();

    /**
     * @brief Get the user shader installation directory path
     * @return Path to ~/.local/share/plasmazones/shaders
     */
    QString userShaderDirectory();

    /**
     * @brief Open the user shader directory in the file manager
     */
    void openUserShaderDirectory();

    /**
     * @brief Refresh the shader registry (reload all shaders from disk)
     */
    void refreshShaders();

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Compilation lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Shader compilation/bake started
     * @param shaderId Shader being compiled
     */
    void shaderCompilationStarted(const QString& shaderId);

    /**
     * @brief Shader compilation/bake completed
     * @param shaderId Shader that was compiled
     * @param success True if compilation succeeded
     * @param error Error message if !success
     */
    void shaderCompilationFinished(const QString& shaderId, bool success, const QString& error);

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader registry changes
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Shader list changed (shaders added, removed, or reloaded)
     */
    void shadersChanged();

private:
    ShaderRegistry* m_registry;
};

} // namespace PlasmaZones
