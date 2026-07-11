// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>

#include <plasmazones_rendering_export.h>

#include <QString>
#include <QStringList>

namespace PlasmaZones {

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
