// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief D-Bus queries for daemon Shader/Settings service
 *
 * Centralizes D-Bus calls to the PlasmaZones daemon's Settings interface
 * for shader-related operations.
 */
namespace ShaderDbusQueries {

/**
 * @brief Query whether shaders are enabled on the system
 * @return true if shaders are enabled and supported, false otherwise
 *
 * This reflects system capability (RHI/shader support), not user preference.
 */
bool queryShadersEnabled();

/**
 * @brief Query the list of available shaders from the daemon
 * @return List of shader info maps, each containing at least "id" and "name" keys
 *
 * Returns empty list if D-Bus is unavailable or query fails.
 * Each map may contain: id, name, description, author, category, parameters, etc.
 */
QVariantList queryAvailableShaders();

/**
 * @brief Query information about a specific shader
 * @param shaderId The shader ID to query
 * @return Shader info map with metadata and parameter definitions
 *
 * Returns empty map if shader not found or D-Bus unavailable.
 * For "none" shader ID, returns empty map without D-Bus call.
 */
QVariantMap queryShaderInfo(const QString& shaderId);

/**
 * @brief Translate shader params from param IDs to uniform names for ZoneShaderItem
 * @param shaderId The shader ID
 * @param params Map of param IDs to values (e.g. {"intensity": 0.5})
 * @return Map of uniform names to values (e.g. {"customParams1_x": 0.5})
 *
 * Returns empty map if daemon unavailable or shader not found.
 */
QVariantMap queryTranslateShaderParams(const QString& shaderId, const QVariantMap& params);

} // namespace ShaderDbusQueries

} // namespace PlasmaZones
