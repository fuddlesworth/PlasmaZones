// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators (plasmazones-shader-validate).
// The per-mode validators (packvalidator_overlay/_animation/_surface/_pointer.cpp) and
// the CLI entry point (main.cpp) live in separate translation units to keep
// each file focused;
// this header exposes the pieces the validators share: the pack-path confine
// guard, the param-type/pool bookkeeping, and the glslang compile+report path.

#pragma once

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <rhi/qshader.h>

#include <optional>

class QTextStream;

namespace PlasmaZones::ShaderValidate {

// ── authoring-model detection ──────────────────────────────────────────────

/// The four authoring models a pack can belong to.
enum class PackModel {
    Overlay,
    Animation,
    Surface,
    Pointer
};

/// Which authoring model @p packDir belongs to, or nullopt when the directory
/// carries no marker.
///
/// Detected from the pack's SIBLING `shared/` directory rather than from
/// metadata.json, because the three schemas are not distinguishable: all four
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

/// The `shared/` include roots for @p packDir, in resolution order.
///
/// A pack in the INSTALLED layout, `<data root>/plasmazones/<family>/<id>`,
/// gets its sibling `shared/` first (always listed, whether or not it exists)
/// and then the XDG data chain for its family. That is what lets the tool
/// work on an installed pack: one in `~/.local/share/plasmazones/<family>/<id>`
/// usually has no sibling `shared/` at all, since the helpers ship once into
/// the system prefix, and when it has one it may be a partial user override of
/// a header or two beside the system copy. The widened list is the same SET of
/// roots the runtime registries resolve includes against. The ORDER differs
/// for an installed pack: the runtime walks its search paths user-first, while
/// this list puts the sibling first and then `QStandardPaths::locateAll`,
/// which is also user-first, so a user override of a shared header shadows the
/// system copy in both. Only a pack that sits in the system prefix itself and
/// has a user override of the same header sees a different winner here (the
/// sibling, i.e. the system copy) than at runtime (the user copy).
///
/// Every other layout (the source tree's `data/<family>/<id>`, a scratchpad
/// laid out like it, a vendored pack set) is self-contained and resolves
/// against its sibling `shared/` and NOTHING else, so an installed copy of the
/// helpers can never satisfy an include the tree itself lacks: a header
/// missing from `data/<family>/shared` fails here the way it fails in CI,
/// rather than resolving from a stale `/usr/share` copy on a developer machine.
QStringList packSharedRoots(const QString& packDir);

// Confine a metadata-supplied shader path to its pack dir. Returns the confined
// path, or nullopt when the path is empty or escapes the pack dir. The
// definition explains the canonical-vs-lexical comparison.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel);

// The report column for a stage label: labels shorter than the column are
// padded to it, longer ones (`effect.frag (Qt-RHI preview)`) get one space so
// the OK/ERROR word never runs into the label.
QString padLabel(const QString& label);

// In-place confinement: rewrites @p path to its confined absolute form and
// returns true, or returns false when the path escapes the pack dir. An EMPTY
// path is left as-is and accepted (that stage is simply absent). Every
// validator gates every user-editable metadata path through this before
// opening it.
bool confinePackPathInPlace(const QString& packDir, QString& path);

// The set of accepted `type` strings for a declared parameter.
extern const QStringList kValidParamTypes;

// The pool a param's lane lives in — collisions are detected per pool, matching
// ParameterInfo::uniformName() / buildParamPreamble.
QString poolName(const QString& type);

// Report a compiled stage's outcome ("OK", or "ERROR" with the glslang
// diagnostics mapped to the author's file/line plus the did-you-mean hint).
// Returns 1 on failure, 0 on success. Shared by all the validators.
int reportCompile(QTextStream& out, const QString& label, const PhosphorRendering::ShaderCompiler::Result& result,
                  const QStringList& declared);

// Build the `p_<id>` name list a pack declares, for the did-you-mean hint.
QStringList declaredParamNames(const QList<PhosphorShaders::ShaderRegistry::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorPointerShaders::PointerShaderEffect::ParameterInfo>& params);

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
/// One declared parameter, reduced to what a preset lint needs: its id, its
/// type token, and whatever range it declares. The four families spell their
/// ParameterInfo differently (slot vs step, image vs no image), so the lint
/// takes this instead of any one of them and each arm projects into it.
struct PresetLintParam
{
    QString id;
    QString type;
    QVariant minValue;
    QVariant maxValue;
};

/// Lint a pack's `presets` block against what the pack declares.
///
/// Checks three things, which is everything decidable without rendering:
/// every preset key names a declared parameter, every value matches that
/// parameter's declared type, and a numeric value sits inside any declared
/// range. Reports to @p out and returns the number of problems found.
///
/// Deliberately NOT an error for a preset to omit parameters: a preset is a
/// partial tuning by design, and the ones it says nothing about fall back to
/// their defaults.
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PresetLintParam>& declared);

/// Per-family overloads, so each validator arm is one call rather than its own
/// projection loop. The four ParameterInfo types spell themselves differently
/// (slot vs step, image vs no image), which is why the lint takes the reduced
/// PresetLintParam and these do the reducing.
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PhosphorShaders::ShaderRegistry::ParameterInfo>& declared);
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo>& declared);
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo>& declared);
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PhosphorPointerShaders::PointerShaderEffect::ParameterInfo>& declared);

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
