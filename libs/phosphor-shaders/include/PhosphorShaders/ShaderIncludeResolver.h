// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QString>
#include <QStringList>

namespace PhosphorShaders {

/// Resolves #include directives in GLSL shader source.
///
/// Supports:
/// - #include "path" — search relative to current file's directory, then include paths
/// - #include <path> — search only in include paths (e.g. global shaders dir)
class PHOSPHORSHADERS_EXPORT ShaderIncludeResolver
{
public:
    static constexpr int MaxIncludeDepth = 10;

    /// Expand #include directives in @p source.
    /// @param source Raw shader source (may contain #include "..." or #include <...>)
    /// @param currentFileDir Directory of the file that contains @p source
    /// @param includePaths Directories to search for includes
    /// @param outError If non-null, set on error (file not found, depth exceeded)
    /// @param outIncludedPaths If non-null, appended with the canonical
    ///        absolute path of every successfully-resolved include file.
    ///        Lets callers fingerprint transitively-included headers (e.g.
    ///        for cache invalidation) — without this, a cache keyed only
    ///        on the top-level shader's mtime serves stale baked SPIR-V
    ///        when an included header changes.
    /// @param outSourcePaths If non-null, populated as a GLSL source-string
    ///        legend: element @c i is the path of source-string number @c i as
    ///        referenced by the `#line <n> <i>` directives this resolver emits
    ///        around each inlined include. Index 0 is the top-level source —
    ///        left empty because the resolver is handed only the source text
    ///        and its directory, not the top-level file's path, so the caller
    ///        (which knows which file it asked to expand) fills index 0.
    ///        Consumers map a glslang / driver diagnostic of the form
    ///        `<i>:<line>` back to `<path>:<line>`.
    /// @return Expanded source, or empty string on error
    static QString expandIncludes(const QString& source, const QString& currentFileDir, const QStringList& includePaths,
                                  QString* outError = nullptr, QStringList* outIncludedPaths = nullptr,
                                  QStringList* outSourcePaths = nullptr);
};

} // namespace PhosphorShaders
