// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/**
 * @brief Resolves #include directives in shader source (GLSL).
 *
 * Supports:
 * - #include "path" — search relative to current file's directory, then include paths
 * - #include <path> — search only in include paths (e.g. global shaders dir)
 *
 * Include paths are typically [shaderDirectory, systemShaderDirectory].
 * Recursion is limited to avoid cycles and runaway expansion.
 */
class PLASMAZONES_EXPORT ShaderIncludeResolver
{
public:
    /** Maximum include depth to avoid cycles and unbounded expansion. */
    static constexpr int MaxIncludeDepth = 10;

    /**
     * Expand #include directives in @p source.
     * @param source Raw shader source (may contain #include "..." or #include <...>)
     * @param currentFileDir Directory of the file that contains @p source (for relative "path")
     * @param includePaths List of directories to search for &lt;path&gt; and for "path" after currentFileDir
     * @param outError If non-null, set on error (file not found, depth exceeded, read error)
     * @return Expanded source, or empty string on error (check outError)
     */
    static QString expandIncludes(const QString& source,
                                 const QString& currentFileDir,
                                 const QStringList& includePaths,
                                 QString* outError = nullptr);
};

} // namespace PlasmaZones
