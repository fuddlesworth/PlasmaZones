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

#include <QLoggingCategory>

namespace PhosphorRendering {

Q_DECLARE_LOGGING_CATEGORY(lcShaderNode)

/// Clear the filename+mtime cache that lives in shadernoderhicore.cpp.
/// Called from ShaderCompiler::clearCache() so a single public clearCache()
/// call flushes BOTH caches — otherwise a hot-reload would wipe the
/// source-hash cache while the filename cache kept serving stale bakes.
void clearFilenameShaderCache();

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
