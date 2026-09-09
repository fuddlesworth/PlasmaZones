// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pointer arm of plasmazones-shader-validate — see packvalidators.h.
// Reproduces the pointer runtime's GLSL assembly (PointerShaderEffect + the
// pPointer entry scaffold + the generated p_<id> preamble + include expansion
// over PointerShaderRegistry::includePathsFor) and bakes the fragment stage,
// the buffer passes and the shared vertex stage through headless glslang.
//
// The pointer-specific lints are the fields no sibling family has: the `layer`
// token, `reach` / `reachParam`, and `trailSeconds`. Each of those is silently
// repaired by PointerShaderEffect::fromJson (an unknown layer falls back to
// below, reach and trailSeconds are clamped, a reachParam naming nothing
// numeric is ignored), so every one of them is linted against the RAW metadata
// — a lint over the parsed struct could never surface the author's mistake.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

#include <algorithm>

using PhosphorPointerShaders::PointerShaderEffect;
using PhosphorPointerShaders::PointerShaderRegistry;
using PhosphorRendering::ShaderCompiler;
namespace PointerShaderContract = PhosphorPointerShaders::PointerShaderContract;

namespace PlasmaZones::ShaderValidate {

namespace {

// The `p_<id>` name list a pointer pack declares, for the did-you-mean hint.
// The sibling overloads live in packvalidatorcommon.cpp because more than one
// arm consumes them; this one has a single caller.
QStringList pointerParamNames(const QList<PointerShaderEffect::ParameterInfo>& params)
{
    QStringList declared;
    declared.reserve(params.size());
    for (const PointerShaderEffect::ParameterInfo& p : params) {
        declared << QStringLiteral("p_") + p.id;
    }
    return declared;
}

// The lowest gate value the preview's fastest moment may open to before the
// pack counts as invisible there. Below this a stage that should be showing
// the effect is showing nothing a user could judge.
constexpr double kMinPreviewGate = 0.15;

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
                    out[i] = QLatin1Char(' ');
                    break;
                }
            }
        }
    }
    return out;
}

// Whether `source` reads `p_<id>` as a whole token. A plain `contains` would
// count `p_width` as a use of `p_wid`, and would then stay silent about a
// parameter nothing reads.
bool mentionsParam(const QString& source, const QString& id)
{
    const QString token = QStringLiteral("p_") + id;
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

// The `p_<id>` names a pack passes as the `activationSpeed` argument of
// pointerSpeedGate(). Parsed rather than regexed because the first argument is
// routinely a call of its own (`pointerSpeedGate(length(v), p_speed)`), so the
// split has to happen at the top-level comma.
QStringList speedGateParamNames(const QString& rawSource)
{
    const QString source = withoutComments(rawSource);
    const QLatin1String callee("pointerSpeedGate");
    QStringList found;
    int at = 0;
    while ((at = source.indexOf(callee, at)) >= 0) {
        int i = at + callee.size();
        while (i < source.size() && source[i].isSpace()) {
            ++i;
        }
        if (i >= source.size() || source[i] != QLatin1Char('(')) {
            at += callee.size();
            continue;
        }
        // Walk the argument list, remembering where the top-level comma fell.
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
        if (comma > 0 && j < source.size()) {
            const QString arg = source.mid(comma + 1, j - comma - 1).trimmed();
            if (arg.startsWith(QLatin1String("p_"))) {
                const QString id = arg.mid(2);
                if (PhosphorShaders::isValidParamId(id) && !found.contains(id)) {
                    found << id;
                }
            }
        }
        at = j > at ? j : at + callee.size();
    }
    return found;
}

} // namespace

// Validate one POINTER pack directory (data/pointer/*). Reproduces the pointer
// runtime's fragment assembly on the preview (Qt-RHI) path — the pPointer entry
// scaffold (an entry-only pack gets a generated main(); a pack with its own
// main() passes through unchanged) + include expansion + the generated p_<id>
// preamble — then bakes through headless glslang. Returns the error count.
//
// As with the animation and surface arms, the kwin-effect classic-GL branch
// (`#define PLASMAZONES_KWIN`, default-block uniforms) is NOT baked here:
// QShaderBaker compiles Vulkan-dialect GLSL and rejects default-block uniforms.
// Baking the #else branch validates the UBO contract in pointer_uniforms.glsl;
// the PLASMAZONES_KWIN plumbing is identical for every pack and exercised by
// the live compositor compile.
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
        } else if (declared->maxValue.isValid() && declared->maxValue.toDouble() > PointerShaderEffect::kMaxReach) {
            lints << QStringLiteral(
                         "reachParam '%1' allows up to %2, past the %3 reach cap (clamped at load, so the "
                         "pack paints outside its damage rect)")
                         .arg(eff.reachParam)
                         .arg(declared->maxValue.toDouble())
                         .arg(PointerShaderEffect::kMaxReach);
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

    // ── declared but never used ──
    // A parameter the shader never reads still costs a slot and still draws a
    // control in the settings app, so the user is handed a slider that moves
    // and changes nothing. Nothing repairs this at load, which is why it needs
    // saying here: the pack works, and the control is dead.
    //
    // Every stage is scanned, not just the fragment, since a parameter may
    // legitimately be read only by a buffer pass or the vertex stage.
    {
        QStringList stagePaths{eff.fragmentShaderPath};
        if (!eff.vertexShaderPath.isEmpty()) {
            stagePaths << eff.vertexShaderPath;
        }
        for (const QString& buf : eff.bufferShaderPaths) {
            stagePaths << buf;
        }
        QString allStages;
        for (const QString& path : stagePaths) {
            QFile f(path);
            if (QFile::exists(path) && f.open(QIODevice::ReadOnly)) {
                allStages += QString::fromUtf8(f.readAll());
                allStages += QLatin1Char('\n');
            }
        }
        if (!allStages.isEmpty()) {
            const QString stripped = withoutComments(allStages);
            for (const PointerShaderEffect::ParameterInfo& p : eff.parameters) {
                if (!PhosphorShaders::isValidParamId(p.id)) {
                    continue; // already linted above, and it has no p_ define
                }
                if (!mentionsParam(stripped, p.id)) {
                    lints << QStringLiteral(
                                 "parameter '%1' is declared but no stage reads p_%1 (it still takes a "
                                 "slot and still draws a control that does nothing)")
                                 .arg(p.id);
                }
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
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile gateFrag(eff.fragmentShaderPath);
        if (gateFrag.open(QIODevice::ReadOnly)) {
            const QStringList gateIds = speedGateParamNames(QString::fromUtf8(gateFrag.readAll()));
            for (const QString& gateId : gateIds) {
                const auto declared = std::find_if(eff.parameters.cbegin(), eff.parameters.cend(),
                                                   [&gateId](const PointerShaderEffect::ParameterInfo& p) {
                                                       return p.id == gateId;
                                                   });
                if (declared == eff.parameters.cend()) {
                    lints << QStringLiteral(
                                 "pointerSpeedGate() is passed p_%1, which is no declared parameter (it "
                                 "expands to an unwritten slot, so the gate reads whatever is in it)")
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

    // ── multipass buffers ──
    // Raw again: fromJson caps the pass count, skips empty entries, clamps
    // bufferScale and fail-closes multipass entirely on a missing buffer.
    const bool declaredMultipass = root.value(QLatin1String("multipass")).toBool(false);
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
        const double rawScale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
        if (rawScale < PointerShaderEffect::kMinBufferScale || rawScale > PointerShaderEffect::kMaxBufferScale) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PointerShaderEffect::kMinBufferScale)
                         .arg(PointerShaderEffect::kMaxBufferScale)
                         .arg(rawScale);
        }
    } else {
        // Both of these are silently dropped by fromJson when multipass is off,
        // so an author who set one and forgot the switch gets no signal at all.
        if (!declaredBuffers.isEmpty()) {
            lints << QStringLiteral("bufferShaders declared without `multipass: true` (ignored at load)");
        }
        if (root.value(QLatin1String("bufferFeedback")).toBool(false)) {
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

    if (lints.isEmpty()) {
        out << "  metadata       OK\n";
    } else {
        out << "  metadata       ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // The runtime include roots for this pack: {<packRoot>/shared, <packRoot>}.
    const QStringList includePaths = PointerShaderRegistry::includePathsFor(QDir(packDir).absolutePath());
    const QStringList paramNames = pointerParamNames(eff.parameters);

    // ── fragment stage (reproduce the runtime assembly) ──
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << fragLabel.leftJustified(15) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
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
                out << "  " << fragLabel.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, PointerShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, fragLabel, result, paramNames);
            }
        }
    }

    // ── multipass buffer passes ──
    // Buffer passes carry their own main() (no entry scaffold, no param
    // preamble), same as the surface and overlay arms.
    for (const QString& buf : eff.bufferShaderPaths) {
        if (!QFile::exists(buf)) {
            continue; // missing buffers already linted above
        }
        const QString label = QFileInfo(buf).fileName();
        QFile bufFile(buf);
        if (!bufFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << label.leftJustified(15) << "ERROR\n    cannot read " << buf << "\n";
            ++errors;
            continue;
        }
        const QString rawBuf = QString::fromUtf8(bufFile.readAll());
        QString err;
        const QString expanded =
            ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), includePaths, &err);
        if (expanded.isEmpty()) {
            out << "  " << label.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
            ++errors;
        } else {
            const ShaderCompiler::Result result = ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
            errors += reportCompile(out, label, result, paramNames);
        }
    }

    // ── vertex stage ──
    // Mirror the runtime: an explicit per-pack `vertexShader` wins, else a
    // `pointer.vert` beside the fragment, else the shared `pointer.vert` from
    // the include paths. It ships its own main(), so no scaffold and no param
    // preamble apply.
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
                out << "  " << label.leftJustified(15) << "ERROR\n    cannot read " << vertPath << "\n";
                ++errors;
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                QString err;
                const QString expanded =
                    ShaderCompiler::expandSource(rawVert, QFileInfo(vertPath).absolutePath(), includePaths, &err);
                if (expanded.isEmpty()) {
                    out << "  " << label.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
                    ++errors;
                } else {
                    const ShaderCompiler::Result result =
                        ShaderCompiler::compile(expanded.toUtf8(), QShader::VertexStage);
                    errors += reportCompile(out, label, result, paramNames);
                }
            }
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
