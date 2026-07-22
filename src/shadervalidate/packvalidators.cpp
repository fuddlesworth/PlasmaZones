// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three per-authoring-model shader-pack validators — see packvalidators.h.
// Each reproduces the matching runtime's GLSL assembly and bakes every stage
// through headless glslang (QShaderBaker, CPU-only). Shared compile/report
// helpers live in packvalidatorcommon.h.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include "daemon/rendering/zoneentryscaffold.h"

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;
using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace PlasmaZones::ShaderValidate {

// Validate one pack directory. Returns the number of errors found.
int validatePack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QString parseErr;
    const ShaderRegistry::ShaderInfo info = ShaderRegistry::parsePackMetadata(packDir, &parseErr);
    if (!parseErr.isEmpty()) {
        out << name << "\n  metadata      ERROR\n    " << parseErr << "\n  → 1 error\n\n";
        return 1;
    }

    out << name << "  (" << info.parameters.size() << " param" << (info.parameters.size() == 1 ? "" : "s") << ", "
        << (info.isMultipass ? "multipass" : "single-pass") << ")\n";

    int errors = 0;

    // ── metadata lints ──
    QStringList lints;
    QHash<QString, QString> claimedLane; // "pool#slot" → first param id, for collision detection
    for (const ShaderRegistry::ParameterInfo& p : info.parameters) {
        if (!kValidParamTypes.contains(p.type)) {
            lints << QStringLiteral("unknown param type '%1' for '%2'").arg(p.type, p.id);
        }
        // An id that isn't a valid GLSL identifier gets no p_ define and no lane
        // (parseShaderMetadata leaves its slot at -1) — surface the real fault, and
        // skip the collision check so two such params don't false-collide on "-1".
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
            continue;
        }
        const QString laneKey = poolName(p.type) + QStringLiteral("#") + QString::number(p.slot);
        if (claimedLane.contains(laneKey)) {
            lints << QStringLiteral("slot collision: '%1' and '%2' both map to %3 lane %4")
                         .arg(claimedLane.value(laneKey), p.id, poolName(p.type))
                         .arg(p.slot);
        } else {
            claimedLane.insert(laneKey, p.id);
        }
    }
    // Buffer-pass + bufferScale lints check the RAW metadata, not the parsed
    // ShaderInfo: parseShaderMetadata clamps bufferScale into [0.125, 1.0] and
    // clears bufferShaderPaths when a declared buffer is missing, so a lint reading
    // the parsed values would silently pass an author error the runtime hid.
    if (info.isMultipass) {
        QJsonObject root;
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (metaFile.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(metaFile.readAll()).object();
        }

        QStringList bufferNames;
        const QJsonArray declared = root.value(QLatin1String("bufferShaders")).toArray();
        for (const QJsonValue& v : declared) {
            if (!v.toString().isEmpty()) {
                bufferNames << v.toString();
            }
        }
        if (bufferNames.isEmpty()) {
            bufferNames << root.value(QLatin1String("bufferShader")).toString(QStringLiteral("buffer.frag"));
        }
        for (const QString& bufName : bufferNames) {
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined) {
                lints << QStringLiteral("multipass buffer shader path escapes the pack directory: %1").arg(bufName);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("multipass buffer shader missing: %1").arg(bufName);
            }
        }

        const double rawScale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
        if (rawScale < 0.125 || rawScale > 1.0) {
            lints << QStringLiteral("bufferScale out of range [0.125, 1.0]: %1 (clamped at load)").arg(rawScale);
        }
    }
    if (!QFile::exists(info.sourcePath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(QFileInfo(info.sourcePath).fileName());
    }

    if (lints.isEmpty()) {
        out << "  metadata      OK\n";
    } else {
        out << "  metadata      ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // ── stage compiles (reproduce the runtime assembly) ──
    const QString packsRoot = QFileInfo(packDir).absolutePath();
    const QStringList includePaths = {packsRoot + QStringLiteral("/shared"), packsRoot};
    const QString preamble = ShaderRegistry::paramPreamble(info);

    if (QFile::exists(info.sourcePath)) {
        // Label by the actual fragment filename — parsePackMetadata honours a
        // custom `fragmentShader` field (default effect.frag), so the stage label
        // tracks the real file rather than assuming effect.frag.
        errors += compileStage(out, QFileInfo(info.sourcePath).fileName(), info.sourcePath, QShader::FragmentStage,
                               includePaths, /*useScaffold=*/true, preamble, info);
    }
    // Only multipass packs bake buffer passes — parseShaderMetadata seeds a
    // default buffer.frag path for every pack, but the runtime (bakeBufferShaders)
    // ignores it unless isMultipass, so the validator must gate identically.
    if (info.isMultipass) {
        for (const QString& buf : info.bufferShaderPaths) {
            if (QFile::exists(buf)) {
                errors += compileStage(out, QFileInfo(buf).fileName(), buf, QShader::FragmentStage, includePaths,
                                       /*useScaffold=*/false, QString(), info);
            }
        }
    }
    // Vertex: per-pack zone.vert if present, else the shared zone.vert the runtime
    // falls back to.
    QString vertPath = info.vertexShaderPath;
    if (vertPath.isEmpty()) {
        vertPath = packsRoot + QStringLiteral("/shared/zone.vert");
    }
    if (QFile::exists(vertPath)) {
        errors += compileStage(out, QFileInfo(vertPath).fileName(), vertPath, QShader::VertexStage, includePaths,
                               /*useScaffold=*/false, QString(), info);
    }

    if (errors == 0) {
        out << "  → OK\n\n";
    } else {
        out << "  → " << errors << (errors == 1 ? " error\n\n" : " errors\n\n");
    }
    return errors;
}

// Validate one ANIMATION pack directory (data/animations/*). Reproduces the
// animation runtime's fragment assembly on the daemon (Qt-RHI) path — the entry
// scaffold (pTransition / pIn+pOut, or a pass-through main()), the generated
// p_<id> preamble, and include expansion — then bakes through headless glslang.
// Returns the number of errors found.
//
// The kwin-effect classic-GL path (`#define PLASMAZONES_KWIN`, default-block
// uniforms) is NOT baked here: QShaderBaker compiles Vulkan-dialect GLSL and
// rejects default-block uniforms, so the kwin branch needs a separate
// OpenGL-target compiler. Compositor-only packs (desktop / geometry / move
// classes — see shaderEffectIsCompositorOnly) are authored against that kwin
// dialect directly and never run on the daemon, so only their metadata is
// linted here; their compile coverage is test_animation_shader_kwin_bake.
// Daemon-capable packs get the full stage compile below.
int validateAnimationPack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        out << name << "\n  metadata      ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        out << name << "\n  metadata      ERROR\n    invalid JSON: " << perr.errorString() << "\n  → 1 error\n\n";
        return 1;
    }

    AnimationShaderEffect eff = AnimationShaderEffect::fromJson(doc.object());
    eff.sourceDir = QDir(packDir).absolutePath();
    // Mirror AnimationShaderRegistry::parseEffect: the fragment path comes from
    // the metadata `fragmentShader` field, resolved relative to the pack dir and
    // confined to it (the metadata is user-editable, so a `../…`/absolute path
    // must be rejected, not opened and fed to glslang — same guard as
    // validateSurfacePack). The runtime does NOT default to effect.frag — a pack
    // that omits the field is unreachable, so an empty path stays empty and is
    // caught by isValid() below.
    if (!eff.fragmentShaderPath.isEmpty()) {
        const auto confined = confinedPackPath(packDir, eff.fragmentShaderPath);
        if (!confined) {
            out << name
                << "\n  metadata      ERROR\n    fragmentShader path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
        eff.fragmentShaderPath = *confined;
    }
    if (!eff.isValid()) {
        out << name << "\n  metadata      ERROR\n    missing required field (id / fragmentShader)\n  → 1 error\n\n";
        return 1;
    }
    const QString fragLabel = QFileInfo(eff.fragmentShaderPath).fileName();

    out << name << "  (" << eff.parameters.size() << " param" << (eff.parameters.size() == 1 ? "" : "s") << ", "
        << eff.textures.size() << " texture" << (eff.textures.size() == 1 ? "" : "s") << ")\n";

    int errors = 0;

    // ── metadata lints ──
    // Animation params are always auto-slot (declaration order per pool), so
    // there are no explicit-slot collisions to detect; lint the param types and
    // ids that gate whether a p_<id> define is emitted at all.
    static const QStringList kAnimParamTypes = {QStringLiteral("float"), QStringLiteral("int"), QStringLiteral("bool"),
                                                QStringLiteral("color")};
    QStringList lints;
    for (const AnimationShaderEffect::ParameterInfo& p : eff.parameters) {
        if (!kAnimParamTypes.contains(p.type)) {
            lints << QStringLiteral(
                         "unknown param type '%1' for '%2' (animation params are float/int/bool/color; "
                         "images are textures, not params)")
                         .arg(p.type, p.id);
        }
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
        }
    }
    // Texture lints mirror parseEffect's parse-time journal warnings. fromJson
    // silently drops these from the in-memory struct, so read the RAW metadata
    // textures array (same as parseEffect) rather than eff.textures.
    const QJsonArray declaredTextures = doc.object().value(QLatin1String("textures")).toArray();
    if (declaredTextures.size() > PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        if (v.toObject().value(QLatin1String("path")).toString().isEmpty()) {
            lints << QStringLiteral("texture entry with empty `path` (dropped at load)");
        }
    }
    if (!QFile::exists(eff.fragmentShaderPath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(fragLabel);
    }

    if (lints.isEmpty()) {
        out << "  metadata      OK\n";
    } else {
        out << "  metadata      ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // ── stage compile (reproduce the daemon runtime fragment assembly) ──
    // Skipped for compositor-only packs: their source is kwin classic-GL
    // (default-block uniforms, unbound samplers) that the strict SPIR-V
    // target rejects by design, and the daemon never loads them.
    if (PhosphorAnimationShaders::shaderEffectIsCompositorOnly(eff)) {
        out << "  " << fragLabel.leftJustified(14) << "SKIP (compositor-only pack; kwin-path GLSL)\n";
    } else if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << fragLabel.leftJustified(14) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
            ++errors;
        } else {
            const QString raw = QString::fromUtf8(frag.readAll());
            // An entry-only pack (pTransition / pIn+pOut) gets a generated main();
            // a traditional main() pack passes through — exactly as both runtimes do.
            const QString assembled =
                PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                                    AnimationShaderRegistry::animationEntryCandidates());
            // Animation runtime include paths are `shared`-only (see
            // surfaceanimator.cpp animIncludePaths, which appends only each
            // search path's `/shared` subdir), so the animation gate matches it.
            const QStringList includePaths = {QFileInfo(packDir).absolutePath() + QStringLiteral("/shared")};
            QString err;
            const QString expanded = ShaderCompiler::expandSource(
                assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << fragLabel.leftJustified(14) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, AnimationShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, fragLabel, result, declaredParamNames(eff.parameters));
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

// Validate one SURFACE pack directory (data/surface/*). Reproduces the surface
// runtime's fragment assembly on the daemon (Qt-RHI) path — the pSurface entry
// scaffold (an entry-only pack gets a generated main(); a pack with its own
// main() passes through unchanged) + include expansion + the generated p_<id>
// preamble — then bakes through headless glslang. Returns the error count.
//
// As with animation packs, the kwin-effect classic-GL branch
// (`#define PLASMAZONES_KWIN`, default-block uniforms) is NOT baked here:
// QShaderBaker compiles Vulkan-dialect GLSL and rejects default-block uniforms.
// Baking the #else branch validates the daemon UBO contract in
// surface_uniforms.glsl; the PLASMAZONES_KWIN plumbing is identical for every
// pack and exercised by the live compositor compile.
int validateSurfacePack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        out << name << "\n  metadata      ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        out << name << "\n  metadata      ERROR\n    invalid JSON: " << perr.errorString() << "\n  → 1 error\n\n";
        return 1;
    }

    SurfaceShaderEffect eff = SurfaceShaderEffect::fromJson(doc.object());
    eff.sourceDir = QDir(packDir).absolutePath();
    // The `fragmentShader` / `bufferShaders` / `vertexShader` paths come from the
    // user-editable metadata.json, so confine each to the pack dir (via
    // confinedPackPath) before it is opened and fed to glslang: a
    // `../../../etc/...` or absolute path must be rejected, not compiled. An
    // empty path is left as-is (that stage is simply absent).
    const auto confineToPack = [&packDir](QString& path) -> bool {
        if (path.isEmpty()) {
            return true;
        }
        const auto confined = confinedPackPath(packDir, path);
        if (!confined) {
            return false;
        }
        path = *confined;
        return true;
    };
    if (!confineToPack(eff.fragmentShaderPath)) {
        out << name
            << "\n  metadata      ERROR\n    fragmentShader path escapes the pack directory (path traversal "
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
                << "\n  metadata      ERROR\n    bufferShaders path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
    }
    if (!confineToPack(eff.vertexShaderPath)) {
        out << name
            << "\n  metadata      ERROR\n    vertexShader path escapes the pack directory (path traversal rejected)\n  "
               "→ 1 error\n\n";
        return 1;
    }
    if (!eff.isValid()) {
        out << name << "\n  metadata      ERROR\n    missing required field (id / fragmentShader)\n  → 1 error\n\n";
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
    const QJsonArray declaredTextures = doc.object().value(QLatin1String("textures")).toArray();
    if (declaredTextures.size() > PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        if (v.toObject().value(QLatin1String("path")).toString().isEmpty()) {
            lints << QStringLiteral("texture entry with empty `path` (dropped at load)");
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
    // Multipass buffer lints — read RAW metadata, not the parsed struct: fromJson
    // clamps bufferScale into [0.125, 1.0] and drops missing buffers, so a lint
    // over the parsed values would hide author errors.
    if (eff.isMultipass) {
        const QJsonArray declaredBuffers = doc.object().value(QLatin1String("bufferShaders")).toArray();
        for (const QJsonValue& v : declaredBuffers) {
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
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
        // bufferWraps / bufferFilters are positionally aligned to bufferShaders;
        // surplus entries beyond the buffer count are silently ignored at load
        // (surfaceshaderregistry.cpp), so flag a length mismatch the author
        // likely did not intend.
        const auto lintBufferArrayLen = [&](QLatin1String key) {
            const int extra = doc.object().value(key).toArray().size() - declaredBuffers.size();
            if (extra > 0) {
                lints << QStringLiteral("%1 has %2 more entr%3 than buffer shaders (surplus ignored at load)")
                             .arg(QString(key))
                             .arg(extra)
                             .arg(extra == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
            }
        };
        lintBufferArrayLen(QLatin1String("bufferWraps"));
        lintBufferArrayLen(QLatin1String("bufferFilters"));
        const double rawScale = doc.object().value(QLatin1String("bufferScale")).toDouble(1.0);
        if (rawScale < 0.125 || rawScale > 1.0) {
            lints << QStringLiteral("bufferScale out of range [0.125, 1.0]: %1 (clamped at load)").arg(rawScale);
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
        out << "  metadata      OK\n";
    } else {
        out << "  metadata      ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // ── stage compile (reproduce the daemon runtime fragment assembly) ──
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << fragLabel.leftJustified(14) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
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
            const QStringList includePaths = {surfacePacksRoot + QStringLiteral("/shared"), surfacePacksRoot};
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
                out << "  " << fragLabel.leftJustified(14) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, SurfaceShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, fragLabel, result, declaredParamNames(eff.parameters));
            }
        }
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
        const QStringList includePaths = {surfacePacksRoot + QStringLiteral("/shared"), surfacePacksRoot};
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
                out << "  " << label.leftJustified(14) << "ERROR\n    cannot read " << buf << "\n";
                ++errors;
                continue;
            }
            const QString rawBuf = QString::fromUtf8(bufFile.readAll());
            QString err;
            const QString expanded =
                ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), bufferIncludePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << label.leftJustified(14) << "ERROR\n    include expansion failed: " << err << "\n";
                ++errors;
            } else {
                const ShaderCompiler::Result result =
                    ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, label, result, declaredParamNames(eff.parameters));
            }
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
        const QStringList includePaths = {surfacePacksRoot + QStringLiteral("/shared"), surfacePacksRoot};
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
                out << "  " << label.leftJustified(14) << "ERROR\n    cannot read " << vertPath << "\n";
                ++errors;
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                QString err;
                const QString expanded =
                    ShaderCompiler::expandSource(rawVert, QFileInfo(vertPath).absolutePath(), includePaths, &err);
                if (expanded.isEmpty()) {
                    out << "  " << label.leftJustified(14) << "ERROR\n    include expansion failed: " << err << "\n";
                    ++errors;
                } else {
                    const ShaderCompiler::Result result =
                        ShaderCompiler::compile(expanded.toUtf8(), QShader::VertexStage);
                    errors += reportCompile(out, label, result, declaredParamNames(eff.parameters));
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
