// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file internal.h
 * @brief Shared constants and helper declarations for ShaderNodeRhi TUs
 *
 * Provides RhiConstants (quad vertices, component indices) and compile-time
 * limits for the multipass buffer system.
 */

#include <PhosphorRendering/ShaderNodeRhi.h>

#include <QByteArray>
#include <QLoggingCategory>

namespace PhosphorRendering {

Q_DECLARE_LOGGING_CATEGORY(lcShaderNode)

/// Clear the filename+mtime cache that lives in shadernoderhicore.cpp.
/// Called from ShaderCompiler::clearCache() so a single public clearCache()
/// call flushes BOTH caches — otherwise a hot-reload would wipe the
/// source-hash cache while the filename cache kept serving stale bakes.
void clearFilenameShaderCache();

/// Apply the T1.4 entry-point scaffold to a raw fragment source. Returns
/// @p raw unchanged when @p candidates is empty or @p raw already defines
/// `main()`; otherwise prepends @p prologue and appends the first matching
/// candidate's generated `main()` (via `composeEntryPoint`). Shared verbatim by
/// the live load (`loadFragmentShader`) and the warm bake so both produce the
/// identical pre-expansion source for a given shader.
inline QString applyEntryAssembly(const QString& raw, const QString& prologue,
                                  const QList<PhosphorShaders::EntryCandidate>& candidates)
{
    return PhosphorShaders::assembleEntryPoint(raw, prologue, candidates);
}

/// Fingerprint the entry scaffold for the bake-cache key. Empty when no
/// scaffold is installed (the animation / traditional path), so the key is
/// unchanged there. Folded in by both the live load and the warm bake so a
/// scaffold change invalidates, and the two paths never collide on a key while
/// disagreeing on the assembled source.
inline QByteArray entryScaffoldFingerprint(const QString& prologue,
                                           const QList<PhosphorShaders::EntryCandidate>& candidates)
{
    if (candidates.isEmpty() && prologue.isEmpty()) {
        return QByteArray();
    }
    QByteArray fp = prologue.toUtf8();
    for (const PhosphorShaders::EntryCandidate& c : candidates) {
        fp.append('\x1f');
        fp.append(c.functionName.toUtf8());
        fp.append('\x1f');
        fp.append(c.generatedMain.toUtf8());
        // alsoRequires gates which candidate composeEntryPoint selects, so two
        // scaffolds differing only there assemble differently — fold it in too.
        for (const QString& req : c.alsoRequires) {
            fp.append('\x1e');
            fp.append(req.toUtf8());
        }
    }
    return fp;
}

namespace RhiConstants {

inline constexpr float QuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

static constexpr int ComponentX = 0;
static constexpr int ComponentY = 1;
static constexpr int ComponentZ = 2;
static constexpr int ComponentW = 3;

} // namespace RhiConstants

} // namespace PhosphorRendering
