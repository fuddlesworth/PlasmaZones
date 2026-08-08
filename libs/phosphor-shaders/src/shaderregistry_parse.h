// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file shaderregistry_parse.h
 * @brief Private cross-TU surface of the shader-pack metadata parser.
 *
 * The parse machinery (metadata.json → ShaderInfo, the containment-checked
 * path resolver, and the process-wide schema validator) lives in
 * shaderregistry_parse.cpp; shaderregistry.cpp's live-scan strategy and
 * translateParamsToUniforms consume it through these declarations. Library
 * internal — not installed, not part of the PhosphorShaders API.
 */

#include <PhosphorShaders/ShaderRegistry.h>

#include <PhosphorFsLoader/PackPathGuard.h>
#include <PhosphorFsLoader/SchemaValidator.h>

class QDir;
class QJsonObject;

namespace PhosphorShaders {
namespace ShaderRegistryParse {

/// Resolve a pack-declared file name against the pack's own directory, or
/// return empty when it escapes (directory-traversal containment; see the
/// implementation's doc for the policy contract).
QString resolveWithinPack(const QDir& dir, const QString& declaredName,
                          PhosphorFsLoader::AbsolutePathPolicy policy = PhosphorFsLoader::AbsolutePathPolicy::Reject);

/// Parse a metadata.json root into a ShaderInfo. The caller has already
/// validated the file exists, fits under kMaxFileBytes, parses as JSON, and
/// has an object root; this owns only the schema-specific bits (paths,
/// buffer coherence, the T1.1 auto-slot assignment, presets).
ShaderRegistry::ShaderInfo parseShaderMetadata(const QString& shaderDir, const QJsonObject& root);

/// Process-wide shader-metadata schema validator, compiled once from the
/// RCC-embedded schema (see qt6_add_resources in CMakeLists).
const PhosphorFsLoader::SchemaValidator& shaderMetadataValidator();

} // namespace ShaderRegistryParse
} // namespace PhosphorShaders
