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
#include "packvalidators_surface_lints.h"

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

    // RAW counts, not the parsed sizes. fromJson silently drops what it cannot use
    // (a malformed entry, anything past a cap), so the parsed size is what SURVIVED
    // rather than what the author WROTE, while every lint below counts the raw
    // array. The header owes the author the number in their own file, or it reads
    // "3 textures" three lines above "too many textures: 5 declared". A non-array
    // value counts 0, which is honest, and the separate type lint reports the shape.
    const qsizetype rawParamCount = doc.object().value(QLatin1String("parameters")).toArray().size();
    const qsizetype rawTextureCount = doc.object().value(QLatin1String("textures")).toArray().size();
    out << name << "  (" << rawParamCount << " param" << (rawParamCount == 1 ? "" : "s") << ", " << rawTextureCount
        << " texture" << (rawTextureCount == 1 ? "" : "s") << ", " << (eff.isMultipass ? "multipass" : "single-pass")
        << ")\n";

    int errors = 0;

    // ── metadata lints ──
    // Collected in packvalidator_surface_lints.cpp, which is the whole of what is
    // decidable from metadata.json alone. Flushed just below, so a lint added there
    // needs no change here.
    const QStringList lints = surfaceMetadataLints(doc.object(), eff, packDir);

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
    // the helper does its own check, and an absent stage is already linted. Uses the
    // ONE include-root list built above, per the note on it — this site rebuilt it
    // byte-identically, which was the drift hazard that note describes.
    errors += bakeCompositorStage(out, eff, eff.fragmentShaderPath, fragLabel, QStringLiteral("frag"), includePaths,
                                  /*scaffold=*/true);

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
