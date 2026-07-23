// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>

#include <plasmazones_rendering_export.h>

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// Resolve the vertex stage for a zone pack whose fragment lives at
/// @p fragmentPath, exactly the way ZoneShaderItem does at load: the pack's
/// own sibling `zone.vert`, else the first `zone.vert` found walking each
/// trusted overlay root's `shared` subdir and then the root itself. Returns
/// an empty string when none exists.
///
/// SINGLE SOURCE OF TRUTH. The daemon warm-bake folds the resolved vert path
/// into the bake-cache key, so it must pick the SAME file the live load will,
/// including priority order — trustedShaderRoots() is QStandardPaths order
/// (user first), which is the REVERSE of ShaderRegistry::searchPaths(). Two
/// earlier warm-bake attempts diverged here (one probed the wrong directory
/// level and baked nothing; one walked the reversed list and would pick the
/// lowest-priority copy on a multi-root install), so route every caller
/// through this helper rather than re-deriving the walk.
/// @p searchDirs, when non-empty, replaces the trusted-root walk with an
/// already-expanded include-path list (each entry probed for `zone.vert`
/// directly). ZoneShaderItem passes its own list so a customised item still
/// resolves against exactly the paths it will compile with; the warm bake
/// omits it and gets the canonical roots.
inline QString resolveZoneVertexPath(const QString& fragmentPath, const QStringList& searchDirs = {})
{
    if (fragmentPath.isEmpty()) {
        return {};
    }
    const QString sibling = QFileInfo(fragmentPath).absolutePath() + QStringLiteral("/zone.vert");
    if (QFile::exists(sibling)) {
        return sibling;
    }
    if (!searchDirs.isEmpty()) {
        for (const QString& dir : searchDirs) {
            const QString candidate = dir + QStringLiteral("/zone.vert");
            if (QFile::exists(candidate)) {
                return candidate;
            }
        }
        return {};
    }
    const QStringList roots = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/overlays"), QStandardPaths::LocateDirectory);
    for (const QString& root : roots) {
        for (const QString& candidate :
             {root + QStringLiteral("/shared/zone.vert"), root + QStringLiteral("/zone.vert")}) {
            if (QFile::exists(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread (e.g. after ShaderRegistry::refresh()).
 *
 * The PlasmaZones-specific bit is the include-path discovery — the daemon installs
 * its bundled shaders under "plasmazones/overlays". The render-node class itself
 * (`PhosphorRendering::ZoneShaderNodeRhi`) is shared with the broader rendering
 * library; callers reference it via its full qualified name.
 *
 * @param vertexPath    absolute path to the vertex shader on disk
 * @param fragmentPath  absolute path to the fragment shader on disk
 * @param includePaths  directories to search for `#include` directives. Pass the
 *                      same list `ShaderRegistry::searchPaths()` returns so include
 *                      resolution matches the on-screen render path. If empty,
 *                      includes will only resolve relative to the shader file itself.
 * @param paramPreamble generated `#define p_<id> ...` block (T1.1) spliced after the
 *                      fragment `#version`; forwarded verbatim to the rendering-library
 *                      warm bake so the warm entry's cache key matches the live load.
 *                      Empty (the default, e.g. zone shaders pre-T1.1) is a no-op.
 * @param entryPrologue   T1.4 entry-point prologue, and
 * @param entryCandidates T1.4 entry functions + generated main(), both forwarded to the
 *                        rendering-library warm bake. MUST match what ZoneShaderItem
 *                        installs via setEntryScaffold so warm + live agree on the
 *                        assembled source and the bake-cache key. Empty = no assembly.
 * @return success and error message (e.g. from QShaderBaker) for UI reporting
 */
PLASMAZONES_RENDERING_EXPORT PhosphorRendering::WarmShaderBakeResult
warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath,
                            const QStringList& includePaths = {}, const QString& paramPreamble = {},
                            const QString& entryPrologue = {},
                            const QList<PhosphorShaders::EntryCandidate>& entryCandidates = {});

} // namespace PlasmaZones
