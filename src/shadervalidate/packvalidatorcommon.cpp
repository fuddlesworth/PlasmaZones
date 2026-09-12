// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators — see packvalidatorcommon.h.

#include "packvalidatorcommon.h"

#include "daemon/rendering/zoneentryscaffold.h"

#include <PhosphorFsLoader/PackPathGuard.h>

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QColor>
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
#include <cmath>
#include <limits>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorPointerShaders::PointerShaderEffect;
using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;
using PhosphorSurfaceShaders::SurfaceShaderEffect;

namespace PlasmaZones::ShaderValidate {

// Delegates to the RUNTIME's guard rather than re-deriving containment.
//
// This used to hand-roll the check with two comparison domains, and its comment
// claimed the result was "deliberately the stricter of the two". It was laxer, on
// the one case that matters most: when the candidate does not exist yet, the
// canonical compare was skipped entirely and the decision fell back to a purely
// LEXICAL prefix test. So `<pack>/link/future.frag`, where `link` is a symlink
// out of the pack, is lexically inside it and PASSED — while the runtime rejects
// it, because `resolveWithinDirectory` canonicalises the deepest EXISTING
// ancestor and re-appends the missing tail. A validator that accepts what
// production refuses is worse than no validator: it signs off the pack.
//
// Sharing the library guard also means this cannot drift from it again, and the
// non-existent-leaf case (a stage the author has not written yet, an
// --emit-preamble run before the shader) is one that guard already handles by
// design rather than by fallback.
std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel)
{
    // Reject, not Trust: every path this sees is PACK-declared, and a pack ships
    // its own assets, so an absolute path can only be a mistake or an escape.
    return PhosphorFsLoader::resolveWithinDirectory(rel, QDir(packDir).absolutePath(),
                                                    PhosphorFsLoader::AbsolutePathPolicy::Reject);
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

int reportPresetProblems(QTextStream& out, const QString& packDir, const QMap<QString, QVariantMap>& presets,
                         const QList<PresetLintParam>& declared)
{
    if (presets.isEmpty()) {
        return 0;
    }
    // Collected, then printed under its own header. Writing straight to the
    // stream put an unindented error line above a "metadata OK" that
    // contradicted it, while still returning a non-zero error count.
    QStringList lints;

    QHash<QString, PresetLintParam> byId;
    byId.reserve(declared.size());
    for (const PresetLintParam& p : declared) {
        byId.insert(p.id, p);
    }

    int problems = 0;
    for (auto it = presets.constBegin(); it != presets.constEnd(); ++it) {
        const QString& presetName = it.key();
        const QVariantMap& values = it.value();

        // A pack-declared preset's key IS its id, and the settings app builds a
        // filename from an id when the user duplicates the preset into an
        // editable one. The runtime refuses an unusable id on load, which drops
        // the preset with a log line the pack author will never see.
        //
        // Checked HERE rather than in the metadata schema. The schemas carried a
        // `propertyNames` rule for this, and the vendored valijson rejects every
        // name under it, valid ones included — so on the one family whose gate
        // actually applies the rule it refused legitimate packs, and on the
        // others it was never consulted at all. One predicate, shared with the
        // runtime that enforces it, beats a schema keyword that works on neither
        // path.
        // An AUTHORING rule, and this is the ONLY place it is enforced. Said plainly
        // because the previous wording claimed "the runtime will refuse it" and the
        // runtime does not: `parsePackPresets` deliberately keeps a key it cannot
        // love, because dropping one there would hide the preset from this very lint
        // and the pack would ship green with a log line no author reads. A pack
        // preset id also never becomes a filename — duplicating one mints a fresh
        // UUID — so there is nothing to refuse at runtime for safety's sake.
        //
        // What the rule protects is the PICKER: `applyPackBucket` takes the key as
        // both the id an assignment stores and the name rendered in every row, and
        // MaxNameChars truncation only runs for user presets. A 250-character key or
        // one carrying a bidi override mangles the row, unreported, for good.
        //
        // `isUsableId` covers the blank, control-character and separator cases;
        // the length bound here is the stricter NAME one, not its own id bound.
        if (!PhosphorShaders::ShaderPreset::isUsableId(presetName)
            || presetName.size() > PhosphorShaders::ShaderPreset::MaxNameChars) {
            // One multi-arg .arg, not a chain: a chained one substitutes into the
            // result of the previous substitution, so a `%2` inside the KEY (reachable
            // — `%` is neither a control character nor a separator, and this branch
            // also runs for a merely over-long key) would be replaced by the count.
            lints << QStringLiteral(
                         "preset '%1' has an unusable id: it must be non-blank, at most %2 characters, free of "
                         "control or formatting characters, and free of path separators. The id is also the name "
                         "the picker renders, and it is what an assignment stores. The runtime does not refuse "
                         "this — the pack would ship and render with an unreadable preset row")
                         .arg(presetName, QString::number(PhosphorShaders::ShaderPreset::MaxNameChars));
            ++problems;
            // NOT a `continue`: the values are independent of the key, so reporting
            // them in the same run spares the author a second round of errors after
            // they rename the preset.
        }
        for (auto vit = values.constBegin(); vit != values.constEnd(); ++vit) {
            const auto found = byId.constFind(vit.key());
            if (found == byId.constEnd()) {
                // "does not declare" also covers declared-and-DROPPED: this reads the
                // post-load parameter list, so a duplicate or invalid id, or an
                // image-typed parameter on the pointer arm (whose vocabulary has no
                // image type), is absent here too. Each arm lints the dropped
                // declaration separately, so the two lines appear together.
                lints << QStringLiteral(
                             "preset '%1' sets '%2', which the pack does not declare (or declared and "
                             "the loader dropped it; see the parameter lints above)")
                             .arg(presetName, vit.key());
                ++problems;
                continue;
            }

            const PresetLintParam& param = *found;
            const QVariant& value = vit.value();

            if (param.type == QLatin1String("bool")) {
                if (value.typeId() != QMetaType::Bool) {
                    // An AUTHORING rule, not a runtime break: every consumer reads this
                    // through QVariant::toBool, which accepts a number. Worded so it
                    // does not imply the pack would misbehave.
                    lints << QStringLiteral("preset '%1' sets '%2' to %3; a bool parameter wants true or false")
                                 .arg(presetName, vit.key(), value.toString());
                    ++problems;
                }
                continue;
            }
            if (param.type == QLatin1String("color") || param.type == QLatin1String("image")) {
                // COLOUR only. An image-typed value cannot fail this test, because
                // parsePackPresets coerces every image value with
                // `.toVariant().toString()` before the lint ever sees the map — so
                // `"tex": 7` arrives as the string "7" and is reported below as a
                // missing FILE, and `"tex": {}` arrives as "" and is accepted as "no
                // texture for this slot". Checking the raw JSON is the only way to
                // report those as type errors, and that means surfacing them out of
                // the parse rather than re-deriving them here.
                if (param.type == QLatin1String("color") && value.typeId() != QMetaType::QString) {
                    lints << QStringLiteral("preset '%1' sets '%2' to a non-color value").arg(presetName, vit.key());
                    ++problems;
                    continue;
                }
                // A colour-typed value is parsed with QColor at runtime, and an
                // unparseable one becomes an INVALID colour rather than an error.
                // Both consuming registries then fall back to the parameter's
                // DECLARED DEFAULT, so the pack renders exactly as if the preset had
                // never mentioned the parameter — "the preset did nothing" rather
                // than "the preset has a typo", and nothing anywhere says which.
                // Checking the string is a string caught only half of it.
                if (param.type == QLatin1String("color") && !QColor::isValidColorName(value.toString())) {
                    lints << QStringLiteral("preset '%1' sets '%2' to '%3', which is not a colour QColor can parse")
                                 .arg(presetName, vit.key(), value.toString());
                    ++problems;
                    continue;
                }
                // An image-typed preset value is a PATH, and what this can and cannot
                // check is worth stating, because the obvious check is unreachable.
                //
                // CONTAINMENT is deliberately NOT re-tested here, and the branch that
                // used to do it was dead code claiming coverage it could not give.
                // `parsePackPresets` resolves every image value under
                // AbsolutePathPolicy::Reject and DROPS the ones that escape, so by the
                // time this lint walks the PARSED map an escaping value is simply
                // absent and there is nothing left to refuse. Verified: a pack with
                // `"tex": "../../../etc/passwd"` reaches this loop with no `tex` entry
                // at all.
                //
                // That is also one of THREE gaps with the same shape, all of them
                // per-entry drops the parse makes before this lint runs, each leaving
                // only a log line: an escaping texture path, a JSON `null` value, and
                // a non-object preset BODY (which drops the whole preset). All three
                // land the author in the same "the preset did nothing" state, and
                // closing any of them means surfacing refusals out of
                // `parsePackPresets` rather than re-deriving them here, where they
                // cannot fire.
                //
                // EXISTENCE this can check, and nothing did: a value that survives the
                // parse is an absolute in-pack path, and a preset naming a texture the
                // pack does not ship binds nothing at runtime and falls back to the
                // parameter's default — the same silent no-op as an unparseable colour
                // above, and the same check an image param's `default` already gets.
                if (param.type == QLatin1String("image") && !packDir.isEmpty()) {
                    const QString declaredPath = value.toString();
                    if (!declaredPath.isEmpty() && !QFileInfo::exists(QDir(packDir).absoluteFilePath(declaredPath))) {
                        // Printed RELATIVE to the pack. What the lint holds is the
                        // parse's resolved absolute path, so the raw value named a
                        // string the author cannot find anywhere in their metadata.json
                        // — unlike every sibling lint, which quotes what they wrote.
                        lints << QStringLiteral("preset '%1' sets '%2' to '%3', which the pack does not contain")
                                     .arg(presetName, vit.key(), QDir(packDir).relativeFilePath(declaredPath));
                        ++problems;
                    }
                }
                continue;
            }

            // float / int: numeric, and inside the declared range when there
            // is one. A missing bound is not a problem — plenty of parameters
            // legitimately declare only one, or neither.
            if (!value.canConvert<double>() || value.typeId() == QMetaType::QString
                || value.typeId() == QMetaType::Bool) {
                lints << QStringLiteral("preset '%1' sets '%2' to a non-numeric value").arg(presetName, vit.key());
                ++problems;
                continue;
            }
            const double v = value.toDouble();
            // NO non-finite check, deliberately, and this is the note that keeps one
            // from being added as "defensive": every value here came through
            // QJsonDocument, whose parser refuses a non-finite number outright — a
            // literal `NaN` / `Infinity` token and an overflowing decimal like `1e400`
            // both fail the whole document with "illegal number", so the pack does not
            // load at all and the metadata gate reports that instead. Verified with a
            // Qt6 probe rather than assumed. A branch here could never fire, which is
            // the same dead-code-claiming-coverage the containment check above was.
            //
            // An int-typed parameter reaches the shader through a C++ cast that
            // TRUNCATES, so `"count": 1.5` silently becomes 1 and the author's
            // declared value is not the one that renders. A fractional literal
            // under an int parameter is always a mistake, and this is the one
            // place it can be said so before the pack ships.
            if (param.type == QLatin1String("int") && v != std::trunc(v)) {
                lints << QStringLiteral(
                             "preset '%1' sets '%2' to %3, but '%2' is an int parameter, so the value "
                             "truncates to %4 at runtime")
                             .arg(presetName, vit.key(), QString::number(v), QString::number(std::trunc(v)));
                ++problems;
            }
            // And inside int's own range, independently of the pack's declared one.
            // The declared bounds are optional, so `{"count": 1e18}` under an int
            // parameter with no min/max linted clean and then hit a
            // static_cast<int> at runtime, which is undefined behaviour rather than
            // a clamp. The type is the bound when the author gave none.
            if (param.type == QLatin1String("int")
                && (v < double(std::numeric_limits<int>::min()) || v > double(std::numeric_limits<int>::max()))) {
                lints << QStringLiteral("preset '%1' sets '%2' to %3, which does not fit in an int parameter")
                             .arg(presetName, vit.key(), QString::number(v));
                ++problems;
            }
            if (param.minValue.isValid() && v < param.minValue.toDouble()) {
                lints << QStringLiteral("preset '%1' sets '%2' to %3, below its declared minimum %4")
                             .arg(presetName, vit.key(), QString::number(v),
                                  QString::number(param.minValue.toDouble()));
                ++problems;
            }
            if (param.maxValue.isValid() && v > param.maxValue.toDouble()) {
                lints << QStringLiteral("preset '%1' sets '%2' to %3, above its declared maximum %4")
                             .arg(presetName, vit.key(), QString::number(v),
                                  QString::number(param.maxValue.toDouble()));
                ++problems;
            }
        }
    }

    if (!lints.isEmpty()) {
        // padLabel, not hand-counted spaces: every other header in this report goes
        // through it, and a change to its column width has to move this one too.
        out << "  " << padLabel(QStringLiteral("presets")) << "ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
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

int reportPresetProblems(QTextStream& out, const QString& packDir, const QMap<QString, QVariantMap>& presets,
                         const QList<ShaderRegistry::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packDir, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packDir, const QMap<QString, QVariantMap>& presets,
                         const QList<AnimationShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packDir, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packDir, const QMap<QString, QVariantMap>& presets,
                         const QList<SurfaceShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packDir, presets, toLintParams(declared));
}

int reportPresetProblems(QTextStream& out, const QString& packDir, const QMap<QString, QVariantMap>& presets,
                         const QList<PointerShaderEffect::ParameterInfo>& declared)
{
    return reportPresetProblems(out, packDir, presets, toLintParams(declared));
}

// Build the `p_<id>` name list a pack declares, for the did-you-mean hint.
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
