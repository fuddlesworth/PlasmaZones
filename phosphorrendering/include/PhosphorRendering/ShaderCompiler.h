// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorrendering_export.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <rhi/qshader.h>
#include <rhi/qshaderbaker.h>

namespace PhosphorRendering {

/// Static utility for GLSL → SPIR-V compilation with include resolution and caching.
///
/// Compilation results are cached by source hash to avoid redundant QShaderBaker
/// invocations. The cache is in-memory only — cleared on process restart.
///
/// All methods are thread-safe (render threads may compile concurrently).
class PHOSPHORRENDERING_EXPORT ShaderCompiler
{
public:
    struct Result
    {
        QShader shader;
        bool success = false;
        QString error;
    };

    /// Compile GLSL source to QShader (SPIR-V 1.0 + GLSL 330 + ES 300/310/320).
    /// Cached by source hash; second call with same source returns immediately.
    static Result compile(const QByteArray& source, QShader::Stage stage);

    /// Load shader from file, expand #include directives, then compile.
    /// @param path         Path to .frag or .vert file
    /// @param includePaths Directories to search for #include directives
    static Result compileFromFile(const QString& path, const QStringList& includePaths);

    /// Load a shader file and expand #include directives without compiling.
    /// @param path         Path to .frag or .vert file
    /// @param includePaths Directories to search for #include directives
    /// @param outError     If non-null, set on error
    /// @return Expanded GLSL source, or empty string on error
    static QString loadAndExpand(const QString& path, const QStringList& includePaths, QString* outError = nullptr);

    /// Clear the in-memory compilation cache (e.g. on shader hot-reload).
    static void clearCache();

    /// Bake target list: SPIR-V 1.0, GLSL 330, GLSL ES 300/310/320.
    static const QList<QShaderBaker::GeneratedShader>& bakeTargets();
};

} // namespace PhosphorRendering
