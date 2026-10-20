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
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QJsonArray>
#include <QJsonObject>
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
/// metadata.json, because the four schemas are not distinguishable: all four
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
///
/// THAT GUARANTEE COVERS `#include` ONLY, which this used to leave implied. A
/// `builtin:` buffer token does not come through these roots at all: the
/// registry's own resolver probes the pack's sibling shared/ and then falls
/// back to QStandardPaths, so a self-contained tree missing a builtin file
/// resolves it from the installed copy and validates on a developer machine
/// while failing in CI. The surface arm lints that case by comparing the
/// resolved directory against this list, which is exactly the discrimination
/// this function is good for.
QStringList packSharedRoots(const QString& packDir);

/// Read and parse @p packDir's metadata.json for a validator arm. On failure it
/// prints the pack's whole report, the one `metadata ERROR` naming why (an
/// unreadable file, invalid JSON, or a root that parses but is not an object)
/// and the `→ 1 error` footer, and returns nullopt. Shared so no arm can drift
/// back to reporting "invalid JSON: no error occurred" for a `[]` root.
std::optional<QJsonObject> readPackMetadata(QTextStream& out, const QString& packDir);

// Confine a metadata-supplied shader path to its pack dir. Returns the confined
// path, or nullopt when the path is empty or escapes the pack dir. The
// definition explains the canonical-vs-lexical comparison.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel);

// The report column for a stage label: labels shorter than the column are
// padded to it, longer ones (`kawase_down_0.frag`) get one space so the
// OK/ERROR word never runs into the label.
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
// A stage that compiled is then reflected against the shared descriptor-binding
// table (bindingLayoutProblems) and fails when a sampler or uniform block sits
// at the wrong binding. Returns 1 on failure, 0 on success. Shared by all the
// validators.
//
// Takes NO source-string legend, unlike the compositor bake. QShaderBaker
// drops the `#line` source string and reports every error against the top-level
// source, so there is no id here to resolve. Measured rather than assumed; the
// definition says how, and what it costs.
int reportCompile(QTextStream& out, const QString& label, const PhosphorRendering::ShaderCompiler::Result& result,
                  const QStringList& declared);

/// Every way a baked stage's descriptor bindings disagree with the one table all
/// four families and the daemon's ShaderNodeRhi share (PhosphorShaders::Bindings):
/// a contract sampler at the wrong slot, a consumer sampler inside the reserved
/// range, or a uniform block off binding 0. Empty when the layout is right.
/// Exposed so the tests can pin the rule without going through a whole pack.
QStringList bindingLayoutProblems(const QShader& shader);

// Build the `p_<id>` name list a pack declares, for the did-you-mean hint.
QStringList declaredParamNames(const QList<PhosphorShaders::ShaderRegistry::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo>& params);
QStringList declaredParamNames(const QList<PhosphorPointerShaders::PointerShaderEffect::ParameterInfo>& params);

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

/// Lint a preset value set on an IMAGE-typed parameter, for the two families that cannot
/// keep one.
///
/// The animation and surface arms parse a pack's presets before its directory is stamped,
/// so `parsePackPresets` refuses every image-typed preset value fail-closed and the value
/// is gone before any other lint sees it. Reads the RAW `presets` block for the keys a
/// pack sets, because the parsed map is exactly where they have already been dropped.
/// Returns its lints rather than printing, so the three preset reporters share ONE
/// `presets ERROR` header. Each used to print its own, and a pack tripping two of them
/// (an image param plus an out-of-range float, say) printed the header twice in one
/// report. See `reportPresetLints`.
QStringList imageParamPresetLints(const QJsonObject& root);

/// Lint the RAW `presets` value from @p root, for the faults the parsed map cannot show.
///
/// Each of them costs the author presets or values with only a log line: a
/// present-but-non-object `presets` (the loader ignores it wholesale), a non-object preset
/// BODY, a null preset value, more presets than the loader keeps, and a preset with more
/// values than it keeps. `collectPresetLints` below receives the already-parsed,
/// already-truncated map and so is blind to all of them.
QStringList rawPresetLints(const QJsonObject& root);

/// Lint a pack's `presets` block against what the pack declares.
///
/// Checks everything decidable without rendering: every preset key is usable as a
/// picker label, every key names a declared parameter, every value matches that
/// parameter's declared type, and a numeric value sits inside any declared range
/// AND inside its own type's range. Containment is deliberately NOT among them —
/// see the implementation, where the parse has already refused an escaping path
/// before this lint can see it.
///
/// @p packDir is the pack's directory, used to check that an image-typed preset
/// value names a file the pack actually ships. Every arm passes a real directory
/// today; an empty one skips the existence check.
///
/// Deliberately NOT an error for a preset to omit parameters: a preset is a
/// partial tuning by design, and the ones it says nothing about fall back to
/// their defaults.
///
/// RETURNS its lints rather than printing them. `reportPresetLints` is what
/// prints, under one shared header — see there for why.
QStringList presetLints(const QString& packDir, const QMap<QString, QVariantMap>& presets,
                        const QList<PresetLintParam>& declared);

/// Per-family overloads, so each validator arm is one call rather than its own
/// projection loop. The four ParameterInfo types spell themselves differently
/// (slot vs step, image vs no image), which is why the lint takes the reduced
/// PresetLintParam and these do the reducing.
QStringList presetLints(const QString& packDir, const QMap<QString, QVariantMap>& presets,
                        const QList<PhosphorShaders::ShaderRegistry::ParameterInfo>& declared);
QStringList presetLints(const QString& packDir, const QMap<QString, QVariantMap>& presets,
                        const QList<PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo>& declared);
QStringList presetLints(const QString& packDir, const QMap<QString, QVariantMap>& presets,
                        const QList<PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo>& declared);
QStringList presetLints(const QString& packDir, const QMap<QString, QVariantMap>& presets,
                        const QList<PhosphorPointerShaders::PointerShaderEffect::ParameterInfo>& declared);

/// Print everything the three collectors gathered under ONE `presets ERROR` header and
/// return the count, so a caller still does `errors += ...`. Prints nothing when @p lints
/// is empty, which is what keeps a clean pack's report free of an empty section.
int reportPresetLints(QTextStream& out, const QStringList& lints);

// ── compositor (KWin) bake, in both of KWin's dialects ─────────────────────
// Packs whose appliesTo makes them compositor-only are never loaded by the
// daemon, and their source is classic-GL (default-block uniforms, unbound
// samplers) that QShaderBaker's strict SPIR-V target rejects by design. Those
// packs once had metadata lints and NO stage compile anywhere in a headless
// run, because the GPU bake test that does cover them QSKIPs without a
// desktop GL 4.5 context, which is exactly the CI case.
//
// glslang's DEFAULT mode (no -V / -G, so no SPIR-V target) validates plain
// OpenGL-dialect GLSL and accepts that source, so the coverage is reachable
// offline through the `glslangValidator` binary. Out of process rather than
// linked: Qt vendors glslang inside ShaderTools without exposing it, and a
// direct libglslang dependency for one code path is a heavier build cost than
// a tool the GLSL toolchain already ships.
//
// Every compositor stage is compiled once per dialect KWin compiles it in:
// `(compositor)` is desktop GLSL 140, what KWin 6.7 hands a desktop GL context
// (PhosphorShaders::kwinDesktopDialect), and `(compositor GLES)` is GLSL ES 300,
// what KWin hands a GLES context, which is every context on 6.8
// (PhosphorShaders::kwinGlesDialect). A stage 6.8 adds a define to is also
// compiled as `(compositor GLES 6.7)`, the same dialect without it, which is
// what 6.7 builds on a GLES context. glslang holds ES 300 to the spec where a
// lenient driver would not, which is how varying locations reached a release
// compiling on NVIDIA and failing on Mesa. Each pack's fragment stages are then
// LINKED against the vertex stage the compositor pairs them with, which is
// where a varying read by name and written under another, a precision
// mismatch, or an `#extension` after a declaration shows.
//
// Out of reach either way: glslang is not the driver, so a driver-specific
// limit or bug still passes here, and KWin's own colour-management include
// cannot be resolved offline (finalizeColorStub stands in for it).

/// Absolute path to a usable glslang binary (`glslangValidator`, else the
/// `glslang` the project renamed it to), or an empty string when neither is on
/// PATH. Resolved once per run.
QString glslangValidatorPath();

/// Replace the source-string id a glslang diagnostic @p line leads with by the
/// name of the file it stands for: @p label for source string 0, the file
/// name of @p sourcePaths[i] (the include resolver's legend) for an include.
/// Shared by both reporters, the Qt-RHI one (reportCompile) and the compositor
/// bake. Leaves the line untouched when the legend does not cover the id.
QString nameDiagnosticSource(const QString& line, const QString& label, const QStringList& sourcePaths);

/// Read the stage at @p path once for every arm that bakes it. Prints under
/// @p label and returns false when it cannot be read or is empty, so the
/// preview and compositor arms do not each report the same file.
bool readStageSource(QTextStream& out, const QString& label, const QString& path, QString& raw);

/// The compositor's HDR colour-management block declares `pzFinalizeColor`
/// and routes the generated main()'s fragColor write through it. The real
/// block includes KWin's `colormanagement.glsl` from a resource path glslang
/// cannot see, so the bake splices an identity stand-in that declares the SAME
/// symbol and macro: a pack that defines its own `pzFinalizeColor`, or
/// redefines `PZ_FINALIZE_COLOR`, collides here the way it collides live. The
/// compositor splices the real block into every pass that authors new sRGB
/// content: an animation's per-window fragment and a pointer pack's main pass.
QString finalizeColorStub();

/// One compositor stage, assembled as the effect assembles it but not yet
/// finished: no KWin define block, varyings still located.
struct CompositorStage
{
    /// The report label, usually the file name.
    QString label;
    PhosphorShaders::ShaderStage stage = PhosphorShaders::ShaderStage::Fragment;
    QString source;
    /// The include resolver's source-string legend, so a diagnostic inside a
    /// shared header reads `surface_lib.glsl:112` instead of a bare `1`.
    QStringList sourcePaths;
    /// What the runtime splices beside the define block on KWin 6.8 only (the
    /// top-down texcoord define of the per-window animation path and the
    /// present shader), so it reaches the GLES arm alone. A stage that has
    /// some is also compiled as GLSL ES without them, which is what KWin 6.7
    /// builds on a GLES context.
    QString glesExtraDefines;
    /// Who supplies the stage, so a link diagnostic offers only a fix the
    /// author can make.
    enum class Origin {
        /// The pack ships it.
        Pack,
        /// The compositor's own, used because the pack declares no stage of
        /// its own, which it may.
        Builtin,
        /// The compositor's own, and the pass always runs with it (a screen
        /// pass, a surface buffer pass).
        FixedBuiltin,
    };
    Origin origin = Origin::Pack;
    /// The compositor places declarations of its own above the pack's first
    /// line (the entry scaffold, the colour-management block), so nothing in
    /// the pack can precede every declaration.
    bool compositorDeclarationsFirst = false;
};

/// How a family assembles one of its stages before the KWin define block.
struct CompositorAssembly
{
    /// The entry scaffold; an empty prologue means none (buffer passes and
    /// vertex stages ship their own main()).
    QString entryPrologue;
    QList<PhosphorShaders::EntryCandidate> entryCandidates;
    /// The p_<id> preamble, spliced after include expansion. Empty for a stage
    /// the runtime splices none into.
    QString preamble;
    /// Splice finalizeColorStub, as the runtime splices the real block.
    bool finalizeColor = false;
    QString glesExtraDefines;
};

/// Assemble @p raw (read from @p path) the way the compositor does: the entry
/// scaffold, include expansion through the resolver the compositor uses (NOT
/// ShaderCompiler::expandSource, whose angle includes also search the
/// shader's own directory), the preamble and the colour stand-in. An include
/// failure is printed under @p label, tagged for both dialects since it
/// blocks both, and returns nullopt.
std::optional<CompositorStage> assembleCompositorStage(QTextStream& out, const QString& label,
                                                       PhosphorShaders::ShaderStage stage, const QString& raw,
                                                       const QString& path, const QStringList& includePaths,
                                                       const CompositorAssembly& assembly);

/// A built-in compositor stage, which has no includes to expand.
CompositorStage builtinCompositorStage(const QString& label, PhosphorShaders::ShaderStage stage, const QString& source,
                                       const QString& glesExtraDefines = {},
                                       CompositorStage::Origin origin = CompositorStage::Origin::Builtin);

/// Finish @p stage the way the compositor does (PhosphorShaders::
/// kwinCompositorSource, which is exactly what the compositor's
/// injectKwinDefineAfterVersion runs), compile it through glslang in each
/// source KWin builds it from (desktop, GLES, and GLES as 6.7 builds it when
/// 6.8 adds a define), and print one OK/ERROR line per arm under its label.
///
/// Returns 1 when any arm rejects the stage and 0 when all compile: a stage
/// counts once in the pack's error total however many arms reject it, so the
/// total is the number of broken stages. When an arm rejects it with the same
/// diagnostics as the last arm that printed them, its line says so rather than
/// repeating them. The arms run concurrently. A glslang that cannot run at all
/// (staging failure, start failure, timeout, crash) fails them alike and is
/// reported once.
int reportCompositorCompile(QTextStream& out, const CompositorStage& stage);

/// assembleCompositorStage then reportCompositorCompile, the pair every arm
/// runs per stage. Returns the stage when it compiled, for its link, and adds
/// one to @p errors when it did not.
std::optional<CompositorStage> bakeCompositorStage(QTextStream& out, int& errors, const QString& label,
                                                   PhosphorShaders::ShaderStage stage, const QString& raw,
                                                   const QString& path, const QStringList& includePaths,
                                                   const CompositorAssembly& assembly);

/// A pack's DECLARED vertex stage after its compositor bake. @c stage is set
/// when it compiled. It is empty with @c broken false when the runtime falls
/// back to its built-in stage (an include failure), and empty with @c broken
/// true when it failed to compile, in which case the program fails live and
/// there is nothing to link.
struct DeclaredVertex
{
    std::optional<CompositorStage> stage;
    bool broken = false;
};

/// bakeCompositorStage for a declared vertex stage, sorting the two ways it
/// can fail as the runtime does.
DeclaredVertex bakeDeclaredVertex(QTextStream& out, int& errors, const QString& label, const QString& raw,
                                  const QString& path, const QStringList& includePaths,
                                  const CompositorAssembly& assembly);

/// The vertex stage the compositor runs a fragment with: the declared one when
/// it compiled, @p builtin when it would have fallen back, and none when the
/// declared one failed to compile.
std::optional<CompositorStage> linkedVertex(const DeclaredVertex& declared, const CompositorStage& builtin);

/// Link @p fragment against @p vertex, the program the compositor actually
/// builds, and print one OK/ERROR line tagged `(compositor link)` under the
/// fragment's label. Checks what compiling each stage alone cannot, in both
/// dialects: every varying the fragment uses must be written by the vertex
/// stage under the same name and type (the compositor strips the locations,
/// so names are all that match them), and neither stage may enable an
/// `#extension` after a declaration. glslang's own link, run on the GLES
/// pair, adds that uniforms both declare must agree on precision. Call it only for pack stages that compiled,
/// so a compile error is not reported twice. A built-in stage is compiled by
/// the tests. Returns 1 on failure, 0 otherwise.
int reportCompositorLink(QTextStream& out, const CompositorStage& vertex, const CompositorStage& fragment);

// Compile one ZONE stage through the exact runtime assembly and print OK/ERROR.
// Returns 1 on failure, 0 on success.
int compileStage(QTextStream& out, const QString& label, const QString& path, QShader::Stage stage,
                 const QStringList& includePaths, bool useScaffold, const QString& preamble,
                 const PhosphorShaders::ShaderRegistry::ShaderInfo& info);

} // namespace PlasmaZones::ShaderValidate
