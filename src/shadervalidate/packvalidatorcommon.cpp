// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators — see packvalidatorcommon.h.

#include "packvalidatorcommon.h"

#include "daemon/rendering/zoneentryscaffold.h"

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorPointerShaders::PointerShaderEffect;
using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;
using PhosphorSurfaceShaders::SurfaceShaderEffect;

namespace PlasmaZones::ShaderValidate {

// Two comparison domains. When both the pack dir and the candidate exist on
// disk the check is CANONICAL (symlinks resolved on both sides), so a symlink
// inside the pack that points outside it is rejected even though its lexical
// path sits under the pack. When the candidate does not exist yet (a stage the
// author has not written, an --emit-preamble run before the shader) the check
// falls back to the LEXICAL cleaned path, which still rejects `../` escapes and
// absolute paths. The runtime only canonicalises, so this gate is deliberately
// the stricter of the two.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel)
{
    if (rel.isEmpty()) {
        return std::nullopt;
    }
    const QString lexicalRoot = QDir::cleanPath(QDir(packDir).absolutePath()) + QStringLiteral("/");
    const QString abs = QDir::cleanPath(QDir(packDir).filePath(rel));
    const QString canonicalRoot = QFileInfo(QDir(packDir).absolutePath()).canonicalFilePath();
    const QString canonicalSelf = QFileInfo(abs).canonicalFilePath();
    const bool useCanonical = !canonicalRoot.isEmpty() && !canonicalSelf.isEmpty();
    const QString candidate = useCanonical ? canonicalSelf : abs;
    const QString comparisonRoot = useCanonical ? (canonicalRoot + QStringLiteral("/")) : lexicalRoot;
    if (!candidate.startsWith(comparisonRoot)) {
        return std::nullopt;
    }
    return abs;
}

bool confinePackPathInPlace(const QString& packDir, QString& path)
{
    if (path.isEmpty()) {
        return true;
    }
    const auto confined = confinedPackPath(packDir, path);
    if (!confined) {
        return false;
    }
    path = *confined;
    return true;
}

const QStringList kValidParamTypes = {QStringLiteral("float"), QStringLiteral("int"), QStringLiteral("bool"),
                                      QStringLiteral("color"), QStringLiteral("image")};

// The pool a param's lane lives in — collisions are detected per pool, matching
// ParameterInfo::uniformName() / buildParamPreamble.
QString poolName(const QString& type)
{
    if (type == QLatin1String("color")) {
        return QStringLiteral("color");
    }
    if (type == QLatin1String("image")) {
        return QStringLiteral("image");
    }
    return QStringLiteral("scalar");
}

// Cheap Levenshtein for the "did you mean" hint on an undeclared p_ symbol.
int editDistance(const QString& a, const QString& b)
{
    const int n = a.size();
    const int m = b.size();
    QList<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev = cur;
    }
    return prev[m];
}

// If the glslang diagnostic names an undeclared `p_<x>` and a declared param is
// a near match, surface the suggestion — the friction T1.2 exists to remove.
// @p declared is the list of generated `p_<id>` names (zone or animation).
void appendDidYouMean(QTextStream& out, const QString& diagnostic, const QStringList& declared)
{
    static const QRegularExpression re(QStringLiteral("'(p_[A-Za-z0-9_]+)' : undeclared identifier"));
    auto it = re.globalMatch(diagnostic);
    while (it.hasNext()) {
        const QString used = it.next().captured(1);
        if (declared.contains(used)) {
            continue;
        }
        QString best;
        int bestDist = 1000;
        for (const QString& cand : declared) {
            const int d = editDistance(used, cand);
            if (d < bestDist) {
                bestDist = d;
                best = cand;
            }
        }
        // Only suggest if the typo is plausibly a typo (within ~1/3 of length).
        if (!best.isEmpty() && bestDist <= std::max(2, static_cast<int>(used.size()) / 3)) {
            out << "    (did you mean '" << best << "'? — declared in metadata.json)\n";
        }
    }
}

namespace {

/// The family directory names the runtime registries scan under
/// `plasmazones/`. An installed pack always sits directly inside one of them,
/// so the name identifies the family without guessing at metadata.
bool isKnownFamilyDir(const QString& name)
{
    return name == QLatin1String("animations") || name == QLatin1String("overlays") || name == QLatin1String("surface")
        || name == QLatin1String("pointer");
}

/// Which family's marker header @p shared holds, if any.
std::optional<PackModel> modelFromSharedDir(const QDir& shared)
{
    if (shared.exists(QStringLiteral("animation_uniforms.glsl"))) {
        return PackModel::Animation;
    }
    if (shared.exists(QStringLiteral("surface_uniforms.glsl"))) {
        return PackModel::Surface;
    }
    if (shared.exists(QStringLiteral("pointer_uniforms.glsl"))) {
        return PackModel::Pointer;
    }
    if (shared.exists(QStringLiteral("common.glsl"))) {
        return PackModel::Overlay;
    }
    return std::nullopt;
}

} // namespace

QStringList packSharedRoots(const QString& packDir)
{
    // Normalised once: a trailing slash makes QFileInfo::absolutePath() answer
    // the pack itself rather than its parent, which would derive the sibling
    // as `<pack>/shared` and the family as the pack id.
    const QString dir = QDir::cleanPath(QDir(packDir).absolutePath());
    const QString parent = QFileInfo(dir).absolutePath();
    const QString sibling = QDir::cleanPath(parent + QStringLiteral("/shared"));

    // Only the INSTALLED layout, `<data root>/plasmazones/<family>/<id>`, is
    // widened to the XDG chain. Such a pack usually has no sibling `shared/`
    // at all (the helpers ship once into the system prefix while the pack
    // sits in the user's data dir), and when it has one it may be a PARTIAL
    // user override of a header or two, which the runtime resolves alongside
    // the system copy; a sibling-only lookup would fail both. The widened
    // list is the same set of roots the runtime registries resolve against,
    // so this stays a marker lookup rather than becoming a guess.
    //
    // Everything else (the source tree's `data/<family>/<id>`, a scratchpad
    // laid out like it, a vendored pack set) is self-contained: it resolves
    // against its sibling and nowhere else, so an installed copy of the
    // helpers can never satisfy an include the tree itself lacks.
    const QString family = QFileInfo(parent).fileName();
    const QString dataRootChild = QFileInfo(QFileInfo(parent).absolutePath()).fileName();
    const bool installedLayout = isKnownFamilyDir(family) && dataRootChild == QLatin1String("plasmazones");
    if (!installedLayout) {
        return {sibling};
    }

    QStringList roots{sibling};
    {
        const QStringList installed = QStandardPaths::locateAll(
            QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/") + family + QStringLiteral("/shared"),
            QStandardPaths::LocateDirectory);
        for (const QString& dir : installed) {
            const QString clean = QDir::cleanPath(dir);
            if (!roots.contains(clean)) {
                roots << clean;
            }
        }
    }
    return roots;
}

std::optional<PackModel> detectPackModel(const QString& packDir)
{
    // The same roots the stage bakes resolve against: a self-contained tree
    // has only its sibling, and an installed pack is found through its
    // family's XDG chain.
    for (const QString& root : packSharedRoots(packDir)) {
        if (const std::optional<PackModel> model = modelFromSharedDir(QDir(root))) {
            return model;
        }
    }
    return std::nullopt;
}

QString glslangValidatorPath()
{
    // BOTH upstream spellings, in the order a distro is likely to carry them.
    // glslang renamed its binary to plain `glslang` and kept `glslangValidator`
    // as a compatibility symlink that upstream documents as deprecated, so a
    // lookup pinned to either name alone is a time bomb: the old name breaks
    // wherever the symlink has been dropped, and the new one is missing on
    // anything older. Both resolve to the same program and take the same
    // arguments.
    //
    // Resolved once: a run validates up to a few hundred packs and the PATH
    // walk is the same answer every time.
    static const QString path = [] {
        for (const QString& name : {QStringLiteral("glslangValidator"), QStringLiteral("glslang")}) {
            const QString found = QStandardPaths::findExecutable(name);
            if (!found.isEmpty()) {
                return found;
            }
        }
        return QString();
    }();
    return path;
}

int reportCompositorCompile(QTextStream& out, const QString& label, const QString& stage, const QString& source,
                            const QString& toolPath)
{
    // glslang reads the stage from the file extension unless -S says otherwise;
    // -S is passed below, so the temp file name only has to be unique. A
    // per-call QTemporaryDir keeps concurrent invocations from colliding and
    // takes the file with it on scope exit.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        out << "  " << padLabel(label) << "ERROR\n    cannot create a temporary directory for the "
            << "compositor bake\n";
        return 1;
    }
    const QString srcPath = tmp.filePath(QStringLiteral("kwin.") + stage);
    QFile srcFile(srcPath);
    if (!srcFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out << "  " << padLabel(label) << "ERROR\n    cannot write " << srcPath << "\n";
        return 1;
    }
    // Checked: a short write stages a TRUNCATED shader, and glslang would then
    // report a syntax error the author's file does not contain.
    const QByteArray encoded = source.toUtf8();
    if (srcFile.write(encoded) != encoded.size()) {
        out << "  " << padLabel(label)
            << "ERROR\n    could not stage the shader for compilation: " << srcFile.errorString() << "\n";
        return 1;
    }
    srcFile.close();

    // DEFAULT mode on purpose — no -V and no -G. Both of those select a SPIR-V
    // target, which reimposes the very rules this dialect breaks (a -G run
    // rejects the shared headers' default-block uniforms with "non-opaque
    // uniform variables need a layout(location=L)"). Bare glslang validates
    // OpenGL-dialect GLSL and is what matches the compositor's own compile.
    QProcess proc;
    proc.start(toolPath, {QStringLiteral("-S"), stage, srcPath});
    // Well under the 120s ctest timeout on the shader_validate_* gates, so a
    // wedged invocation names the pack it wedged on instead of ctest killing
    // the whole run with nothing to point at.
    if (!proc.waitForFinished(15000)) {
        // A binary that cannot be executed at all (a broken symlink, no
        // execute bit, the wrong ELF class) fails here too, and reporting
        // that as a timeout sends the reader after the wrong problem.
        if (proc.error() == QProcess::FailedToStart) {
            out << "  " << padLabel(label) << "ERROR\n    could not run " << toolPath << ": " << proc.errorString()
                << "\n";
            return 1;
        }
        proc.kill();
        proc.waitForFinished(1000);
        out << "  " << padLabel(label) << "ERROR\n    " << toolPath << " timed out\n";
        return 1;
    }
    const QString log =
        QString::fromUtf8(proc.readAllStandardOutput()) + QString::fromUtf8(proc.readAllStandardError());
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        out << "  " << padLabel(label) << "OK (compositor)\n";
        return 0;
    }
    out << "  " << padLabel(label) << "ERROR (compositor)\n";
    // glslang leads with the temp file name and then one line per diagnostic.
    // Drop the echoed name so the report shows the author's errors and nothing
    // about where the file happened to be staged.
    const QStringList lines = log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        // glslang echoes the source file name as its first output line. That is
        // the staging path under a temp dir, which is noise at best and a
        // different string on every run at worst (it would make the report
        // unstable for anything diffing it). Both the bare name and the full
        // path are matched, since which one is echoed follows the argument.
        if (trimmed.isEmpty() || trimmed == srcPath || trimmed == QFileInfo(srcPath).fileName()) {
            continue;
        }
        if (trimmed.startsWith(QLatin1String("SPIR-V is not generated"))) {
            continue;
        }
        out << "    " << trimmed << "\n";
    }
    return 1;
}

QString padLabel(const QString& label)
{
    constexpr int kColumn = 15;
    return label.size() < kColumn ? label.leftJustified(kColumn) : label + QLatin1Char(' ');
}

// Report a compiled stage's outcome: "OK", or "ERROR" with the glslang
// diagnostics mapped to the author's file/line (T1.3 #line) plus the
// did-you-mean hint. @p declared is the list of generated `p_<id>` names.
// Returns 1 on failure, 0 on success. Shared by all four validators.
int reportCompile(QTextStream& out, const QString& label, const ShaderCompiler::Result& result,
                  const QStringList& declared)
{
    if (result.success) {
        out << "  " << padLabel(label) << "OK\n";
        return 0;
    }
    out << "  " << padLabel(label) << "ERROR\n";
    const QStringList diagLines = result.error.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : diagLines) {
        // glslang names the root source (file id 0, via the T1.3 #line directives)
        // with an empty filename — `ERROR: :58:`. Substitute the stage label so the
        // author sees `effect.frag:58`. Include errors keep their numeric file id.
        QString shown = line.trimmed();
        shown.replace(QStringLiteral("ERROR: :"), QStringLiteral("ERROR: ") + label + QStringLiteral(":"));
        shown.replace(QStringLiteral("WARNING: :"), QStringLiteral("WARNING: ") + label + QStringLiteral(":"));
        out << "    " << shown << "\n";
    }
    appendDidYouMean(out, result.error, declared);
    return 1;
}

// Build the `p_<id>` name list a pack declares, for the did-you-mean hint.
int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PresetLintParam>& declared)
{
    if (presets.isEmpty()) {
        return 0;
    }

    QHash<QString, PresetLintParam> byId;
    byId.reserve(declared.size());
    for (const PresetLintParam& p : declared) {
        byId.insert(p.id, p);
    }

    int problems = 0;
    for (auto it = presets.constBegin(); it != presets.constEnd(); ++it) {
        const QString& presetName = it.key();
        const QVariantMap& values = it.value();
        for (auto vit = values.constBegin(); vit != values.constEnd(); ++vit) {
            const auto found = byId.constFind(vit.key());
            if (found == byId.constEnd()) {
                out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key()
                    << "', which the pack does not declare\n";
                ++problems;
                continue;
            }

            const PresetLintParam& param = *found;
            const QVariant& value = vit.value();

            if (param.type == QLatin1String("bool")) {
                if (value.typeId() != QMetaType::Bool) {
                    out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key()
                        << "' to a non-boolean\n";
                    ++problems;
                }
                continue;
            }
            if (param.type == QLatin1String("color") || param.type == QLatin1String("image")) {
                if (value.typeId() != QMetaType::QString) {
                    out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key() << "' to a non-"
                        << param.type << " value\n";
                    ++problems;
                }
                continue;
            }

            // float / int: numeric, and inside the declared range when there
            // is one. A missing bound is not a problem — plenty of parameters
            // legitimately declare only one, or neither.
            if (!value.canConvert<double>() || value.typeId() == QMetaType::QString
                || value.typeId() == QMetaType::Bool) {
                out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key()
                    << "' to a non-numeric value\n";
                ++problems;
                continue;
            }
            const double v = value.toDouble();
            if (param.minValue.isValid() && v < param.minValue.toDouble()) {
                out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key() << "' to " << v
                    << ", below its declared minimum " << param.minValue.toDouble() << "\n";
                ++problems;
            }
            if (param.maxValue.isValid() && v > param.maxValue.toDouble()) {
                out << padLabel(packLabel) << "preset '" << presetName << "' sets '" << vit.key() << "' to " << v
                    << ", above its declared maximum " << param.maxValue.toDouble() << "\n";
                ++problems;
            }
        }
    }
    return problems;
}

namespace {
/// The projection every overload below shares. Templated on the family's
/// ParameterInfo rather than written four times, because all four carry the
/// three fields the lint reads under the same names.
template<typename ParamInfo>
QList<PresetLintParam> toLintParams(const QList<ParamInfo>& declared)
{
    QList<PresetLintParam> out;
    out.reserve(declared.size());
    for (const ParamInfo& p : declared) {
        out.append(PresetLintParam{p.id, p.type, p.minValue, p.maxValue});
    }
    return out;
}
} // namespace

int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<ShaderRegistry::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packLabel, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<AnimationShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packLabel, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<SurfaceShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packLabel, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packLabel, const QMap<QString, QVariantMap>& presets,
                         const QList<PointerShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packLabel, presets, toLintParams(declared));
}

QStringList declaredParamNames(const QList<ShaderRegistry::ParameterInfo>& params)
{
    QStringList declared;
    for (const ShaderRegistry::ParameterInfo& p : params) {
        declared << QStringLiteral("p_") + p.id;
    }
    return declared;
}
QStringList declaredParamNames(const QList<AnimationShaderEffect::ParameterInfo>& params)
{
    QStringList declared;
    for (const AnimationShaderEffect::ParameterInfo& p : params) {
        declared << QStringLiteral("p_") + p.id;
    }
    return declared;
}
QStringList declaredParamNames(const QList<SurfaceShaderEffect::ParameterInfo>& params)
{
    QStringList declared;
    for (const SurfaceShaderEffect::ParameterInfo& p : params) {
        declared << QStringLiteral("p_") + p.id;
    }
    return declared;
}
QStringList declaredParamNames(const QList<PointerShaderEffect::ParameterInfo>& params)
{
    QStringList declared;
    for (const PointerShaderEffect::ParameterInfo& p : params) {
        declared << QStringLiteral("p_") + p.id;
    }
    return declared;
}

// Compile one ZONE stage through the exact runtime assembly and print OK/ERROR.
// Returns 1 on failure, 0 on success.
int compileStage(QTextStream& out, const QString& label, const QString& path, QShader::Stage stage,
                 const QStringList& includePaths, bool useScaffold, const QString& preamble,
                 const ShaderRegistry::ShaderInfo& info)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out << "  " << padLabel(label) << "ERROR\n    cannot read " << path << "\n";
        return 1;
    }
    const QString raw = QString::fromUtf8(f.readAll());

    // effect.frag goes through the entry scaffold (which may wrap a pZone/pImage
    // body in a generated main()); buffer passes and the vertex shader carry their
    // own main() and are not scaffolded — matching ZoneShaderItem's live load.
    const QString assembled = useScaffold ? PlasmaZones::assembleZoneEntrySource(raw) : raw;

    QString err;
    QString expanded =
        ShaderIncludeResolver::expandIncludes(assembled, QFileInfo(path).absolutePath(), includePaths, &err);
    if (expanded.isEmpty()) {
        out << "  " << padLabel(label) << "ERROR\n    include expansion failed: " << err << "\n";
        return 1;
    }
    // The p_<id> preamble is spliced only into the scaffolded main fragment, the
    // single stage that reads parameters by name.
    if (useScaffold && !preamble.isEmpty()) {
        expanded = PhosphorShaders::spliceAfterVersion(expanded, preamble);
    }

    const ShaderCompiler::Result result = ShaderCompiler::compile(expanded.toUtf8(), stage);
    // The did-you-mean hint only makes sense for the stage that received the
    // preamble: an unscaffolded stage cannot see any p_<id>, so suggesting
    // one would send the author after a name that stage can never use.
    return reportCompile(out, label, result, useScaffold ? declaredParamNames(info.parameters) : QStringList());
}

} // namespace PlasmaZones::ShaderValidate
