// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/phosphorrendering_export.h>

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
/// @par Thread-safety
/// All methods are safe to call from any thread. Cache reads are lock-free for
/// already-baked sources. Cache misses serialize on an internal bake mutex —
/// QShaderBaker (glslang) is not reentrant, and concurrent bake() calls crash
/// inside QSpirvCompiler::compileToSpirv(), so the mutex is load-bearing and
/// must not be removed. loadAndExpand() is pure I/O and runs concurrently.
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
    /// @param path             Path to .frag or .vert file
    /// @param includePaths     Directories to search for #include directives
    /// @param outError         If non-null, set on error
    /// @param outIncludedPaths If non-null, appended with the canonical
    ///                         absolute path of every transitively-included
    ///                         header. Lets callers fingerprint includes
    ///                         so cache-invalidation responds to header
    ///                         edits, not just edits to the top-level file.
    /// @return Expanded GLSL source, or empty string on error
    static QString loadAndExpand(const QString& path, const QStringList& includePaths, QString* outError = nullptr,
                                 QStringList* outIncludedPaths = nullptr);

    /// Expand #include directives in already-read @p source, without touching
    /// the filesystem for the top-level text. Shares its search-path policy
    /// with `loadAndExpand` (`currentFileDir` searched first, then
    /// `includePaths`), so a caller that must transform the raw source before
    /// expansion — e.g. the T1.4 entry-point harness, which prepends a
    /// `#version`/include scaffold to an entry-only pack — gets byte-identical
    /// expansion to the load-from-path path.
    /// @param source          Raw (already-read) GLSL, possibly harness-assembled
    /// @param currentFileDir  Directory of the top-level shader, searched first
    /// @param includePaths    Additional `#include` search directories
    /// @param outError         If non-null, set on error
    /// @param outIncludedPaths If non-null, appended with each included header's
    ///                         canonical path (same fingerprint contract as
    ///                         `loadAndExpand`)
    /// @return Expanded GLSL source, or empty string on error
    static QString expandSource(const QString& source, const QString& currentFileDir, const QStringList& includePaths,
                                QString* outError = nullptr, QStringList* outIncludedPaths = nullptr);

    /// Clear the in-memory compilation cache (e.g. on shader hot-reload).
    ///
    /// Clears both the source-hash BakeCache AND the filename+mtime cache in
    /// the node core — after this call, the next prepare() will re-read and
    /// re-compile shader files from disk. Both caches share clearCache() so
    /// consumers can call one function to fully invalidate.
    static void clearCache();

    /// Bake target list: SPIR-V 1.0, GLSL 330, GLSL ES 300/310/320.
    static const QList<QShaderBaker::GeneratedShader>& bakeTargets();
};

} // namespace PhosphorRendering
