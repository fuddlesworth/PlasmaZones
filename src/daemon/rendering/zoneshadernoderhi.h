// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"

#include <PhosphorRendering/ZoneShaderNodeRhi.h>

#include <plasmazones_rendering_export.h>

#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// PlasmaZones-flavoured ZoneShaderNodeRhi. The class itself is shared with
/// PhosphorRendering; this typedef + the include-path discovery below
/// (warmShaderBakeCacheForPaths) is what's PlasmaZones-specific — the daemon
/// installs its bundled shaders under "plasmazones/shaders".
using ZoneShaderNodeRhi = PhosphorRendering::ZoneShaderNodeRhi;

/** Alias for PhosphorRendering::WarmShaderBakeResult — avoids type duplication. */
using WarmShaderBakeResult = PhosphorRendering::WarmShaderBakeResult;

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread (e.g. after ShaderRegistry::refresh()).
 *
 * @param vertexPath    absolute path to the vertex shader on disk
 * @param fragmentPath  absolute path to the fragment shader on disk
 * @param includePaths  directories to search for `#include` directives. Pass the
 *                      same list `ShaderRegistry::searchPaths()` returns so include
 *                      resolution matches the on-screen render path. If empty,
 *                      includes will only resolve relative to the shader file itself.
 * @return success and error message (e.g. from QShaderBaker) for UI reporting
 */
PLASMAZONES_RENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath,
                                                                              const QString& fragmentPath,
                                                                              const QStringList& includePaths = {});

} // namespace PlasmaZones
