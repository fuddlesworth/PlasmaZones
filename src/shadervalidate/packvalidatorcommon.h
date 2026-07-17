// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators (plasmazones-shader-validate).
// The per-mode validators (packvalidators.cpp) and the CLI entry point
// (main.cpp) live in separate translation units to keep each file focused;
// this header exposes the pieces the validators share: the pack-path confine
// guard, the param-type/pool bookkeeping, and the glslang compile+report path.

#pragma once

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QString>
#include <QStringList>

#include <rhi/qshader.h>

#include <optional>

class QTextStream;

namespace PlasmaZones::ShaderValidate {

// Confine a metadata-supplied shader path to its pack dir. Returns the confined
// path, or nullopt when the path is empty or escapes the pack dir. See the
// definition for the canonical-vs-lexical domain rules and why this gate is
// deliberately stricter than the runtime.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel);

// The set of accepted `type` strings for a declared parameter.
extern const QStringList kValidParamTypes;

// The pool a param's lane lives in — collisions are detected per pool, matching
// ParameterInfo::uniformName() / buildParamPreamble.
QString poolName(const QString& type);

// Report a compiled stage's outcome ("OK", or "ERROR" with the glslang
// diagnostics mapped to the author's file/line plus the did-you-mean hint).
// Returns 1 on failure, 0 on success. Shared by all three validators.
int reportCompile(QTextStream& out, const QString& label, const PhosphorRendering::ShaderCompiler::Result& result,
                  const QStringList& declared);

// Build the `p_<id>` name list a pack declares, for the did-you-mean hint.
QStringList declaredParamNames(const QList<PhosphorShaders::ShaderRegistry::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo>& params);

// Compile one ZONE stage through the exact runtime assembly and print OK/ERROR.
// Returns 1 on failure, 0 on success.
int compileStage(QTextStream& out, const QString& label, const QString& path, QShader::Stage stage,
                 const QStringList& includePaths, bool useScaffold, const QString& preamble,
                 const PhosphorShaders::ShaderRegistry::ShaderInfo& info);

} // namespace PlasmaZones::ShaderValidate
