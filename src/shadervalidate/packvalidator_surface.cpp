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
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

using PhosphorRendering::ShaderCompiler;
using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace PlasmaZones::ShaderValidate {

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
    // Multipass buffer lints — read RAW metadata, not the parsed struct: fromJson
    // clamps bufferScale into [0.125, 1.0] and drops missing buffers, so a lint
    // over the parsed values would hide author errors.
    if (eff.isMultipass) {
        const QJsonArray declaredBuffers = doc.object().value(QLatin1String("bufferShaders")).toArray();
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
        out << "  metadata       OK\n";
    } else {
        out << "  metadata       ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // ── stage compile (reproduce the daemon runtime fragment assembly) ──
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << fragLabel.leftJustified(15) << "ERROR\n    cannot read " << eff.fragmentShaderPath << "\n";
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
                out << "  " << fragLabel.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
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
                out << "  " << label.leftJustified(15) << "ERROR\n    cannot read " << buf << "\n";
                ++errors;
                continue;
            }
            const QString rawBuf = QString::fromUtf8(bufFile.readAll());
            QString err;
            const QString expanded =
                ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), bufferIncludePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << label.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
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
