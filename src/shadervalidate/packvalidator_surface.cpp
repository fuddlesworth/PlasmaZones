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

#include <rhi/qshader.h>

using PhosphorRendering::ShaderCompiler;
using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace PlasmaZones::ShaderValidate {

namespace {

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
    QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembled, QFileInfo(path).absolutePath(),
                                                                         includePaths, &err);
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
    return reportCompositorCompile(out, label, stage, src, tool);
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
        out << name << "\n  metadata       ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        out << name << "\n  metadata       ERROR\n    invalid JSON: " << perr.errorString() << "\n  → 1 error\n\n";
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
        out << name
            << "\n  metadata       ERROR\n    fragmentShader path escapes the pack directory (path traversal "
               "rejected)\n  → 1 error\n\n";
        return 1;
    }
    for (QString& b : eff.bufferShaderPaths) {
        // `builtin:` tokens resolve against the surface shared/ dir (fixed
        // whitelist, same resolver as the runtime registry) rather than the
        // pack dir; an unknown token resolves empty and is linted below as a
        // missing buffer, mirroring the runtime's fail-closed path.
        if (SurfaceShaderRegistry::isBuiltinBufferShader(b)) {
            b = SurfaceShaderRegistry::resolveBuiltinBufferShader(b, QDir(packDir).absolutePath());
            continue;
        }
        if (!confineToPack(b)) {
            out << name
                << "\n  metadata       ERROR\n    bufferShaders path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
    }
    if (!confineToPack(eff.vertexShaderPath)) {
        out << name
            << "\n  metadata       ERROR\n    vertexShader path escapes the pack directory (path traversal rejected)\n "
               " "
               "→ 1 error\n\n";
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

    // ── metadata lints ──
    static const QStringList kSurfaceParamTypes = {QStringLiteral("float"), QStringLiteral("int"),
                                                   QStringLiteral("bool"), QStringLiteral("color")};
    QStringList lints;
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
    const QJsonArray declaredTextures = doc.object().value(QLatin1String("textures")).toArray();
    if (declaredTextures.size() > PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        const QString texPath = v.toObject().value(QLatin1String("path")).toString();
        if (texPath.isEmpty()) {
            lints << QStringLiteral("texture entry with empty `path` (dropped at load)");
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
    const QJsonArray rawBufferShaders = doc.object().value(QLatin1String("bufferShaders")).toArray();
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
        }
        for (const QJsonValue& v : declaredBuffers) {
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
                // fromJson SKIPS an empty entry while bufferWraps and
                // bufferFilters keep every entry in place, and those arrays are
                // positionally aligned with this one — so one empty entry shifts
                // every later pass's wrap and filter override by one, silently.
                lints << QStringLiteral(
                    "empty bufferShaders entry (dropped at load, which shifts the bufferWraps and bufferFilters "
                    "alignment for every later pass)");
                continue;
            }
            if (SurfaceShaderRegistry::isBuiltinBufferShader(bufName)) {
                if (SurfaceShaderRegistry::resolveBuiltinBufferShader(bufName, QDir(packDir).absolutePath())
                        .isEmpty()) {
                    lints << QStringLiteral("unknown or unlocatable builtin buffer shader: %1").arg(bufName);
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
        // bufferWraps / bufferFilters are positionally aligned to bufferShaders
        // and lossy in BOTH directions at load: a longer array is trimmed and a
        // shorter one is padded with the single-value default, neither with a
        // warning. Flag any mismatch, matching the animation arm, rather than
        // surplus alone — a short array is the likelier authoring slip.
        const auto lintBufferArrayLen = [&](QLatin1String key) {
            const QJsonArray arr = doc.object().value(key).toArray();
            if (!arr.isEmpty() && arr.size() != declaredBuffers.size()) {
                lints << QStringLiteral(
                             "%1 has %2 entries for %3 buffer shaders (aligned positionally; "
                             "surplus dropped and missing entries fall back at load)")
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
            for (qsizetype i = 0; i < scales.size(); ++i) {
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
        const double rawScale = doc.object().value(QLatin1String("bufferScale")).toDouble(1.0);
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

    if (lints.isEmpty()) {
        out << "  " << padLabel(QStringLiteral("metadata")) << "OK\n";
    } else {
        out << "  " << padLabel(QStringLiteral("metadata")) << "ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // Preset lint: every preset key must name a declared parameter, and every value
    // must match that parameter's declared type and range. AFTER the metadata block,
    // matching the animation and pointer arms. Run before it, this printed
    // `presets ERROR` above `metadata OK`, which is the self-contradicting shape the
    // collected-then-printed design was introduced to avoid.
    errors += reportRawPresetProblems(out, doc.object());
    // Same gap as the animation arm, same reason: presets parse before sourceDir.
    errors += reportImageParamPresets(out, doc.object());
    errors += reportPresetProblems(out, packDir, eff.presets, eff.parameters);

    // ── stage compile (reproduce the daemon runtime fragment assembly) ──
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << padLabel(fragLabel) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
            ++errors;
        } else {
            const QString raw = QString::fromUtf8(frag.readAll());
            const QString surfacePacksRoot = QFileInfo(packDir).absolutePath();
            // Match the runtime SurfaceShaderItem::surfaceIncludePaths(): each
            // surface data dir contributes its `shared` subdir AND the dir
            // itself, so a shader that resolves an include from the packs-root
            // (not just `shared/`) bakes identically here and can't false-fail
            // the gate. The sibling zone validator uses the same
            // {root/shared, root} pair.
            // Sibling shared/ first, then the family's XDG roots, so an
            // INSTALLED pack (whose helpers live in the system prefix, not
            // beside it) resolves its includes the way the runtime does.
            const QStringList includePaths = QStringList(packSharedRoots(packDir)) << surfacePacksRoot;
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
        const QString surfacePacksRoot = QFileInfo(packDir).absolutePath();
        // Match the runtime SurfaceShaderItem::surfaceIncludePaths(): each surface
        // data dir contributes its `shared` subdir AND the dir itself, so a shader
        // that resolves an include from the packs-root (not just `shared/`) bakes
        // identically here and can't false-fail the gate. The sibling zone
        // validator uses the same {root/shared, root} pair.
        // Sibling shared/ first, then the family's XDG roots, so an
        // INSTALLED pack (whose helpers live in the system prefix, not
        // beside it) resolves its includes the way the runtime does.
        const QStringList includePaths = QStringList(packSharedRoots(packDir)) << surfacePacksRoot;
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
                errors += reportCompile(out, label, result, declaredParamNames(eff.parameters));
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
        const QString surfacePacksRoot = QFileInfo(packDir).absolutePath();
        // Match the runtime SurfaceShaderItem::surfaceIncludePaths(): each surface
        // data dir contributes its `shared` subdir AND the dir itself, so a shader
        // that resolves an include from the packs-root (not just `shared/`) bakes
        // identically here and can't false-fail the gate. The sibling zone
        // validator uses the same {root/shared, root} pair.
        // Sibling shared/ first, then the family's XDG roots, so an
        // INSTALLED pack (whose helpers live in the system prefix, not
        // beside it) resolves its includes the way the runtime does.
        const QStringList includePaths = QStringList(packSharedRoots(packDir)) << surfacePacksRoot;
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
                    errors += reportCompile(out, label, result, declaredParamNames(eff.parameters));
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
