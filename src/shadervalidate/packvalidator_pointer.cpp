// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pointer arm of plasmazones-shader-validate — see packvalidators.h.
// Reproduces the pointer runtime's GLSL assembly (PointerShaderEffect + the
// pPointer entry scaffold + the generated p_<id> preamble + include expansion
// over PointerShaderRegistry::includePathsFor) and bakes every stage TWICE:
// once on the preview (Qt-RHI, UBO) dialect through headless QShaderBaker,
// and once on the compositor (PLASMAZONES_KWIN, default-block uniforms)
// dialect through glslang in default mode, because the compositor is where
// every pointer pack actually ships and the two dialects do not declare the
// same identifiers.
//
// The pointer-specific lints are the fields no sibling family has: the `layer`
// token, `reach` / `reachParam`, and `trailSeconds`. Each of those is silently
// repaired by PointerShaderEffect::fromJson (an unknown layer falls back to
// below, reach and trailSeconds are clamped, a reachParam naming nothing
// numeric is ignored), so every one of them is linted against the RAW metadata
// — a lint over the parsed struct could never surface the author's mistake.
// The sampler lints (needsCursor, textures, the multipass channels) exist
// because the contract itself says the static gate for them lives here: an
// undeclared sampler reads texture unit 0 on the compositor, which is
// undefined content rather than a clean transparent texel.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

#include <algorithm>
#include <cmath>

using PhosphorPointerShaders::PointerShaderEffect;
using PhosphorPointerShaders::PointerShaderRegistry;
using PhosphorRendering::ShaderCompiler;
namespace PointerShaderContract = PhosphorPointerShaders::PointerShaderContract;

namespace PlasmaZones::ShaderValidate {

namespace {

// The lowest gate value the preview's fastest moment may open to before the
// pack counts as invisible there. Below this a stage that should be showing
// the effect is showing nothing a user could judge.
constexpr double kMinPreviewGate = 0.15;

// The smallest reach, in logical px, a reachParam may let the user pick. The
// damage rect is the trail's bounding box inflated by the reach, so a reach
// this small clips a pack to a sliver around the path: nothing the pack
// paints further out ever reaches the screen, and the user sees a broken
// pack rather than a small one.
//
// A USABILITY floor, deliberately above the runtime's hard one. Load clamps
// reach to PointerShaderEffect::kMinReach (1.0), which the library documents
// as the smallest value that still turns a point into a region — so a reach
// between the two draws, it is just clipped too tight to be worth shipping.
// The lints below have to say that rather than claim the pack cannot draw.
//
// Two bundled packs (ink, windtrail) declare a reachParam minimum of exactly
// this value, so they pass on the strict `<` with no margin at all. Raising
// the floor, or relaxing the comparison to `<=`, breaks both at once.
constexpr double kUsableReachFloor = 4.0;

// GLSL smoothstep, so the lint computes the same number pointerSpeedGate does.
double smoothstepAt(double edge0, double edge1, double x)
{
    if (edge1 <= edge0) {
        return x < edge0 ? 0.0 : 1.0;
    }
    double t = (x - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// GLSL source with comments blanked, so a call that only appears in a comment
// cannot be mistaken for a real one. Newlines are kept so nothing else shifts.
QString withoutComments(const QString& source)
{
    QString out = source;
    const int n = out.size();
    for (int i = 0; i < n - 1; ++i) {
        if (out[i] == QLatin1Char('/') && out[i + 1] == QLatin1Char('/')) {
            while (i < n && out[i] != QLatin1Char('\n')) {
                out[i++] = QLatin1Char(' ');
            }
        } else if (out[i] == QLatin1Char('/') && out[i + 1] == QLatin1Char('*')) {
            while (i < n) {
                const bool end = i < n - 1 && out[i] == QLatin1Char('*') && out[i + 1] == QLatin1Char('/');
                if (out[i] != QLatin1Char('\n')) {
                    out[i] = QLatin1Char(' ');
                }
                ++i;
                if (end) {
                    // Blank the closing slash without stepping past it: the
                    // for loop's own increment moves on, so the character
                    // after the comment (`/**///x`, or a second block
                    // comment starting right there) is not skipped.
                    out[i] = QLatin1Char(' ');
                    break;
                }
            }
        }
    }
    return out;
}

// Whether `source` reads `token` as a whole identifier. A plain `contains`
// would count `p_width` as a use of `p_wid`, and would then stay silent about
// a parameter nothing reads; the same goes for `uTexture1` inside
// `uTexture10` or `iChannel0` inside `iChannel0Resolution`.
bool mentionsToken(const QString& source, const QString& token)
{
    int at = 0;
    while ((at = source.indexOf(token, at)) >= 0) {
        const int before = at - 1;
        const int after = at + token.size();
        const bool boundedLeft =
            before < 0 || !(source[before].isLetterOrNumber() || source[before] == QLatin1Char('_'));
        const bool boundedRight =
            after >= source.size() || !(source[after].isLetterOrNumber() || source[after] == QLatin1Char('_'));
        if (boundedLeft && boundedRight) {
            return true;
        }
        at += token.size();
    }
    return false;
}

bool mentionsParam(const QString& source, const QString& id)
{
    return mentionsToken(source, QStringLiteral("p_") + id);
}

// The `p_<id>` names a pack passes as the `activationSpeed` argument of
// pointerSpeedGate() (its second argument) or of the pointerActivationGate()
// wrapper (its only argument), which is what a pack whose threshold defaults
// to 0 calls so the filtered walk is skipped there. Both are scanned, or a
// pack moving to the wrapper would silently leave this lint's coverage.
// Parsed rather than regexed because pointerSpeedGate's first argument is
// routinely a call of its own (`pointerSpeedGate(length(v), p_speed)`), so
// the split has to happen at the top-level comma.
QStringList speedGateParamNames(const QString& strippedSource)
{
    struct Callee
    {
        QLatin1String name;
        bool argAfterComma;
    };
    static const Callee kCallees[] = {
        {QLatin1String("pointerSpeedGate"), true},
        {QLatin1String("pointerActivationGate"), false},
    };
    const QString& source = strippedSource;
    QStringList found;
    for (const Callee& callee : kCallees) {
        int at = 0;
        while ((at = source.indexOf(callee.name, at)) >= 0) {
            // Identifier-bounded on the left, like mentionsToken: a pack
            // helper named e.g. my_pointerSpeedGate is not the shared gate.
            // The match is case-sensitive, so a name that also changes the
            // case (myPointerSpeedGate) never reaches this check at all.
            if (at > 0 && (source[at - 1].isLetterOrNumber() || source[at - 1] == QLatin1Char('_'))) {
                at += callee.name.size();
                continue;
            }
            int i = at + callee.name.size();
            while (i < source.size() && source[i].isSpace()) {
                ++i;
            }
            if (i >= source.size() || source[i] != QLatin1Char('(')) {
                at += callee.name.size();
                continue;
            }
            // Walk the argument list, remembering where the top-level comma
            // fell.
            int depth = 0;
            int comma = -1;
            int j = i;
            for (; j < source.size(); ++j) {
                const QChar c = source[j];
                if (c == QLatin1Char('(')) {
                    ++depth;
                } else if (c == QLatin1Char(')')) {
                    if (--depth == 0) {
                        break;
                    }
                } else if (c == QLatin1Char(',') && depth == 1 && comma < 0) {
                    comma = j;
                }
            }
            const int argStart = callee.argAfterComma ? comma + 1 : i + 1;
            const bool haveArg = callee.argAfterComma ? comma > 0 : comma < 0;
            if (haveArg && j < source.size()) {
                const QString arg = source.mid(argStart, j - argStart);
                // The first identifier-bounded `p_<id>` token anywhere in the
                // argument: the argument is routinely an expression
                // (`p_activationSpeed * pointerScale()`, `2.0 * p_speed`,
                // `(p_speed)`), and a pack written any of those ways must not
                // slip out of the lint's coverage.
                int at2 = 0;
                while ((at2 = arg.indexOf(QLatin1String("p_"), at2)) >= 0) {
                    const bool boundedLeft =
                        at2 == 0 || !(arg[at2 - 1].isLetterOrNumber() || arg[at2 - 1] == QLatin1Char('_'));
                    int end = at2 + 2;
                    while (end < arg.size() && (arg[end].isLetterOrNumber() || arg[end] == QLatin1Char('_'))) {
                        ++end;
                    }
                    const QString id = arg.mid(at2 + 2, end - at2 - 2);
                    if (boundedLeft && PhosphorShaders::isValidParamId(id)) {
                        if (!found.contains(id)) {
                            found << id;
                        }
                        break;
                    }
                    at2 = end;
                }
            }
            at = j > at ? j : at + callee.name.size();
        }
    }
    return found;
}

// A stage's source, or an empty string when it cannot be read. Missing and
// unreadable stages are linted elsewhere; the scans here just skip them.
QString readStage(const QString& path)
{
    QFile f(path);
    if (!QFile::exists(path) || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

// Assemble one stage on the COMPOSITOR dialect exactly as
// pointerdecorationshader.cpp does and compile it through glslangValidator.
// Returns 1 on failure, 0 on success.
//
// The main fragment gets the pPointer entry scaffold, include expansion, the
// p_<id> preamble and then the shared KWin define block after #version. A
// buffer pass and a declared vertex stage get include expansion and the
// define block only: the compositor splices NO preamble into them (buffer
// sources address parameters by raw customParams slot, by contract), so
// splicing one here would pass a buffer that fails live.
//
// The splice order mirrors the runtime: each spliceAfterVersion lands its
// block immediately below #version, so splicing the preamble first and the
// define block second leaves the define block ABOVE the preamble, which is
// what the compositor produces.
//
// COVERAGE BOUNDARY, the same one the animation arm records: the source is
// handed to glslang with the pack's `#version 450` intact, while KWin
// recompiles at the GL context's core version. A construct legal at 450 and
// illegal there still passes here. The compositor also splices KWin's own
// colour-management block ahead of the source, which is not reproduced
// here, so an identifier colliding with that block passes here and fails
// live. What this does cover is every identifier the two dialects disagree
// on, which is the class that shipped uncaught while only the preview
// branch was baked.
//
// INCLUDES are expanded the way the compositor expands them, through the
// resolver with the registry roots alone: the angle form searches only those
// roots, and only the quoted form looks beside the including file. The
// preview bake (ShaderCompiler::expandSource) is more forgiving and lets an
// angle include find a pack-local file, so a pack written that way baked
// clean everywhere and then failed include expansion where it ships.
int bakeCompositorStage(QTextStream& out, const PointerShaderEffect& eff, const QString& path, const QString& label,
                        const QString& stage, const QStringList& includePaths, bool scaffold)
{
    if (!QFile::exists(path)) {
        return 0; // an absent stage is already linted by the caller
    }
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        // Hard failure rather than a skip, for the reason the animation arm
        // gives: a pack cannot reach a release with the branch it ships on
        // uncompiled, and a quiet degrade is how that happened before.
        // Explained once per run, as the animation arm does; every stage
        // still counts the error.
        static bool explained = false;
        out << "  " << padLabel(label) << "ERROR (compositor)\n";
        if (!explained) {
            explained = true;
            out << "    neither glslangValidator nor glslang found on PATH. One of them is required to "
                   "compile the compositor dialect every pointer pack ships on (install the glslang package)\n";
        }
        return 1;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out << "  " << padLabel(label) << "ERROR\n    cannot read " << path << "\n";
        return 1;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    const QString assembled = scaffold
        ? PhosphorShaders::assembleEntryPoint(raw, PointerShaderRegistry::pointerEntryPrologue(),
                                              PointerShaderRegistry::pointerEntryCandidates())
        : raw;
    QString err;
    QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembled, QFileInfo(path).absolutePath(),
                                                                         includePaths, &err);
    if (src.isEmpty()) {
        // The resolver returns empty for any failure — a missing file, an
        // unreadable one, a malformed directive, a cycle. Naming the
        // angle-versus-quoted case as THE cause sent an author with a simple
        // typo off changing bracket style, so it is offered as the likely
        // explanation rather than asserted, and the resolver's own message
        // leads.
        out << "  " << padLabel(label) << "ERROR (compositor)\n    include expansion failed the way the compositor "
            << "expands it: " << err
            << "\n    (if the file sits beside this one, note that the compositor resolves an angle include only "
            << "against the registry roots; the quoted form is the pack-local one)\n";
        return 1;
    }
    if (scaffold) {
        src = PhosphorShaders::spliceAfterVersion(src, PointerShaderRegistry::paramPreamble(eff));
    }
    src = PhosphorShaders::spliceAfterVersion(src, PhosphorShaders::kwinDefineBlock());
    return reportCompositorCompile(out, label, stage, src, tool);
}

} // namespace

// Validate one POINTER pack directory (data/pointer/*). Reproduces the pointer
// runtime's fragment assembly — the pPointer entry scaffold (an entry-only
// pack gets a generated main(); a pack with its own main() passes through
// unchanged) + include expansion + the generated p_<id> preamble — and bakes
// it on BOTH dialects. Returns the error count.
//
// Unlike the animation and surface arms, the compositor branch (`#define
// PLASMAZONES_KWIN`, default-block uniforms) IS baked here, out of process
// through glslang in default mode, and it is the bake that matters: every
// shipping pointer pack compiles through that branch, while the Qt-RHI
// #else branch is only the settings preview. The two branches declare
// different identifiers (the preview's UBO carries qt_Matrix, qt_Opacity and
// the rest of BaseUniforms), so a pack that bakes clean on the preview can
// still fail on the path that ships. The preview bake stays too, since a
// pack that previews as a compile error is broken in the browser where packs
// are chosen.
int validatePointerPack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        out << name << "\n  metadata       ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        out << name << "\n  metadata       ERROR\n    invalid JSON: " << perr.errorString() << "\n  → 1 error\n\n";
        return 1;
    }
    const QJsonObject root = doc.object();

    // Parse WITHOUT a source dir so every declared path stays verbatim, then
    // confine each one here: passing packDir would let fromJson's own guard
    // clear a rejected path, which reads downstream as "absent" rather than
    // "rejected". Same split the surface arm uses.
    PointerShaderEffect eff = PointerShaderEffect::fromJson(root);
    eff.sourceDir = QDir(packDir).absolutePath();
    const auto confineToPack = [&packDir](QString& path) {
        return confinePackPathInPlace(packDir, path);
    };
    if (!confineToPack(eff.fragmentShaderPath)) {
        out << name
            << "\n  metadata       ERROR\n    fragmentShader path escapes the pack directory (path traversal "
               "rejected)\n  → 1 error\n\n";
        return 1;
    }
    for (QString& b : eff.bufferShaderPaths) {
        if (!confineToPack(b)) {
            out << name
                << "\n  metadata       ERROR\n    bufferShaders path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
    }
    if (!confineToPack(eff.vertexShaderPath)) {
        out << name
            << "\n  metadata       ERROR\n    vertexShader path escapes the pack directory (path traversal "
               "rejected)\n  → 1 error\n\n";
        return 1;
    }
    if (!eff.isValid()) {
        out << name << "\n  metadata       ERROR\n    missing required field (id / fragmentShader)\n  → 1 error\n\n";
        return 1;
    }
    const QString fragLabel = QFileInfo(eff.fragmentShaderPath).fileName();

    out << name << "  (" << eff.parameters.size() << " param" << (eff.parameters.size() == 1 ? "" : "s") << ", "
        << eff.textures.size() << " texture" << (eff.textures.size() == 1 ? "" : "s") << ", "
        << (eff.isMultipass ? "multipass" : "single-pass") << ")\n";

    int errors = 0;
    QStringList lints;

    // ── parameters ──
    QSet<QString> seenParamIds;
    const QJsonArray declaredParams = root.value(QLatin1String("parameters")).toArray();
    if (declaredParams.size() > PointerShaderContract::kMaxDeclaredParameters) {
        lints << QStringLiteral("too many parameters: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredParams.size()))
                     .arg(PointerShaderContract::kMaxDeclaredParameters);
    }
    for (const QJsonValue& v : declaredParams) {
        // Raw, not eff.parameters: fromJson drops an empty or duplicate id
        // outright, so those two authoring slips are invisible downstream.
        const QJsonObject pObj = v.toObject();
        const QString id = pObj.value(QLatin1String("id")).toString();
        const QString type = pObj.value(QLatin1String("type")).toString();
        if (id.isEmpty()) {
            lints << QStringLiteral("parameter with an empty `id` (dropped at load)");
            continue;
        }
        if (seenParamIds.contains(id)) {
            lints << QStringLiteral("duplicate parameter id '%1' (the later entry is dropped at load)").arg(id);
            continue;
        }
        seenParamIds.insert(id);
        if (!kValidParamTypes.contains(type)) {
            lints << QStringLiteral("unknown param type '%1' for '%2' (pointer params are %3)")
                         .arg(type, id, kValidParamTypes.join(QLatin1String("/")));
        }
        if (!PhosphorShaders::isValidParamId(id)) {
            lints << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(id);
        }
    }

    // ── layer ──
    // fromJson falls back to below on an unknown token with only a journal
    // warning, so a typo'd `layer` silently changes where the pack paints.
    const QJsonValue layerValue = root.value(QLatin1String("layer"));
    if (!layerValue.isUndefined()) {
        const QString layerToken = layerValue.toString();
        if (layerToken != QLatin1String("below") && layerToken != QLatin1String("above")) {
            lints << QStringLiteral("layer must be \"below\" or \"above\": %1 (falls back to below at load)")
                         .arg(layerValue.isString() ? layerToken : QStringLiteral("not a string"));
        }
    }

    // ── reach / reachParam ──
    const QJsonValue reachValue = root.value(QLatin1String("reach"));
    if (!reachValue.isUndefined()) {
        if (!reachValue.isDouble()) {
            lints << QStringLiteral("reach is not a number (the default %1 is used at load)")
                         .arg(PointerShaderEffect().reach);
        } else if (reachValue.toDouble() < 0.0 || reachValue.toDouble() > PointerShaderEffect::kMaxReach) {
            lints << QStringLiteral("reach out of range [0, %1]: %2 (clamped at load)")
                         .arg(PointerShaderEffect::kMaxReach)
                         .arg(reachValue.toDouble());
        } else if (eff.reachParam.isEmpty() && reachValue.toDouble() < kUsableReachFloor) {
            // The same floor the reachParam arm enforces: under it the damage
            // rect is a sliver around the path (for a resting pointer, empty),
            // so the pack never draws, and a shader windowing on the reach
            // meets equal smoothstep edges at 0.
            lints << QStringLiteral(
                         "reach %1 is under the %2 logical px floor: the damage rect is the path inflated by "
                         "the reach, so the pack is clipped to a sliver around it (load clamps reach to %3 px, "
                         "below which a resting pointer's rect has no area at all)")
                         .arg(reachValue.toDouble())
                         .arg(kUsableReachFloor)
                         .arg(PointerShaderEffect::kMinReach);
        }
    }
    if (!eff.reachParam.isEmpty()) {
        // resolvedReach only honours a reachParam that names a declared float
        // or int parameter; anything else falls back to `reach` silently, and
        // the damage rect the host derives is then wrong for every value the
        // user picks.
        const auto declared = std::find_if(eff.parameters.cbegin(), eff.parameters.cend(),
                                           [&eff](const PointerShaderEffect::ParameterInfo& p) {
                                               return p.id == eff.reachParam;
                                           });
        if (declared == eff.parameters.cend()) {
            lints << QStringLiteral("reachParam '%1' names no declared parameter (reach falls back to %2 at load)")
                         .arg(eff.reachParam)
                         .arg(eff.reach);
        } else if (declared->type != QLatin1String("float") && declared->type != QLatin1String("int")) {
            lints << QStringLiteral(
                         "reachParam '%1' has type '%2', which is not float or int (reach falls back to %3 "
                         "at load)")
                         .arg(eff.reachParam, declared->type)
                         .arg(eff.reach);
        } else {
            if (declared->maxValue.isValid() && declared->maxValue.toDouble() > PointerShaderEffect::kMaxReach) {
                lints << QStringLiteral(
                             "reachParam '%1' allows up to %2, past the %3 reach cap (clamped at load, so the "
                             "pack paints outside its damage rect)")
                             .arg(eff.reachParam)
                             .arg(declared->maxValue.toDouble())
                             .arg(PointerShaderEffect::kMaxReach);
            }
            // The floor matters as much as the cap: a reach the user can drag
            // down to a few pixels clips the pack to nothing around the path.
            if (declared->minValue.isValid() && declared->minValue.toDouble() < kUsableReachFloor) {
                lints << QStringLiteral(
                             "reachParam '%1' allows a minimum of %2 logical px, below the %3 px floor (a reach "
                             "that small clips the pack to nothing: the damage rect is the path inflated by "
                             "the reach, and nothing painted outside it reaches the screen)")
                             .arg(eff.reachParam)
                             .arg(declared->minValue.toDouble())
                             .arg(kUsableReachFloor);
            }
        }
    }

    // ── trailSeconds ──
    // Zero means the pack never gets a frame after an event, which is a live
    // pack that draws nothing. fromJson clamps into [0, 60] without comment.
    const QJsonValue trailValue = root.value(QLatin1String("trailSeconds"));
    if (!trailValue.isUndefined()) {
        if (!trailValue.isDouble()) {
            lints << QStringLiteral("trailSeconds is not a number (the default %1 is used at load)")
                         .arg(PointerShaderEffect().trailSeconds);
        } else if (trailValue.toDouble() <= 0.0) {
            lints << QStringLiteral(
                         "trailSeconds must be positive: %1 (the pack would never be repainted after an "
                         "event)")
                         .arg(trailValue.toDouble());
        } else if (trailValue.toDouble() > 60.0) {
            lints
                << QStringLiteral("trailSeconds out of range (0, 60]: %1 (clamped at load)").arg(trailValue.toDouble());
        }
    }

    // ── stage text, comment-stripped, for every scan below ──
    // Assembled once: the parameter sweep, the speed-gate lint and the
    // sampler lints all read the same text, and they must agree on what a
    // stage is. Main fragment, buffer passes and the declared vertex stage;
    // the shared pointer.vert is not a pack stage and reads no parameters.
    const QString fragText = withoutComments(readStage(eff.fragmentShaderPath));
    QString bufferText;
    for (const QString& buf : eff.bufferShaderPaths) {
        bufferText += withoutComments(readStage(buf));
        bufferText += QLatin1Char('\n');
    }
    const QString vertText =
        eff.vertexShaderPath.isEmpty() ? QString() : withoutComments(readStage(eff.vertexShaderPath));
    const QString allStages = fragText + QLatin1Char('\n') + bufferText + QLatin1Char('\n') + vertText;
    const bool anyStage = !fragText.isEmpty() || !bufferText.isEmpty() || !vertText.isEmpty();

    // ── declared but never used ──
    // A parameter the shader never reads still costs a slot and still draws a
    // control in the settings app, so the user is handed a slider that moves
    // and changes nothing. Nothing repairs this at load, which is why it needs
    // saying here: the pack works, and the control is dead.
    //
    // Every stage is scanned, not just the fragment, since a parameter may
    // legitimately be read only by a buffer pass or the vertex stage. A
    // buffer pass gets no p_<id> preamble on any runtime, so it can only reach
    // the parameters through their raw customParams / customColors lanes; a
    // buffer stage that reads a pool by slot is therefore taken to read every
    // parameter in that pool, because by-name attribution is impossible there
    // and the alternative is a lint that fires on every multipass pack that
    // does the only thing it can.
    if (anyStage) {
        const bool bufferReadsScalars = mentionsToken(bufferText, QStringLiteral("customParams"));
        const bool bufferReadsColors = mentionsToken(bufferText, QStringLiteral("customColors"));
        for (const PointerShaderEffect::ParameterInfo& p : eff.parameters) {
            if (!PhosphorShaders::isValidParamId(p.id)) {
                continue; // already linted above, and it has no p_ define
            }
            // The reachParam parameter is consumed by the HOST: it sizes the
            // damage rect and is handed back to every stage as uPointerFlags.y
            // (pointerReach()), which is how the shared helpers tell a pack to
            // read its budget so the two cannot drift. A pack that reads it
            // only that way has a live control, not a dead one.
            if (!eff.reachParam.isEmpty() && p.id == eff.reachParam) {
                if (!mentionsToken(allStages, QStringLiteral("pointerReach")) && !mentionsParam(allStages, p.id)) {
                    lints << QStringLiteral(
                                 "reachParam '%1' sizes the damage rect but no stage reads "
                                 "pointerReach() or p_%1, so the shader cannot be bounding itself "
                                 "to the reach it declares")
                                 .arg(p.id);
                }
                continue;
            }
            const bool bySlot = p.type == QLatin1String("color") ? bufferReadsColors : bufferReadsScalars;
            if (!bySlot && !mentionsParam(allStages, p.id)) {
                lints << QStringLiteral(
                             "parameter '%1' is declared but no stage reads p_%1 (it still takes a "
                             "slot and still draws a control that does nothing)")
                             .arg(p.id);
            }
        }
    }

    // ── speed gate defaults ──
    // Every lint above catches metadata the loader silently repairs. This one
    // catches the opposite: a pack that loads perfectly, compiles cleanly, and
    // then draws nothing. pointerSpeedGate() is smoothstep(a, 2a, speed), so a
    // threshold whose DEFAULT sits above what the preview's pointer can reach
    // leaves the gate shut across the whole lap, and the pack shows an empty
    // stage in the browser where users pick packs. The windtrail pack shipped
    // exactly that way, past a clean run of every other lint here.
    //
    // Only the default is linted, not the range: a user who raises the
    // threshold themselves has asked for a pack that waits for a fast flick.
    // Scanned over the same assembled stage text as the parameter sweep, so a
    // gate placed in a buffer pass or the vertex stage is seen too.
    if (anyStage) {
        const QStringList gateIds = speedGateParamNames(allStages);
        for (const QString& gateId : gateIds) {
            const auto declared = std::find_if(eff.parameters.cbegin(), eff.parameters.cend(),
                                               [&gateId](const PointerShaderEffect::ParameterInfo& p) {
                                                   return p.id == gateId;
                                               });
            if (declared == eff.parameters.cend()) {
                lints << QStringLiteral(
                             "a speed gate (pointerSpeedGate or pointerActivationGate) is passed p_%1, which "
                             "is no declared parameter (it expands to an unwritten slot, so the gate reads "
                             "whatever is in it)")
                             .arg(gateId);
                continue;
            }
            bool numeric = false;
            const double threshold = declared->defaultValue.toDouble(&numeric);
            // A default of 0 is the documented "no threshold" case and
            // always draws, which is what the packs that ship the
            // parameter at 0 rely on.
            if (!numeric || threshold <= 0.0) {
                continue;
            }
            const double gate =
                smoothstepAt(threshold, threshold * 2.0, PointerShaderContract::kPreviewPeakSpeedPxPerSecond);
            if (gate < kMinPreviewGate) {
                lints << QStringLiteral(
                             "parameter '%1' gates drawing on speed and defaults to %2 px/s, but the "
                             "preview pointer never exceeds %3 px/s, so the gate opens to at most %4 "
                             "of full and the pack previews as an empty stage")
                             .arg(gateId)
                             .arg(threshold, 0, 'g', 4)
                             .arg(PointerShaderContract::kPreviewPeakSpeedPxPerSecond, 0, 'g', 4)
                             .arg(gate, 0, 'f', 2);
            }
        }
    }

    // ── mirrored trailSeconds ──
    // A pack that declares `kTrailSeconds` (as a const or a #define, in any
    // stage) is mirroring the metadata trailSeconds, and nothing else ties
    // the two together: a metadata edit alone would leave the pack fading
    // against the wrong window and freezing its last frame when the host
    // went quiet. Packs that fade on a constant of their own (halo's
    // kIdleSeconds, afterglow's kIdleCutSeconds) keep a margin under the
    // window by design and are not held here. Every declaration is checked,
    // so two stages that disagree with each other are both reported.
    if (anyStage) {
        static const QRegularExpression kMirror(
            QStringLiteral("(?:\\bconst\\s+float\\s+kTrailSeconds\\s*=\\s*|#\\s*define\\s+kTrailSeconds\\s+)"
                           "([0-9]+\\.?[0-9]*|\\.[0-9]+)"));
        // A declaration the value pattern cannot capture (an expression, a
        // named constant, an exponent form) would otherwise slip through with
        // no diagnostic at all, which is worse than a mismatch: the author
        // believes the mirror is being checked. Count the declarations and the
        // captures separately, and report the shortfall.
        static const QRegularExpression kMirrorToken(
            QStringLiteral("(?:\\bconst\\s+float\\s+kTrailSeconds\\s*=|#\\s*define\\s+kTrailSeconds\\b)"));
        int declaredMirrors = 0;
        auto tokens = kMirrorToken.globalMatch(allStages);
        while (tokens.hasNext()) {
            tokens.next();
            ++declaredMirrors;
        }
        int capturedMirrors = 0;
        auto matches = kMirror.globalMatch(allStages);
        while (matches.hasNext()) {
            const QRegularExpressionMatch m = matches.next();
            ++capturedMirrors;
            const double mirrored = m.captured(1).toDouble();
            if (std::abs(mirrored - eff.trailSeconds) > 1e-6) {
                // eff.trailSeconds is the CLAMPED value, which is the right
                // one to compare against because it is what the pack runs on.
                // It is the wrong one to quote back when the two differ: an
                // author who wrote 90 was being told their metadata said 60.
                const double declared = root.value(QLatin1String("trailSeconds")).toDouble(eff.trailSeconds);
                const QString metadataText = std::abs(declared - eff.trailSeconds) > 1e-6
                    ? QStringLiteral("%1 in the metadata (clamped to %2 at load)").arg(declared).arg(eff.trailSeconds)
                    : QStringLiteral("%1 in the metadata").arg(eff.trailSeconds);
                lints << QStringLiteral(
                             "kTrailSeconds is %1 in the shader but trailSeconds is %2 "
                             "(the pack fades against the wrong window)")
                             .arg(mirrored)
                             .arg(metadataText);
            }
        }
        if (declaredMirrors > capturedMirrors) {
            lints << QStringLiteral(
                         "kTrailSeconds is declared %1 time(s) but only %2 could be read as a plain number, so "
                         "the rest are NOT checked against the metadata (write the mirror as a decimal literal)")
                         .arg(declaredMirrors)
                         .arg(capturedMirrors);
        }
    }

    // ── cursor sprite ──
    // The contract puts the static gate for these here. On the compositor an
    // unbound sampler reads texture unit 0, which is whatever happened to be
    // bound last, not a transparent texel, so a shader that samples the
    // sprite without declaring needsCursor paints undefined content. The
    // other direction is merely waste: a sprite bound and uploaded every
    // frame for nothing.
    const bool declaredNeedsCursor = root.value(QLatin1String("needsCursor")).toBool(false);
    if (anyStage) {
        const bool readsCursor = mentionsToken(allStages, QString::fromLatin1(PointerShaderContract::kUCursorSprite));
        if (readsCursor && !declaredNeedsCursor) {
            lints << QStringLiteral(
                "a stage samples uCursorSprite but the pack does not declare `needsCursor` (the sampler is "
                "unbound, and on the compositor it reads whatever texture unit 0 holds)");
        }
        if (declaredNeedsCursor && !readsCursor) {
            lints << QStringLiteral(
                "needsCursor is declared but no stage samples uCursorSprite (the sprite is bound and uploaded "
                "every frame for nothing)");
        }

        // samplesTrail decides whether this pack's trailSeconds gets a say in
        // how the shared history ring is spaced, so a wrong answer is not
        // cosmetic: declaring false while reading the trail leaves the pack
        // drawing from slots spaced for somebody else, and declaring true (or
        // saying nothing) while reading none of it coarsens the stroke of
        // every trail pack chained beside it. Both directions are checked
        // against the stage sources so the declaration cannot drift.
        // The uniform itself appears only inside pointer_lib.glsl, which is
        // spliced in at bake time and is not part of the pack's own sources,
        // so looking for it alone would say "reads nothing" about every pack
        // in the bundle. What a pack writes is one of the accessors. These are
        // every helper in pointer_lib.glsl that reaches uPointerTrail, plus
        // the uniform for a pack that indexes the array directly.
        static const QLatin1String kTrailReaders[] = {
            QLatin1String("pointerTrailAt"),
            QLatin1String("pointerTrailCount"),
            QLatin1String("pointerLiveCount"),
            QLatin1String("pointerSmoothedAt"),
            QLatin1String("pointerSegmentDistance"),
            QLatin1String("pointerSegmentOutside"),
            QLatin1String("pointerSmoothSegmentDistance"),
        };
        bool readsTrail = mentionsToken(allStages, QString::fromLatin1(PointerShaderContract::kUPointerTrail));
        for (const QLatin1String& reader : kTrailReaders) {
            if (readsTrail) {
                break;
            }
            readsTrail = mentionsToken(allStages, QString(reader));
        }
        const QJsonValue samplesTrailValue = root.value(QLatin1String("samplesTrail"));
        const bool declaredSamplesTrail = samplesTrailValue.toBool(true);
        if (readsTrail && !declaredSamplesTrail) {
            lints << QStringLiteral(
                "samplesTrail is false but a stage reads uPointerTrail (the pack's own trailSeconds is then left "
                "out of the chain's sample spacing, so it draws from slots spaced for another pack)");
        }
        if (!readsTrail && declaredSamplesTrail) {
            lints << QStringLiteral(
                "no stage reads uPointerTrail, so declare `samplesTrail: false` (otherwise this pack's "
                "trailSeconds raises the sample spacing for every trail pack chained with it, while reading "
                "none of it itself)");
        }
    }

    // ── textures ──
    const QJsonArray declaredTextures = root.value(QLatin1String("textures")).toArray();
    if (declaredTextures.size() > PointerShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PointerShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        const QString texPath = v.toObject().value(QLatin1String("path")).toString();
        if (texPath.isEmpty()) {
            lints << QStringLiteral("texture entry with empty `path` (dropped at load, which shifts every later slot)");
        } else {
            const auto confined = confinedPackPath(packDir, texPath);
            if (!confined) {
                lints << QStringLiteral(
                             "texture path escapes the pack directory: %1 (rejected at load, sampler reads "
                             "transparent)")
                             .arg(texPath);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("texture missing: %1 (sampler reads transparent at load)").arg(texPath);
            }
        }
        // Raw wrap: fromJson clears an unrecognised token to the runtime
        // default, so only the raw value can surface the typo.
        const QString wrap = v.toObject().value(QLatin1String("wrap")).toString();
        if (!wrap.isEmpty() && !PointerShaderContract::isValidWrapToken(wrap)) {
            lints
                << QStringLiteral("texture wrap not in {clamp,repeat,mirror}: %1 (cleared to clamp at load)").arg(wrap);
        }
    }
    // Slot use against the declaration, both ways: uTexture<N> without a
    // declared slot N is the same unit-0 read the cursor lint describes, and
    // a declared slot nothing samples is an image decoded and uploaded for
    // nothing. Counted from the RAW array, since that is what fromJson slots
    // (an over-cap entry is dropped and its sampler is unbound).
    if (anyStage) {
        const int declaredSlots =
            std::min(static_cast<int>(declaredTextures.size()), PointerShaderContract::kMaxUserTextureSlots);
        static const QStringList kTextureSamplers = {QString::fromLatin1(PointerShaderContract::kUTexture1),
                                                     QString::fromLatin1(PointerShaderContract::kUTexture2),
                                                     QString::fromLatin1(PointerShaderContract::kUTexture3)};
        for (int slot = 0; slot < kTextureSamplers.size(); ++slot) {
            const bool reads = mentionsToken(allStages, kTextureSamplers.at(slot));
            const bool declaredSlot = slot < declaredSlots;
            if (reads && !declaredSlot) {
                lints << QStringLiteral(
                             "a stage samples %1 but the pack declares no texture in slot %2 (the sampler is "
                             "unbound, and on the compositor it reads whatever texture unit 0 holds)")
                             .arg(kTextureSamplers.at(slot))
                             .arg(slot);
            } else if (!reads && declaredSlot) {
                lints << QStringLiteral(
                             "texture slot %1 is declared but no stage samples %2 (the image is decoded and "
                             "uploaded for nothing)")
                             .arg(slot)
                             .arg(kTextureSamplers.at(slot));
            }
        }
    }

    // ── multipass buffers ──
    // Raw again: fromJson caps the pass count, skips empty entries, clamps
    // bufferScale and fail-closes multipass entirely on a missing buffer.
    const bool declaredMultipass = root.value(QLatin1String("multipass")).toBool(false);
    const bool declaredFeedback = root.value(QLatin1String("bufferFeedback")).toBool(false);
    const QJsonArray declaredBuffers = root.value(QLatin1String("bufferShaders")).toArray();
    if (declaredMultipass) {
        if (declaredBuffers.isEmpty()) {
            lints << QStringLiteral(
                "multipass is true but no bufferShaders are declared (multipass is turned off at "
                "load)");
        }
        if (declaredBuffers.size() > PointerShaderContract::kMaxBufferPasses) {
            lints << QStringLiteral("too many buffer shaders: %1 declared, cap is %2 (surplus dropped at load)")
                         .arg(static_cast<int>(declaredBuffers.size()))
                         .arg(PointerShaderContract::kMaxBufferPasses);
        }
        for (const QJsonValue& v : declaredBuffers) {
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
                lints << QStringLiteral("empty bufferShaders entry (dropped at load, which shifts every later pass)");
                continue;
            }
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined) {
                lints << QStringLiteral(
                             "buffer shader path escapes the pack directory: %1 (multipass is turned off at "
                             "load)")
                             .arg(bufName);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("buffer shader missing: %1 (multipass is turned off at load)").arg(bufName);
            }
        }
        const QJsonValue scaleVal = root.value(QLatin1String("bufferScale"));
        // A non-numeric value (the string "0.5", a plausible slip since the
        // path fields ARE strings) coerces to the 1.0 default at load with no
        // diagnostic anywhere.
        if (!scaleVal.isUndefined() && !scaleVal.isDouble()) {
            lints << QStringLiteral("bufferScale is not a number (falls back to 1.0 at load)");
        }
        const double rawScale = scaleVal.toDouble(1.0);
        if (rawScale < PointerShaderEffect::kMinBufferScale || rawScale > PointerShaderEffect::kMaxBufferScale) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PointerShaderEffect::kMinBufferScale)
                         .arg(PointerShaderEffect::kMaxBufferScale)
                         .arg(rawScale);
        }
        // The channels are the whole point of a multipass pack. A main pass
        // that never samples iChannel<N> pays for every buffer draw and shows
        // none of it, and a `bufferFeedback` pack whose buffer never reads its
        // own channel keeps no state, which is the one thing feedback is for.
        if (anyStage) {
            static const QStringList kChannels = {QString::fromLatin1(PointerShaderContract::kIChannel0),
                                                  QString::fromLatin1(PointerShaderContract::kIChannel1),
                                                  QString::fromLatin1(PointerShaderContract::kIChannel2),
                                                  QString::fromLatin1(PointerShaderContract::kIChannel3)};
            const auto readsAnyChannel = [](const QString& text) {
                return std::any_of(kChannels.cbegin(), kChannels.cend(), [&text](const QString& ch) {
                    return mentionsToken(text, ch);
                });
            };
            if (!readsAnyChannel(fragText)) {
                lints << QStringLiteral(
                    "multipass is true but the main fragment never samples an iChannel (every buffer pass "
                    "is drawn and nothing reads it)");
            }
            if (declaredFeedback && !readsAnyChannel(bufferText)) {
                lints << QStringLiteral(
                    "bufferFeedback is true but no buffer pass samples an iChannel (its previous frame is "
                    "bound and never read, so nothing persists)");
            }
            // The preview's multi-buffer path draws every pass into a single
            // cleared target per frame and keeps a feedback pair only for the
            // single-buffer path, so a two-pass feedback pack persists on the
            // compositor and starts from black in the browser every frame.
            // Counted over the entries that survive the load, which means both
            // filters fromJson applies: an empty entry is dropped there (and
            // linted above), and everything past kMaxBufferPasses is dropped
            // too. Without the cap a pack declaring three non-empty buffers
            // was told it had three passes when only two ever run.
            const auto nonEmpty =
                std::count_if(declaredBuffers.cbegin(), declaredBuffers.cend(), [](const QJsonValue& v) {
                    return !v.toString().isEmpty();
                });
            const auto livePasses = std::min<qsizetype>(nonEmpty, PointerShaderContract::kMaxBufferPasses);
            if (declaredFeedback && livePasses > 1) {
                lints << QStringLiteral(
                             "bufferFeedback with %1 buffer passes persists on the compositor only: the settings "
                             "preview keeps a previous frame for a single buffer pass, so the browser shows this "
                             "pack without its state")
                             .arg(livePasses);
            }
        }
    } else {
        // Both of these are silently dropped by fromJson when multipass is off,
        // so an author who set one and forgot the switch gets no signal at all.
        if (!declaredBuffers.isEmpty()) {
            lints << QStringLiteral("bufferShaders declared without `multipass: true` (ignored at load)");
        }
        if (declaredFeedback) {
            lints << QStringLiteral("bufferFeedback declared without `multipass: true` (ignored at load)");
        }
    }

    if (!QFile::exists(eff.fragmentShaderPath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(fragLabel);
    }
    // An explicit per-pack `vertexShader` was resolved to absolute above; a
    // typo'd path would make the vertex stage below bow out with no diagnostic,
    // exactly as in the surface arm. An empty path is the normal shared
    // pointer.vert case and is not an error.
    if (!eff.vertexShaderPath.isEmpty() && !QFile::exists(eff.vertexShaderPath)) {
        lints << QStringLiteral("vertex shader missing: %1").arg(QFileInfo(eff.vertexShaderPath).fileName());
    }
    // A declared vertex stage that reads the Qt scene-graph members is the
    // shared pointer.vert, or a copy of it, named as the pack's own. Those two
    // identifiers exist only in the preview's UBO branch: on the compositor
    // the stage fails to compile and the pack silently falls back to the
    // built-in vertex source, so the preview and the preview bake both pass
    // while the declared stage is dead where the pack ships. The compositor
    // bake below would fail it too, but with a bare undeclared-identifier
    // error that does not say why.
    if (!vertText.isEmpty()
        && (mentionsToken(vertText, QStringLiteral("qt_Matrix"))
            || mentionsToken(vertText, QStringLiteral("qt_Opacity")))) {
        lints << QStringLiteral(
                     "vertexShader %1 reads qt_Matrix / qt_Opacity, which exist only in the preview's UBO branch: "
                     "shared/pointer.vert is the preview's stage, and a pack naming it (or a copy of it) as its "
                     "own gets a vertex stage that fails to compile on the compositor and is silently replaced "
                     "by the built-in one")
                     .arg(QFileInfo(eff.vertexShaderPath).fileName());
    }

    if (lints.isEmpty()) {
        out << "  metadata       OK\n";
    } else {
        out << "  metadata       ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // The runtime include roots for this pack: {<packRoot>/shared, <packRoot>}
    // plus the installed shared helpers.
    const QStringList includePaths = PointerShaderRegistry::includePathsFor(QDir(packDir).absolutePath());
    const QStringList paramNames = declaredParamNames(eff.parameters);
    // Stages that get no p_<id> preamble cannot see any p_<id>, so the
    // did-you-mean hint would only ever suggest a name they cannot use.
    const QStringList noParams;

    // ── fragment stage, preview dialect (reproduce the runtime assembly) ──
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << padLabel(fragLabel) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
            ++errors;
        } else {
            const QString raw = QString::fromUtf8(frag.readAll());
            QString err;
            // Assemble an entry-only pack (a `vec4 pPointer(vec2 uv)` body, no
            // main()) into a full TU before expansion, identical to both
            // runtimes. A pack that ships its own main() passes through.
            const QString assembled = PhosphorShaders::assembleEntryPoint(
                raw, PointerShaderRegistry::pointerEntryPrologue(), PointerShaderRegistry::pointerEntryCandidates());
            const QString expanded = ShaderCompiler::expandSource(
                assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << padLabel(fragLabel) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, PointerShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, fragLabel, result, paramNames);
            }
        }
    }
    // ── fragment stage, compositor dialect ──
    errors += bakeCompositorStage(out, eff, eff.fragmentShaderPath, fragLabel, QStringLiteral("frag"), includePaths,
                                  /*scaffold=*/true);

    // ── multipass buffer passes ──
    // Buffer passes carry their own main() (no entry scaffold, no param
    // preamble), same as the surface and overlay arms, on both dialects.
    for (const QString& buf : eff.bufferShaderPaths) {
        if (!QFile::exists(buf)) {
            continue; // missing buffers already linted above
        }
        const QString label = QFileInfo(buf).fileName();
        QFile bufFile(buf);
        if (!bufFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << padLabel(label) << "ERROR\n    cannot read " << buf << "\n";
            ++errors;
            continue;
        }
        const QString rawBuf = QString::fromUtf8(bufFile.readAll());
        QString err;
        const QString expanded =
            ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), includePaths, &err);
        if (expanded.isEmpty()) {
            out << "  " << padLabel(label) << "ERROR\n    include expansion failed: " << err << "\n";
            ++errors;
        } else {
            const ShaderCompiler::Result result = ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
            errors += reportCompile(out, label, result, noParams);
        }
        errors += bakeCompositorStage(out, eff, buf, label, QStringLiteral("frag"), includePaths,
                                      /*scaffold=*/false);
    }

    // ── vertex stage ──
    // Mirror the preview runtime: an explicit per-pack `vertexShader` wins,
    // else a `pointer.vert` beside the fragment, else the shared `pointer.vert`
    // from the include paths. It ships its own main(), so no scaffold and no
    // param preamble apply.
    //
    // Only a DECLARED vertex stage is baked on the compositor dialect. The
    // shared pointer.vert is the preview's stage alone: the compositor never
    // compiles it and uses its own TU-local vertex source instead, so baking
    // it there would fail every pack on a stage that never runs there.
    {
        QString vertPath = eff.vertexShaderPath;
        if (vertPath.isEmpty()) {
            const QString vertLocal =
                QFileInfo(eff.fragmentShaderPath).absolutePath() + QStringLiteral("/pointer.vert");
            if (QFile::exists(vertLocal)) {
                vertPath = vertLocal;
            } else {
                for (const QString& incDir : includePaths) {
                    const QString candidate = incDir + QStringLiteral("/pointer.vert");
                    if (QFile::exists(candidate)) {
                        vertPath = candidate;
                        break;
                    }
                }
            }
        }
        if (!vertPath.isEmpty() && QFile::exists(vertPath)) {
            const QString label = QFileInfo(vertPath).fileName();
            QFile vertFile(vertPath);
            if (!vertFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                out << "  " << padLabel(label) << "ERROR\n    cannot read " << vertPath << "\n";
                ++errors;
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                QString err;
                const QString expanded =
                    ShaderCompiler::expandSource(rawVert, QFileInfo(vertPath).absolutePath(), includePaths, &err);
                if (expanded.isEmpty()) {
                    out << "  " << padLabel(label) << "ERROR\n    include expansion failed: " << err << "\n";
                    ++errors;
                } else {
                    const ShaderCompiler::Result result =
                        ShaderCompiler::compile(expanded.toUtf8(), QShader::VertexStage);
                    errors += reportCompile(out, label, result, noParams);
                }
            }
        }
        if (!eff.vertexShaderPath.isEmpty()) {
            errors += bakeCompositorStage(out, eff, eff.vertexShaderPath, QFileInfo(eff.vertexShaderPath).fileName(),
                                          QStringLiteral("vert"), includePaths, /*scaffold=*/false);
        }
    }

    if (errors == 0) {
        out << "  → OK\n\n";
    } else {
        out << "  → " << errors << (errors == 1 ? " error\n\n" : " errors\n\n");
    }
    return errors;
}

} // namespace PlasmaZones::ShaderValidate
