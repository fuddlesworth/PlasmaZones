// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators (plasmazones-shader-validate).
// The per-mode validators (packvalidator_overlay/_animation/_surface.cpp) and
// the CLI entry point (main.cpp) live in separate translation units to keep
// each file focused;
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

// ── authoring-model detection ──────────────────────────────────────────────

/// The three authoring models a pack can belong to.
enum class PackModel {
    Overlay,
    Animation,
    Surface
};

/// Which authoring model @p packDir belongs to, or nullopt when the directory
/// carries no marker.
///
/// Detected from the pack's SIBLING `shared/` directory rather than from
/// metadata.json, because the three schemas are not distinguishable: all three
/// carry id / name / fragmentShader, and the only animation-exclusive field
/// (`appliesTo`) is optional, so a universal animation pack that omits it looks
/// exactly like an overlay pack. The shared directory is unambiguous, since
/// each family's uniform header lives there under its own name, and it is the
/// SAME include root the stage bakes resolve against, so detection and
/// compilation can never disagree about which family a pack was treated as.
///
/// This exists because the model used to be a flag with `--overlay` silently
/// the default, so running the tool on an animation pack without `--animation`
/// produced two confident errors that were pure artifacts of the wrong
/// validator (a demand for `zone.vert`, and `common.glsl` not resolving). A
/// diagnostic that is wrong in a way an author cannot see through is worse than
/// no diagnostic, so the model is inferred and the flags only override.
///
/// Lives here rather than in main.cpp so it is reachable from the tests: it is
/// validator logic, and only the flag-override policy on top of it is CLI.
std::optional<PackModel> detectPackModel(const QString& packDir);

// Confine a metadata-supplied shader path to its pack dir. Returns the confined
// path, or nullopt when the path is empty or escapes the pack dir. See the
// definition for the canonical-vs-lexical domain rules and why this gate is
// deliberately stricter than the runtime.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel);

// In-place confinement: rewrites @p path to its confined absolute form and
// returns true, or returns false when the path escapes the pack dir. An EMPTY
// path is left as-is and accepted (that stage is simply absent). All three
// validators gate every user-editable metadata path through this before
// opening it.
bool confinePackPathInPlace(const QString& packDir, QString& path);

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

// ── compositor (KWin classic-GL) bake ──────────────────────────────────────
// Packs whose appliesTo makes them compositor-only are never loaded by the
// daemon, and their source is classic-GL (default-block uniforms, unbound
// samplers) that QShaderBaker's strict SPIR-V target rejects by design. That
// left 35 of the 94 bundled animation packs with metadata lints and NO stage
// compile anywhere in a headless run — the GPU bake test that does cover them
// QSKIPs without a desktop GL 4.5 context, which is exactly the CI case.
//
// glslang's DEFAULT mode (no -V / -G, so no SPIR-V target) validates plain
// OpenGL-dialect GLSL and accepts that source, so the coverage is reachable
// offline through the `glslangValidator` binary. Out of process rather than
// linked: Qt vendors glslang inside ShaderTools without exposing it, and a
// direct libglslang dependency for one code path is a heavier build cost than
// a tool the GLSL toolchain already ships.

/// Absolute path to a usable glslang binary (`glslangValidator`, else the
/// `glslang` the project renamed it to), or an empty string when neither is on
/// PATH. Resolved once per run.
QString glslangValidatorPath();

/// Compile @p source as @p stage through `glslangValidator` at @p toolPath and
/// print OK/ERROR under @p label. @p stage is the glslang `-S` token ("frag" /
/// "vert"). Returns 1 on failure, 0 on success.
int reportCompositorCompile(QTextStream& out, const QString& label, const QString& stage, const QString& source,
                            const QString& toolPath);

// Compile one ZONE stage through the exact runtime assembly and print OK/ERROR.
// Returns 1 on failure, 0 on success.
int compileStage(QTextStream& out, const QString& label, const QString& path, QShader::Stage stage,
                 const QStringList& includePaths, bool useScaffold, const QString& preamble,
                 const PhosphorShaders::ShaderRegistry::ShaderInfo& info);

} // namespace PlasmaZones::ShaderValidate
