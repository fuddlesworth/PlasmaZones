// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The surface/decoration arm of plasmazones-shader-validate — see
// packvalidators.h. Reproduces the surface runtime's GLSL assembly
// (SurfaceShaderEffect + generated p_<id> preamble + include expansion) and
// bakes the fragment stage, the buffer passes and the shared vertex stage
// through headless glslang.
//
// The one arm whose buffer entries are not all filenames: a `builtin:` token
// resolves through SurfaceShaderRegistry to a shared pass, so the path lints
// here check that resolution instead of confining a pack-relative path.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

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

#include <algorithm>

#include <rhi/qshader.h>

using PhosphorRendering::ShaderCompiler;
using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace PlasmaZones::ShaderValidate {

namespace {

/// The declared-name list for a stage that takes NO generated preamble. Passed to
/// reportCompile so its did-you-mean hint stays silent rather than suggesting a
/// p_<id> the stage could not have referenced.
const QStringList kNoDeclaredParams;

// Bake one stage on the COMPOSITOR dialect (`#define PLASMAZONES_KWIN`,
// default-block uniforms), which QShaderBaker cannot compile — it wants
// Vulkan-dialect GLSL — so this shells out to glslang the way the animation and
// pointer arms do.
//
// Modelled on the POINTER arm rather than the animation one: the animation helper
// carries a finalizeColor argument because its per-window path splices KWin's
// colour-management block, and the surface compositor path splices no such block
// (surface_compile.cpp assembles the entry, expands includes, splices the param
// preamble and injects the KWin define, and nothing else).
//
// Expansion goes through ShaderIncludeResolver, NOT ShaderCompiler::expandSource,
// because that is what the compositor uses and the two differ: expandSource also
// searches the shader's own directory for an angle include, so a pack-local angle
// include resolves on the daemon and fails where it ships.
int bakeCompositorStage(QTextStream& out, const SurfaceShaderEffect& eff, const QString& path, const QString& label,
                        const QString& stage, const QStringList& includePaths, bool scaffold)
{
    if (!QFile::exists(path)) {
        return 0; // an absent stage is already linted by the caller
    }
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        // Hard failure rather than a skip, for the reason the two sibling arms
        // give: a pack cannot reach a release with the branch it ships on
        // uncompiled, and a quiet degrade is how that happened before. Explained
        // once per run; every stage still counts the error.
        static bool explained = false;
        out << "  " << padLabel(label) << "ERROR (compositor)\n";
        if (!explained) {
            explained = true;
            out << "    neither glslangValidator nor glslang found on PATH. One of them is required to "
                   "compile the compositor dialect every surface pack ships on (install the glslang package)\n";
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
        ? PhosphorShaders::assembleEntryPoint(raw, SurfaceShaderRegistry::surfaceEntryPrologue(),
                                              SurfaceShaderRegistry::surfaceEntryCandidates())
        : raw;
    QString err;
    QStringList sourcePaths;
    QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembled, QFileInfo(path).absolutePath(),
                                                                         includePaths, &err, nullptr, &sourcePaths);
    if (src.isEmpty()) {
        out << "  " << padLabel(label) << "ERROR (compositor)\n    include expansion failed the way the compositor "
            << "expands it: " << err
            << "\n    (if the file sits beside this one, note that the compositor resolves an angle include only "
            << "against the registry roots; the quoted form is the pack-local one)\n";
        return 1;
    }
    if (scaffold) {
        // Buffer passes compile WITHOUT the generated preamble and read their
        // parameters by raw contract slot, so splicing one here would let a buffer
        // reference a p_<id> that fails on both paths that ship.
        src = PhosphorShaders::spliceAfterVersion(src, SurfaceShaderRegistry::paramPreamble(eff));
    }
    // LAST, so the define block lands above the preamble, which is what
    // injectKwinDefineAfterVersion produces at runtime.
    src = PhosphorShaders::spliceAfterVersion(src, PhosphorShaders::kwinDefineBlock());
    return reportCompositorCompile(out, label, stage, src, tool, sourcePaths);
}

} // namespace

// Validate one SURFACE pack directory (data/surface/*). Reproduces the surface
// runtime's fragment assembly — the pSurface entry scaffold (an entry-only pack
// gets a generated main(); a pack with its own main() passes through unchanged) +
// include expansion + the generated p_<id> preamble — and bakes BOTH branches
// every surface pack ships on. Returns the error count.
//
// The daemon (Qt-RHI) branch bakes through QShaderBaker, which validates the UBO
// contract in surface_uniforms.glsl. The kwin-effect classic-GL branch
// (`#define PLASMAZONES_KWIN`, default-block uniforms) is baked separately through
// glslang, because QShaderBaker wants Vulkan-dialect GLSL and rejects default-block
// uniforms. Both are required: a contract error on the compositor branch used to
// ship and surface as a black decoration, since the compositor swallows a compile
// failure.
//
// COVERAGE BOUNDARY, as on the animation arm. The compositor recompiles `#version
// 450` down to its context core version at load, so a construct this gate accepts
// at 450 can still fail there. And glslang is not the compositor's own driver
// compiler, so a driver-specific rejection is out of reach either way.
int validateSurfacePack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    invalid JSON: " << perr.errorString() << "\n  → 1 error\n\n";
        return 1;
    }

    SurfaceShaderEffect eff = SurfaceShaderEffect::fromJson(doc.object());
    eff.sourceDir = QDir(packDir).absolutePath();
    // The `fragmentShader` / `bufferShaders` / `vertexShader` paths come from the
    // user-editable metadata.json, so confine each to the pack dir (via
    // confinedPackPath) before it is opened and fed to glslang: a
    // `../../../etc/...` or absolute path must be rejected, not compiled. An
    // empty path is left as-is (that stage is simply absent).
    const auto confineToPack = [&packDir](QString& path) {
        return confinePackPathInPlace(packDir, path);
    };
    if (!confineToPack(eff.fragmentShaderPath)) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    fragmentShader path escapes the pack directory (path traversal "
               "rejected)\n  → 1 error\n\n";
        return 1;
    }
    // Every offender named, not just the first. The fragment and vertex exits
    // above each cover ONE key, so naming it adds nothing; bufferShaders is a
    // list, and bailing on the first escape printed neither the path nor the
    // index while suppressing the whole rest of the report. An author with two
    // bad entries then fixed one and got the same anonymous line again. The lint
    // further down that DOES name a path re-derives confinement over the RAW
    // array, so it can only ever fire for an entry past the pass cap, which
    // fromJson already dropped and which therefore never reaches this loop.
    QStringList escapingBuffers;
    for (qsizetype i = 0; i < eff.bufferShaderPaths.size(); ++i) {
        QString& b = eff.bufferShaderPaths[i];
        // `builtin:` tokens resolve against the surface shared/ dir (fixed
        // whitelist, same resolver as the runtime registry) rather than the
        // pack dir; an unknown token resolves empty and is linted below as a
        // missing buffer, mirroring the runtime's fail-closed path.
        if (SurfaceShaderRegistry::isBuiltinBufferShader(b)) {
            b = SurfaceShaderRegistry::resolveBuiltinBufferShader(b, QDir(packDir).absolutePath());
            continue;
        }
        const QString declared = b;
        if (!confineToPack(b)) {
            escapingBuffers << QStringLiteral("bufferShaders[%1]: %2").arg(QString::number(i), declared);
        }
    }
    if (!escapingBuffers.isEmpty()) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    bufferShaders path escapes the pack directory (path traversal rejected)\n";
        for (const QString& e : escapingBuffers) {
            out << "      " << e << "\n";
        }
        out << "  → " << escapingBuffers.size() << (escapingBuffers.size() == 1 ? " error" : " errors") << "\n\n";
        return static_cast<int>(escapingBuffers.size());
    }
    if (!confineToPack(eff.vertexShaderPath)) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    vertexShader path escapes the pack directory (path traversal rejected)\n "
               " "
               "→ 1 error\n\n";
        return 1;
    }
    if (!eff.isValid()) {
        out << name << "\n  " << padLabel(QStringLiteral("metadata"))
            << "ERROR\n    missing required field (id / fragmentShader)\n  → 1 error\n\n";
        return 1;
    }
    const QString fragLabel = QFileInfo(eff.fragmentShaderPath).fileName();

    // RAW counts, not the parsed sizes. fromJson silently drops what it cannot
    // use (a malformed entry, anything past a cap), so the parsed size is what
    // SURVIVED rather than what the author WROTE, while every lint below counts
    // the raw array. The header therefore used to contradict the lint three
    // lines under it: "3 textures" followed by "too many textures: 5 declared".
    // The author's own file says 5, so 5 is the number the header owes them.
    //
    // A non-array value counts 0, which is honest: toArray() gives an empty
    // array and there is nothing the author declared AS a parameter list. The
    // separate type lint is what reports the wrong shape.
    const qsizetype rawParamCount = doc.object().value(QLatin1String("parameters")).toArray().size();
    const qsizetype rawTextureCount = doc.object().value(QLatin1String("textures")).toArray().size();
    out << name << "  (" << rawParamCount << " param" << (rawParamCount == 1 ? "" : "s") << ", " << rawTextureCount
        << " texture" << (rawTextureCount == 1 ? "" : "s") << ", " << (eff.isMultipass ? "multipass" : "single-pass")
        << ")\n";

    int errors = 0;

    // ── metadata lints ──
    static const QStringList kSurfaceParamTypes = {QStringLiteral("float"), QStringLiteral("int"),
                                                   QStringLiteral("bool"), QStringLiteral("color")};
    QStringList lints;
    // Same blind spot as `textures` below, and quieter, because a pack with no
    // parameters is legitimate: a `parameters` of the wrong shape loads as zero
    // parameters, every per-parameter lint iterates nothing, and the pack passes
    // with its entire control set discarded.
    const QJsonValue parametersValue = doc.object().value(QLatin1String("parameters"));
    if (!parametersValue.isUndefined() && !parametersValue.isNull() && !parametersValue.isArray()) {
        lints << QStringLiteral("`parameters` is not an array (every parameter is ignored at load)");
    }
    for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
        if (!kSurfaceParamTypes.contains(p.type)) {
            lints << QStringLiteral("unknown param type '%1' for '%2' (surface params are float/int/bool/color)")
                         .arg(p.type, p.id);
        }
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
        }
    }
    // Slot budget, mirroring the animation arm. translateSurfaceParams drops
    // every scalar past kMaxParameterSlots and every colour past
    // kMaxCustomColors at load with a journal warning, and buildParamPreamble
    // emits no p_<id> for them, so a pack that READS such a parameter fails its
    // bake with a bare undeclared-identifier error the did-you-mean hint cannot
    // explain (the name IS declared), and one that does not read it ships green.
    {
        int scalarParams = 0;
        int colorParams = 0;
        for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
            if (p.type == QLatin1String("color")) {
                ++colorParams;
            } else {
                // float / int / bool all take a scalar sub-slot.
                ++scalarParams;
            }
        }
        const int scalarBudget = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxParameterSlots;
        const int colorBudget = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors;
        if (scalarParams > scalarBudget) {
            lints << QStringLiteral(
                         "too many scalar params: %1 declared, budget is %2 (the surplus get no p_<id> "
                         "and are dropped at load)")
                         .arg(QString::number(scalarParams), QString::number(scalarBudget));
        }
        if (colorParams > colorBudget) {
            lints << QStringLiteral(
                         "too many color params: %1 declared, budget is %2 (the surplus get no p_<id> "
                         "and are dropped at load)")
                         .arg(QString::number(colorParams), QString::number(colorBudget));
        }
    }
    // A declared default / min / max is never checked against the parameter's own
    // type, in fromJson or here, so `"type": "float", "default": "wide"` ships
    // green and renders 0.0 because the conversion fails silently. Same for a
    // default outside the min/max the pack itself declares, which the UI then
    // clamps to something the author never chose.
    for (const QJsonValue& v : doc.object().value(QLatin1String("parameters")).toArray()) {
        const QJsonObject po = v.toObject();
        const QString pid = po.value(QLatin1String("id")).toString();
        const QString ptype = po.value(QLatin1String("type")).toString();
        if (pid.isEmpty() || ptype.isEmpty()) {
            continue; // already linted above
        }
        const QJsonValue def = po.value(QLatin1String("default"));
        if (ptype == QLatin1String("bool")) {
            if (!def.isUndefined() && !def.isBool()) {
                lints << QStringLiteral("parameter '%1' is bool but its default is not true or false").arg(pid);
            }
            continue;
        }
        if (ptype == QLatin1String("color") || ptype == QLatin1String("image")) {
            if (!def.isUndefined() && !def.isString()) {
                lints << QStringLiteral("parameter '%1' is %2 but its default is not a string").arg(pid, ptype);
            }
            continue;
        }
        // float / int from here.
        if (!def.isUndefined() && !def.isDouble()) {
            lints << QStringLiteral("parameter '%1' is %2 but its default is not a number").arg(pid, ptype);
            continue;
        }
        const QJsonValue lo = po.value(QLatin1String("min"));
        const QJsonValue hi = po.value(QLatin1String("max"));
        if (!lo.isUndefined() && !lo.isDouble()) {
            lints << QStringLiteral("parameter '%1' has a non-numeric min").arg(pid);
        }
        if (!hi.isUndefined() && !hi.isDouble()) {
            lints << QStringLiteral("parameter '%1' has a non-numeric max").arg(pid);
        }
        if (lo.isDouble() && hi.isDouble() && lo.toDouble() > hi.toDouble()) {
            lints << QStringLiteral("parameter '%1' has min %2 above max %3")
                         .arg(pid)
                         .arg(lo.toDouble())
                         .arg(hi.toDouble());
        }
        if (def.isDouble() && lo.isDouble() && hi.isDouble()
            && (def.toDouble() < lo.toDouble() || def.toDouble() > hi.toDouble())) {
            lints << QStringLiteral("parameter '%1' default %2 is outside its own declared range [%3, %4]")
                         .arg(pid)
                         .arg(def.toDouble())
                         .arg(lo.toDouble())
                         .arg(hi.toDouble());
        }
    }
    // Duplicate ids are linted over the RAW array rather than eff.parameters,
    // because fromJson drops the second declaration with only a qCWarning. A
    // pack that declares one id twice therefore lints clean against the parsed
    // struct and ships with one of the two silently gone. The overlay and
    // pointer arms already walk the raw array for this; packvalidatorcommon
    // states that every arm does, so this one was the exception.
    {
        const QJsonArray rawParams = doc.object().value(QLatin1String("parameters")).toArray();
        QSet<QString> seenParamIds;
        for (const QJsonValue& v : rawParams) {
            const QString pid = v.toObject().value(QLatin1String("id")).toString();
            if (pid.isEmpty()) {
                continue;
            }
            if (seenParamIds.contains(pid)) {
                lints << QStringLiteral("duplicate parameter id '%1' (only the first declaration survives load)")
                             .arg(pid);
            } else {
                seenParamIds.insert(pid);
            }
        }
    }
    // A `textures` that is not an array at all used to lint completely clean:
    // toArray() answers empty for a string, a number or an object, the loop
    // below never runs, and the pack passed with its texture list silently
    // ignored. Say so rather than validating a list the author did not write.
    const QJsonValue texturesValue = doc.object().value(QLatin1String("textures"));
    if (!texturesValue.isUndefined() && !texturesValue.isNull() && !texturesValue.isArray()) {
        lints << QStringLiteral("`textures` is not an array (the whole list is ignored at load)");
    }
    const QJsonArray declaredTextures = texturesValue.toArray();
    if (declaredTextures.size() > PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        // A non-object entry (a bare path string, the natural mistake) reported
        // as "empty `path`", which describes an object that has the key and left
        // it blank. The author wrote no object at all, so they went looking for
        // a key that is not in their file.
        if (!v.isObject()) {
            lints << QStringLiteral(
                "texture entry is not an object (dropped at load, which also shifts every later "
                "texture down one sampler slot). An entry is `{\"path\": \"...\"}`, not a bare path");
            continue;
        }
        const QString texPath = v.toObject().value(QLatin1String("path")).toString();
        if (texPath.isEmpty()) {
            lints << QStringLiteral(
                "texture entry with empty `path` (dropped at load, which also shifts "
                "every later texture down one sampler slot)");
        } else {
            // Same confinement and existence check the animation arm applies,
            // and for the same reason: the registry clears a rejected texture
            // path and the sampler falls back to transparent, so a typo ships
            // green and fails at first paint.
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
        // Wrap vocabulary lint — read RAW metadata: SurfaceShaderEffect::fromJson
        // silently clears an invalid wrap to clamp, so a lint over the parsed
        // eff.textures could never surface an author's typo. Mirror fromJson's
        // {clamp,repeat,mirror} guard so a bad wrap fails the validator instead.
        const QString wrap = v.toObject().value(QLatin1String("wrap")).toString();
        if (!wrap.isEmpty() && !PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(wrap)) {
            lints
                << QStringLiteral("texture wrap not in {clamp,repeat,mirror}: %1 (cleared to clamp at load)").arg(wrap);
        }
    }
    // The multipass lints below are gated on the separate "multipass" key, and so
    // is the RUNTIME: the registry clears every buffer pass of a pack that
    // declares bufferShaders without it. Such a pack would otherwise validate
    // here as a clean single-pass pack and then render with no chain at all,
    // which is the loudest possible difference between what the validator says
    // and what the user sees. The converse (multipass true, no bufferShaders)
    // already fails closed further down.
    // Boolean pack keys are read with toBool(default), which answers the DEFAULT
    // for anything that is not a bool rather than complaining. So
    // `"halfFloatBuffers": "false"` loads as TRUE, the exact opposite of what the
    // author wrote, and `"multipass": 1` leaves the pack single-pass. The JSON
    // schema catches this for the bundled packs only; a user pack never meets it.
    {
        static const QStringList kBoolKeys = {
            QStringLiteral("multipass"),        QStringLiteral("needsBackdrop"),
            QStringLiteral("animated"),         QStringLiteral("audio"),
            QStringLiteral("bufferFeedback"),   QStringLiteral("depthBuffer"),
            QStringLiteral("halfFloatBuffers"), QStringLiteral("interiorOpaque"),
            QStringLiteral("providesBorder"),   QStringLiteral("providesOpacityTint")};
        for (const QString& k : kBoolKeys) {
            const QJsonValue v = doc.object().value(k);
            if (!v.isUndefined() && !v.isNull() && !v.isBool()) {
                lints << QStringLiteral(
                             "\"%1\" must be true or false; any other value is ignored and the default "
                             "is used instead")
                             .arg(k);
            }
        }
    }
    // paddingParam names the parameter whose value becomes the pack's outer
    // padding request. paddingRequest answers 0 for a name that resolves to no
    // numeric parameter, so a typo does not fail anything: the pack simply asks
    // for no margin and clips at the frame edge, which looks like a shader bug.
    {
        const QString paddingParam = doc.object().value(QLatin1String("paddingParam")).toString();
        if (!paddingParam.isEmpty()) {
            bool resolves = false;
            for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
                if (p.id == paddingParam && (p.type == QLatin1String("float") || p.type == QLatin1String("int"))) {
                    resolves = true;
                    // The host bounds the request into [0, kMaxDecorationOuterPaddingPx], so
                    // a max above that ceiling has a dead top end. All bundled packs comply.
                    bool okMax = false;
                    const double declaredMax = p.maxValue.toDouble(&okMax);
                    if (okMax
                        && declaredMax > static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx)) {
                        lints << QStringLiteral(
                                     "paddingParam '%1' declares a max of %2, above the host's "
                                     "%3 px ceiling, so the top of its range is unreachable")
                                     .arg(paddingParam)
                                     .arg(declaredMax)
                                     .arg(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx);
                    }
                    break;
                }
            }
            if (!resolves) {
                lints << QStringLiteral(
                             "paddingParam '%1' names no declared float or int parameter, so the pack "
                             "requests no padding and clips at the frame edge")
                             .arg(paddingParam);
            }
        }
    }
    // preview is the pack's thumbnail. The registry clears one that escapes the
    // pack directory with a journal warning only, and accepts a name whose file
    // does not exist, so either mistake ships green and shows up as a pack with
    // no thumbnail. Same shape as the texture branch above.
    {
        const QString preview = doc.object().value(QLatin1String("preview")).toString();
        if (!preview.isEmpty()) {
            const auto confined = confinedPackPath(packDir, preview);
            if (!confined) {
                lints << QStringLiteral(
                             "preview path escapes the pack directory: %1 (cleared at load, so the pack "
                             "shows no thumbnail)")
                             .arg(preview);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("preview missing: %1 (the pack shows no thumbnail)").arg(preview);
            }
        }
    }

    const QJsonArray rawBufferShaders = doc.object().value(QLatin1String("bufferShaders")).toArray();
    // The buffer keys are read only inside the multipass branch, so on a
    // single-pass pack they are inert. Declaring them reads as a pack that thinks
    // it is multipass, which is the same authoring mistake the gate below names
    // from the other side.
    if (!eff.isMultipass) {
        for (const QLatin1String key :
             {QLatin1String("bufferScales"), QLatin1String("bufferWraps"), QLatin1String("bufferFilters")}) {
            if (!doc.object().value(key).toArray().isEmpty()) {
                lints
                    << QStringLiteral("%1 is declared on a single-pass pack, where it is never read").arg(QString(key));
            }
        }
    }
    if (!rawBufferShaders.isEmpty() && !eff.isMultipass) {
        lints << QStringLiteral(
                     "bufferShaders declares %1 pass(es) but \"multipass\" is not true, so every one of "
                     "them is dropped at load and the pack renders single-pass")
                     .arg(static_cast<int>(rawBufferShaders.size()));
    }

    // Multipass buffer lints — read RAW metadata, not the parsed struct: fromJson
    // clamps bufferScale into [kMinBufferScale, 1.0] and drops missing buffers, so
    // a lint over the parsed values would hide author errors.
    if (eff.isMultipass) {
        const QJsonArray declaredBuffers = doc.object().value(QLatin1String("bufferShaders")).toArray();
        // The inverse of the gate above. With no usable bufferShaders every
        // buffer lint below and the whole buffer bake become no-ops while the
        // header still prints "multipass", and the registry quietly normalises
        // the pack back to single-pass with no warning of its own.
        if (declaredBuffers.isEmpty()) {
            lints << QStringLiteral(
                "\"multipass\" is true but bufferShaders is missing, empty or not an array, so "
                "the pack is normalised back to single-pass at load");
        }
        // A key handed something other than an array is ignored ENTIRELY at load,
        // because QJsonValue::toArray() answers an empty array for any non-array
        // value. So `"bufferScales": 0.5` or `"bufferWraps": "clamp"` ships green
        // with the whole list silently dropped.
        const auto lintIsArray = [&](QLatin1String key) {
            const QJsonValue v = doc.object().value(key);
            if (!v.isUndefined() && !v.isNull() && !v.isArray()) {
                lints << QStringLiteral("%1 must be an array; any other value is ignored entirely at load")
                             .arg(QString(key));
            }
        };
        lintIsArray(QLatin1String("bufferShaders"));
        lintIsArray(QLatin1String("bufferWraps"));
        lintIsArray(QLatin1String("bufferFilters"));
        lintIsArray(QLatin1String("bufferScales"));
        // The builtin Kawase pyramid is POSITIONAL, not a set: each pass is bound
        // to iChannel<j> by its INDEX, and the seven frags hardcode which channel
        // they read, so the chain composes in exactly one order. Reordered or
        // short, every individual token still resolves and every file still
        // compiles, so nothing else here would notice; the pack simply blurs
        // wrongly. Require the whole sequence as soon as any of it appears.
        static const QStringList kKawaseChain = {
            QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
            QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
            QStringLiteral("builtin:kawase-up-0"),   QStringLiteral("builtin:kawase-up-1"),
            QStringLiteral("builtin:kawase-up-2")};
        // A depth pack pins every pass to the single bufferScale on the daemon,
        // because the passes share one depth attachment and a render target's
        // colour and depth attachments must agree in size. That runtime
        // behaviour is correct and documented in place; what was missing is the
        // diagnostic, so a pack declaring both shipped green with its whole
        // pyramid flattened at load.
        if (doc.object().value(QLatin1String("depthBuffer")).toBool()
            && !doc.object().value(QLatin1String("bufferScales")).toArray().isEmpty()) {
            lints << QStringLiteral(
                "bufferScales is declared alongside \"depthBuffer\": true, and every entry is discarded at load "
                "(the passes share one depth attachment, so they all render at bufferScale)");
        }
        bool anyKawase = false;
        for (const QJsonValue& v : declaredBuffers) {
            if (kKawaseChain.contains(v.toString())) {
                anyKawase = true;
                break;
            }
        }
        if (anyKawase) {
            bool chainOk = declaredBuffers.size() >= kKawaseChain.size();
            for (int i = 0; chainOk && i < kKawaseChain.size(); ++i) {
                chainOk = declaredBuffers.at(i).toString() == kKawaseChain.at(i);
            }
            if (!chainOk) {
                lints << QStringLiteral(
                             "the builtin Kawase passes are positional and must appear as bufferShaders"
                             "[0..6] in the order %1")
                             .arg(kKawaseChain.join(QLatin1String(", ")));
            }
            // THE RADIUS SLOT. kawase_down_0 and every pass after it read the
            // blur radius as customParams[0].x, and slots are assigned by
            // DECLARATION ORDER (buildParamPreamble), so the chain blurs by
            // whatever the pack's first scalar parameter happens to be. Nothing
            // enforced that: surface_blur.glsl's own header calls it out as "the
            // part a pack author has to carry" and says no validator lint and no
            // test covers it. Reorder the parameters array and the pack still
            // compiles, still loads, and blurs by a corner radius.
            //
            // The first SCALAR, not the first parameter: colours and images live
            // in their own pools, so a pack may lead with a colour and still have
            // its radius in slot 0.
            {
                QString firstScalarId;
                QString firstScalarType;
                for (const QJsonValue& pv : parametersValue.toArray()) {
                    const QString ptype = pv.toObject().value(QLatin1String("type")).toString();
                    if (ptype == QLatin1String("float") || ptype == QLatin1String("int")) {
                        firstScalarId = pv.toObject().value(QLatin1String("id")).toString();
                        firstScalarType = ptype;
                        break;
                    }
                }
                if (firstScalarType.isEmpty()) {
                    lints << QStringLiteral(
                        "the builtin Kawase passes read the blur radius as customParams[0].x, but this pack declares "
                        "no float or int parameter, so the chain blurs by 0");
                } else if (firstScalarId != QLatin1String("blurRadius")) {
                    // A name check, deliberately, and the message says why rather
                    // than pretending the name itself is load-bearing: the SLOT is
                    // what matters and the name is the only thing decidable here.
                    lints << QStringLiteral(
                                 "the builtin Kawase passes read the blur radius as customParams[0].x, which is the "
                                 "FIRST scalar parameter declared, and that is '%1' here. Every bundled chain pack "
                                 "declares 'blurRadius' first; if '%1' is not the radius the chain blurs by the wrong "
                                 "control")
                                 .arg(firstScalarId);
                }
            }
            // THE BASE SCALE. surfaceKawaseDownBackdrop cannot size its taps from
            // textureSize(), because it reads the backdrop capture whose size is
            // not the canvas's, so it derives them from a HARDCODED
            // kSurfaceKawaseBaseTexel of 4 canvas px per texel. That constant IS
            // the 0.25 base, so a first scale of anything else silently mis-spaces
            // the one pass that cannot detect it.
            const QJsonArray kawaseScales = doc.object().value(QLatin1String("bufferScales")).toArray();
            static const QList<double> kKawaseScales = {0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25};
            if (kawaseScales.isEmpty()) {
                lints << QStringLiteral(
                             "the builtin Kawase chain needs per-pass bufferScales %1; with none declared every pass "
                             "renders at the pack-wide bufferScale and the pyramid is not a pyramid")
                             .arg(QStringLiteral("[0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25]"));
            } else if (!kawaseScales.isEmpty() && !qFuzzyCompare(kawaseScales.at(0).toDouble(), kKawaseScales.at(0))) {
                lints << QStringLiteral(
                             "bufferScales[0] is %1, but the first Kawase DOWN pass derives its tap spacing from a "
                             "hardcoded 4 canvas px per texel, which is the 0.25 base. Any other first scale "
                             "mis-spaces that pass and nothing at runtime can detect it")
                             .arg(kawaseScales.at(0).toDouble());
            } else if (chainOk && kawaseScales.size() >= kKawaseScales.size()) {
                // Only when the chain itself is the canonical seven: a pack that
                // failed chainOk above is already being told the bigger thing, and
                // comparing scales against a chain it does not have would be noise.
                for (qsizetype i = 0; i < kKawaseScales.size(); ++i) {
                    if (!qFuzzyCompare(kawaseScales.at(i).toDouble(), kKawaseScales.at(i))) {
                        lints << QStringLiteral(
                                     "bufferScales[%1] is %2 where the Kawase pyramid halves to %3; the up passes read "
                                     "the level below by channel index, so an off-pyramid scale composes a level "
                                     "against the wrong resolution")
                                     .arg(i)
                                     .arg(kawaseScales.at(i).toDouble())
                                     .arg(kKawaseScales.at(i));
                        break;
                    }
                }
            }
        }
        for (const QJsonValue& v : declaredBuffers) {
            // A non-string entry reported as "empty", which says the author wrote
            // "" when they wrote an object or a number. It costs the same thing,
            // so the consequence below is repeated rather than softened.
            if (!v.isString()) {
                lints << QStringLiteral(
                    "bufferShaders entry is not a string (kept in place as empty, but it fails the "
                    "scan-time existence check and drops the WHOLE pack to single-pass)");
                continue;
            }
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
                // fromJson appends an empty entry IN PLACE rather than skipping
                // it, deliberately, so the positional alignment with bufferWraps
                // and bufferFilters holds (surfaceshadereffect.cpp says why: a
                // dropped empty broke that alignment on the very next load, since
                // toJson re-emits empties). What an empty entry costs is the whole
                // chain: it fails the registry's existence check at scan time and
                // fails the pack closed to single-pass.
                lints << QStringLiteral(
                    "empty bufferShaders entry (kept in place, but it fails the scan-time existence check and "
                    "drops the WHOLE pack to single-pass)");
                continue;
            }
            if (SurfaceShaderRegistry::isBuiltinBufferShader(bufName)) {
                const QString resolvedBuiltin =
                    SurfaceShaderRegistry::resolveBuiltinBufferShader(bufName, QDir(packDir).absolutePath());
                if (resolvedBuiltin.isEmpty()) {
                    lints << QStringLiteral("unknown or unlocatable builtin buffer shader: %1").arg(bufName);
                    continue;
                }
                // RESOLVED FROM OUTSIDE THIS TREE, which is the dev-passes /
                // CI-fails shape. The registry probes the pack's sibling shared/
                // first and then falls back to QStandardPaths, so a self-contained
                // tree missing a builtin file quietly resolves it from the
                // INSTALLED copy under /usr/share. On a developer machine with the
                // package on it the pack validates; in CI, or on any machine
                // without the install, the same tree fails.
                //
                // packSharedRoots is what draws the line, and it draws it the right
                // way round on its own: for a self-contained tree it is the sibling
                // shared/ and nothing else, so an outside resolution is not in the
                // list. For an INSTALLED pack it widens to the XDG chain, so
                // resolving from the system prefix is expected and silent.
                const QString resolvedDir = QFileInfo(resolvedBuiltin).canonicalPath();
                bool insideTree = false;
                for (const QString& root : packSharedRoots(packDir)) {
                    if (!root.isEmpty() && QFileInfo(root).canonicalFilePath() == resolvedDir) {
                        insideTree = true;
                        break;
                    }
                }
                if (!insideTree) {
                    lints << QStringLiteral(
                                 "%1 resolved to %2, which is outside this pack tree's shared roots. A tree that does "
                                 "not ship the file validates here only because a copy is installed, and fails "
                                 "anywhere without one")
                                 .arg(bufName, resolvedBuiltin);
                }
                continue;
            }
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined) {
                lints << QStringLiteral("multipass buffer shader path escapes the pack directory: %1").arg(bufName);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("multipass buffer shader missing: %1").arg(bufName);
            }
        }
        // The runtime caps buffer passes and drops the surplus with only a
        // journal warning, the same "runtime hid the author error" class the
        // sibling arms lint.
        if (declaredBuffers.size() > PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses) {
            lints << QStringLiteral("too many buffer shaders: %1 declared, cap is %2 (surplus dropped at load)")
                         .arg(static_cast<int>(declaredBuffers.size()))
                         .arg(PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
        }
        // bufferWraps / bufferFilters / bufferScales are positionally aligned to
        // bufferShaders, and a short array is padded with the single-value default
        // at load with no warning. Flag any mismatch, matching the animation arm,
        // rather than surplus alone, since a short array is the likelier slip.
        //
        // The message deliberately does NOT say the surplus is "dropped". That
        // wording was carried over from the animation arm and is false here: the
        // surface loader keeps these arrays at their declared length and only ever
        // reads the first bufferShaders.size() entries, so a surplus entry is
        // retained and simply never consulted. The one place anything really is
        // dropped is the pass budget, which the bufferScales block below reports
        // on its own.
        const auto lintBufferArrayLen = [&](QLatin1String key) {
            const QJsonArray arr = doc.object().value(key).toArray();
            if (!arr.isEmpty() && arr.size() != declaredBuffers.size()) {
                lints << QStringLiteral(
                             "%1 has %2 entries for %3 buffer shaders (aligned positionally; surplus entries are "
                             "never read and missing ones fall back to the single value at load)")
                             .arg(QString(key))
                             .arg(static_cast<int>(arr.size()))
                             .arg(static_cast<int>(declaredBuffers.size()));
            }
        };
        lintBufferArrayLen(QLatin1String("bufferWraps"));
        lintBufferArrayLen(QLatin1String("bufferFilters"));
        // bufferScales is aligned the same way, and each entry is clamped at
        // load like the single-value bufferScale (a non-number falls back to
        // it with only a journal warning).
        lintBufferArrayLen(QLatin1String("bufferScales"));
        {
            const QJsonArray scales = doc.object().value(QLatin1String("bufferScales")).toArray();
            // Past the pass budget fromJson DROPS the entry rather than clamping
            // it, so the per-entry messages below would state a consequence that
            // does not happen. Report the overflow once and lint only the entries
            // that survive.
            const qsizetype scaleCap = PhosphorShaders::kMaxBufferPasses;
            if (scales.size() > scaleCap) {
                lints << QStringLiteral(
                             "bufferScales has %1 entries, past the %2-pass budget; the surplus is "
                             "dropped at load rather than clamped")
                             .arg(static_cast<int>(scales.size()))
                             .arg(static_cast<int>(scaleCap));
            }
            for (qsizetype i = 0; i < scales.size() && i < scaleCap; ++i) {
                const QJsonValue v = scales.at(i);
                if (!v.isDouble()) {
                    lints << QStringLiteral(
                                 "bufferScales entry %1 is not a number (that pass falls back to "
                                 "bufferScale at load)")
                                 .arg(i);
                } else if (v.toDouble() < PhosphorShaders::kMinBufferScale
                           || v.toDouble() > PhosphorShaders::kMaxBufferScale) {
                    lints << QStringLiteral("bufferScales entry %1 out of range [%2, %3]: %4 (clamped at load)")
                                 .arg(i)
                                 .arg(PhosphorShaders::kMinBufferScale)
                                 .arg(PhosphorShaders::kMaxBufferScale)
                                 .arg(v.toDouble());
                }
            }
        }
        // Vocabulary, on all four spellings. validatedWrap / validatedFilter
        // clear an unrecognised token to empty with a journal warning only.
        const auto lintSurfaceTokens = [&lints, &doc](QLatin1String key, bool wrap) {
            for (const QJsonValue& v : doc.object().value(key).toArray()) {
                // A NON-STRING entry was the one wrap/filter fault that reached
                // the user with no diagnostic anywhere, not even a journal line:
                // QJsonValue::toString() answers empty for a number, bool, null,
                // array or object, and the emptiness gate below then reads it as
                // "not specified" rather than as the mistake it is.
                if (!v.isString()) {
                    lints << QStringLiteral("%1 has a non-string entry, which is ignored at load").arg(QString(key));
                    continue;
                }
                const QString tok = v.toString();
                const bool ok = wrap ? PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(tok)
                                     : PhosphorSurfaceShaders::SurfaceShaderContract::isValidFilterToken(tok);
                if (!tok.isEmpty() && !ok) {
                    lints << QStringLiteral("%1 value '%2' not in vocabulary (cleared at load)").arg(QString(key), tok);
                }
            }
        };
        lintSurfaceTokens(QLatin1String("bufferWraps"), true);
        lintSurfaceTokens(QLatin1String("bufferFilters"), false);
        const QString singleWrap = doc.object().value(QLatin1String("bufferWrap")).toString();
        if (!singleWrap.isEmpty() && !PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(singleWrap)) {
            lints << QStringLiteral("bufferWrap value '%1' not in vocabulary (cleared at load)").arg(singleWrap);
        }
        const QString singleFilter = doc.object().value(QLatin1String("bufferFilter")).toString();
        if (!singleFilter.isEmpty()
            && !PhosphorSurfaceShaders::SurfaceShaderContract::isValidFilterToken(singleFilter)) {
            lints << QStringLiteral("bufferFilter value '%1' not in vocabulary (cleared at load)").arg(singleFilter);
        }
        // DAEMON-ONLY, and the schema accepting these gave no hint of it. All four
        // wrap/filter spellings are declared daemon-only on SurfaceShaderEffect,
        // and the compositor bears that out: it creates every buffer target
        // GL_LINEAR / GL_CLAMP_TO_EDGE and never reads the keys. A pack that asks
        // for "repeat" or "nearest" therefore renders one way in the settings
        // preview and another on a real window, which is precisely the class of
        // divergence this validator exists to surface before a pack ships.
        const auto lintDaemonOnlyToken = [&lints, &doc](QLatin1String key, QLatin1String honoured) {
            QStringList offending;
            const QJsonValue value = doc.object().value(key);
            if (value.isString()) {
                if (!value.toString().isEmpty() && value.toString() != honoured) {
                    offending << value.toString();
                }
            } else {
                for (const QJsonValue& v : value.toArray()) {
                    const QString tok = v.toString();
                    if (!tok.isEmpty() && tok != honoured) {
                        offending << tok;
                    }
                }
            }
            if (!offending.isEmpty()) {
                offending.removeDuplicates();
                lints << QStringLiteral(
                             "%1 declares %2, which the DAEMON honours and the compositor ignores: it "
                             "creates every buffer target as '%3'. The pack will render differently in "
                             "the settings preview and on a window")
                             .arg(QString(key), offending.join(QLatin1String(", ")), QString(honoured));
            }
        };
        lintDaemonOnlyToken(QLatin1String("bufferWrap"), QLatin1String("clamp"));
        lintDaemonOnlyToken(QLatin1String("bufferWraps"), QLatin1String("clamp"));
        lintDaemonOnlyToken(QLatin1String("bufferFilter"), QLatin1String("linear"));
        lintDaemonOnlyToken(QLatin1String("bufferFilters"), QLatin1String("linear"));
        // The single-value twin of the per-entry not-a-number lint above. toDouble
        // answers its DEFAULT for a string or a bool, so `"bufferScale": "0.5"`
        // silently loads as 1.0 and the range check below sees nothing wrong.
        const QJsonValue rawScaleValue = doc.object().value(QLatin1String("bufferScale"));
        if (!rawScaleValue.isUndefined() && !rawScaleValue.isNull() && !rawScaleValue.isDouble()) {
            lints << QStringLiteral("bufferScale is not a number, so it falls back to 1.0 at load");
        }
        const double rawScale = rawScaleValue.toDouble(1.0);
        if (rawScale < PhosphorShaders::kMinBufferScale || rawScale > PhosphorShaders::kMaxBufferScale) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PhosphorShaders::kMinBufferScale)
                         .arg(PhosphorShaders::kMaxBufferScale)
                         .arg(rawScale);
        }
    }
    if (!QFile::exists(eff.fragmentShaderPath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(fragLabel);
    }
    // An explicit per-pack `vertexShader` was resolved to absolute above; if the
    // author typo'd the path the vertex stage below silently skips it (the
    // exists() guard bows out with no diagnostic), so lint it here the same way
    // the fragment stage is linted. An empty vertexShaderPath is the normal
    // shared-surface.vert case and is not an error.
    if (!eff.vertexShaderPath.isEmpty() && !QFile::exists(eff.vertexShaderPath)) {
        lints << QStringLiteral("vertex shader missing: %1").arg(QFileInfo(eff.vertexShaderPath).fileName());
    }
    // A PACK-LOCAL surface.vert THAT THE METADATA DOES NOT DECLARE runs on one
    // host and not the other, which is the worst shape a divergence can take: the
    // pack looks fine and renders differently.
    //
    // The daemon's vertex lookup falls through an undeclared `vertexShader` to a
    // `surface.vert` sitting beside the fragment, so it picks the file up. The
    // compositor does not look there at all and uses its default vertex stage. So
    // an author who drops the file in without declaring it sees their stage in the
    // settings preview and not on a real window.
    //
    // Lints the UNDECLARED case only. Declaring the file is the supported way to
    // ship a per-pack vertex stage and is handled above.
    if (eff.vertexShaderPath.isEmpty()) {
        const QString siblingVert = QFileInfo(eff.fragmentShaderPath).absolutePath() + QStringLiteral("/surface.vert");
        if (QFile::exists(siblingVert)) {
            lints << QStringLiteral(
                "surface.vert sits beside the fragment but `vertexShader` does not name it. The daemon picks up an "
                "undeclared sibling and the compositor does not, so this stage runs in the settings preview and not "
                "on a real window. Declare it in metadata.json");
        }
    }

    if (lints.isEmpty()) {
        out << "  " << padLabel(QStringLiteral("metadata")) << "OK\n";
    } else {
        out << "  " << padLabel(QStringLiteral("metadata")) << "ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // A NOTE, not a lint: declaring depth is legal and the runtime honours it.
    // What an author cannot see is that it is honoured on the DAEMON alone, and
    // the compositor is where a decoration lives on a real window, so the same
    // pack reads one way in the settings preview and another on screen. Printed
    // for a single-pass pack too; the multipass block below only knows about
    // the interaction with bufferScales, which a single-pass pack does not have.
    if (doc.object().value(QLatin1String("depthBuffer")).toBool()) {
        out << "  " << padLabel(QStringLiteral("note"))
            << "\"depthBuffer\" is honoured on the daemon only (settings preview, OSD and popup decorations); "
               "the compositor implements no depth buffer for surface packs\n";
        out << "  " << padLabel(QString())
            << "and the surface family ships no depth module, so declare "
               "`layout(binding = 16) uniform sampler2D uDepthBuffer;` yourself\n";
    }
    // Also a NOTE, beside the one above because a note printed before the lint flush
    // lands above its own `metadata ERROR` header. cornerRadius is not reserved, so a
    // pack may declare it for a badge and be right; only declarers get the silhouette.
    const auto declaresParam = [&eff](QLatin1String id) {
        return std::any_of(eff.parameters.cbegin(), eff.parameters.cend(), [id](const auto& p) {
            return p.id == id;
        });
    };
    if (declaresParam(QLatin1String("cornerRadius")) && !declaresParam(QLatin1String("roundBottomCorners"))) {
        out << "  " << padLabel(QStringLiteral("note"))
            << "declares cornerRadius but not roundBottomCorners, so it cannot follow a chain that "
               "squares its bottom corners\n";
    }

    // Preset lint: every preset key must name a declared parameter, and every value
    // must match that parameter's declared type and range. AFTER the metadata block,
    // matching the animation and pointer arms. Run before it, this printed
    // `presets ERROR` above `metadata OK`, which is the self-contradicting shape the
    // collected-then-printed design was introduced to avoid.
    // Same gap as the animation arm, same reason: presets parse before sourceDir.
    errors += reportPresetLints(out,
                                rawPresetLints(doc.object()) + imageParamPresetLints(doc.object())
                                    + presetLints(packDir, eff.presets, eff.parameters));

    // ── stage compile (reproduce the daemon runtime fragment assembly) ──
    //
    // ONE include-root list for every stage below (fragment, buffer passes,
    // vertex) and for both dialects. It was rebuilt identically three times,
    // which is three chances for one of them to drift from the other two while
    // the report keeps claiming all three reproduce the runtime.
    //
    // Match the runtime SurfaceShaderItem::surfaceIncludePaths(): each surface
    // data dir contributes its `shared` subdir AND the dir itself, so a shader
    // that resolves an include from the packs-root (not just `shared/`) bakes
    // identically here and cannot false-fail the gate. The sibling zone
    // validator uses the same {root/shared, root} pair. Sibling `shared/` first,
    // then the family's XDG roots, so an INSTALLED pack (whose helpers live in
    // the system prefix, not beside it) resolves its includes the way the
    // runtime does.
    const QString surfacePacksRoot = QFileInfo(packDir).absolutePath();
    const QStringList includePaths = QStringList(packSharedRoots(packDir)) << surfacePacksRoot;
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << padLabel(fragLabel) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
            ++errors;
        } else {
            const QString raw = QString::fromUtf8(frag.readAll());
            QString err;
            // Assemble an entry-only pack (a `vec4 pSurface(vec2 uv)` body, no
            // main()) into a full TU before expansion, identical to the daemon /
            // kwin paths, so `pSurface` packs validate. A main() pack passes
            // through unchanged.
            const QString assembled = PhosphorShaders::assembleEntryPoint(
                raw, SurfaceShaderRegistry::surfaceEntryPrologue(), SurfaceShaderRegistry::surfaceEntryCandidates());
            const QString expanded = ShaderCompiler::expandSource(
                assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << padLabel(fragLabel) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, SurfaceShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, fragLabel, result, declaredParamNames(eff.parameters));
            }
        }
    }
    // The COMPOSITOR branch of the same stage. Outside the exists() guard above:
    // the helper does its own check, and an absent stage is already linted.
    {
        const QStringList compositorIncludePaths = QStringList(packSharedRoots(packDir))
            << QFileInfo(packDir).absolutePath();
        errors += bakeCompositorStage(out, eff, eff.fragmentShaderPath, fragLabel, QStringLiteral("frag"),
                                      compositorIncludePaths, /*scaffold=*/true);
    }

    // ── multipass buffer passes ──
    // Buffer passes carry their own main() (no entry scaffold, no param preamble)
    // and bake on the daemon Qt-RHI path, same as overlay packs. The compositor
    // runtime executes them via the GL-FBO chain; both share this source.
    if (eff.isMultipass) {
        for (const QString& buf : eff.bufferShaderPaths) {
            if (!QFile::exists(buf)) {
                continue; // missing buffers already linted above
            }
            // A `builtin:` buffer resolved via the QStandardPaths fallback (user
            // pack with no sibling shared/ dir) lives OUTSIDE surfacePacksRoot,
            // where its angle-includes (surface_blur.glsl) would miss the
            // pack-derived include paths. Append the buffer's own dir so the
            // installed shared dir resolves; for bundled packs this duplicates
            // an entry already in the list, which the resolver tolerates.
            const QStringList bufferIncludePaths = QStringList(includePaths) << QFileInfo(buf).absolutePath();
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
                ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), bufferIncludePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << padLabel(label) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const ShaderCompiler::Result result =
                    ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
                // NO declared names: a buffer pass gets no p_<id> preamble, so it
                // cannot reference one and a did-you-mean hint could only point at
                // a symbol that does not exist in this stage. The pointer arm and
                // the shared compileStage helper pass an empty list for the same
                // reason.
                errors += reportCompile(out, label, result, kNoDeclaredParams);
            }
            // The COMPOSITOR branch of the same buffer pass. scaffold=false: a
            // buffer ships its own main() and reads its parameters by raw contract
            // slot, so it takes neither the entry scaffold nor the preamble.
            errors += bakeCompositorStage(out, eff, buf, label, QStringLiteral("frag"), bufferIncludePaths,
                                          /*scaffold=*/false);
        }
    }

    // ── vertex stage ──
    // Mirror the daemon runtime (SurfaceShaderItem::updatePaintNode): an explicit
    // per-pack `vertexShader` wins, else a per-pack `surface.vert` beside the
    // fragment, else a shared `surface.vert` from the include paths. The vertex
    // stage gets no scaffold and no param preamble — it ships its own main() (the
    // fragment stage's pSurface scaffold does not apply here). Without this a
    // malformed vertex stage passes the validator and only fails at the live
    // daemon — the sibling zone path (validatePack) already bakes the vertex
    // stage, so surface validation must too.
    {
        QString vertPath = eff.vertexShaderPath;
        if (vertPath.isEmpty()) {
            // Beside the FRAGMENT (matching the daemon runtime and the comment
            // above), not merely inside packDir — a nested fragmentShader path
            // resolves its sibling surface.vert the same way at runtime.
            const QString vertLocal =
                QFileInfo(eff.fragmentShaderPath).absolutePath() + QStringLiteral("/surface.vert");
            if (QFile::exists(vertLocal)) {
                vertPath = vertLocal;
            } else {
                for (const QString& incDir : includePaths) {
                    const QString candidate = incDir + QStringLiteral("/surface.vert");
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
                    // Empty for the same reason as the buffer passes above: the
                    // vertex stage takes no generated preamble either.
                    errors += reportCompile(out, label, result, kNoDeclaredParams);
                }
            }
        }
        // The COMPOSITOR branch, for a DECLARED vertex stage ONLY. The shared
        // surface.vert fallback resolved above is deliberately not baked here: the
        // compositor never resolves that file (it supplies its own built-in
        // fullscreen-quad stage), and the shared one reads qt_Matrix, which is a
        // daemon UBO member and undeclared under PLASMAZONES_KWIN. Baking it would
        // fail every bundled pack on a stage that never runs there.
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
