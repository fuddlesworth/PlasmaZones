// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The surface/decoration arm of plasmazones-shader-validate — see
// packvalidators.h. Reproduces the surface runtime's GLSL assembly
// (SurfaceShaderEffect + generated p_<id> preamble + include expansion) and
// bakes the fragment stage, the buffer passes and the vertex stage on the
// preview (Qt-RHI) dialect through headless QShaderBaker. The fragment, the
// buffer passes and a DECLARED vertex stage are also compiled in both of
// KWin's compositor dialects through glslang, and each fragment is linked
// against the vertex stage the compositor runs it with. The shared
// surface.vert is the preview's stage alone.
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
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <optional>
#include <vector>

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

// How the compositor assembles the MAIN fragment of a surface pack for the
// compositor arms (`#define PLASMAZONES_KWIN`, default-block uniforms), which
// QShaderBaker cannot compile, since it wants Vulkan-dialect GLSL. Buffer
// passes and vertex stages take a default CompositorAssembly instead: they
// compile WITHOUT the scaffold and the generated preamble and read their
// parameters by raw contract slot, so splicing one there would let them
// reference a p_<id> that fails where it ships.
//
// Modelled on the POINTER arm rather than the animation one: the surface
// compositor path splices no colour-management block and no orientation
// define. surface_compile.cpp assembles the entry, expands includes, splices
// the param preamble and hands the result to injectKwinDefineAfterVersion
// (the define block and the varying strip), and nothing else.
CompositorAssembly surfaceMainAssembly(const SurfaceShaderEffect& eff)
{
    CompositorAssembly assembly;
    assembly.entryPrologue = SurfaceShaderRegistry::surfaceEntryPrologue();
    assembly.entryCandidates = SurfaceShaderRegistry::surfaceEntryCandidates();
    assembly.preamble = SurfaceShaderRegistry::paramPreamble(eff);
    return assembly;
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
// COVERAGE BOUNDARY, as on the animation arm. The two compositor arms compile
// the pack after KWin's own rewrites, desktop GLSL 140 for a 6.7 desktop
// context and GLSL ES 300 for a GLES one, and link each fragment against the
// vertex stage the compositor pairs it with. glslang is not the compositor's
// own driver compiler, though, so a driver-specific rejection is out of reach.
int validateSurfacePack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    const std::optional<QJsonObject> metadata = readPackMetadata(out, packDir);
    if (!metadata) {
        return 1;
    }
    const QJsonObject& root = *metadata;

    SurfaceShaderEffect eff = SurfaceShaderEffect::fromJson(root);
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
    // Every offender named, not just the first. The fragment exit above and the
    // vertex exit below each cover ONE key, so naming it adds nothing; bufferShaders is a
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
            << "ERROR\n    vertexShader path escapes the pack directory (path traversal rejected)\n";
        out << "  → 1 error\n\n";
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
    const qsizetype rawParamCount = root.value(QLatin1String("parameters")).toArray().size();
    const qsizetype rawTextureCount = root.value(QLatin1String("textures")).toArray().size();
    out << name << "  (" << rawParamCount << " param" << (rawParamCount == 1 ? "" : "s") << ", " << rawTextureCount
        << " texture" << (rawTextureCount == 1 ? "" : "s") << ", " << (eff.isMultipass ? "multipass" : "single-pass")
        << ")\n";

    int errors = 0;

    // ── metadata lints ──
    // Collected in packvalidator_surface_lints.cpp, which is the whole of what is
    // decidable from metadata.json alone. Flushed just below, so a lint added there
    // needs no change here.
    const QStringList lints = surfaceMetadataLints(root, eff, packDir);

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
    // pack reads one way in the settings preview and another on screen.
    //
    // GATED ON THE CHAIN ACTUALLY RUNNING. The registry's coherence block sets
    // useDepthBuffer = false for any pack it resolves to single-pass, so the daemon
    // never sees the flag on a single-pass pack, on a `multipass: true` with no
    // bufferShaders, or on one whose entry does not resolve. The note used to print
    // for those anyway and claimed the daemon honours the key two lines under the
    // lint saying it is never read, which is the lint an author needs there.
    const bool depthChainRuns = eff.isMultipass && !eff.bufferShaderPaths.isEmpty()
        && std::all_of(eff.bufferShaderPaths.cbegin(), eff.bufferShaderPaths.cend(), [](const QString& p) {
                                    return !p.isEmpty() && QFileInfo(p).isFile();
                                });
    if (depthChainRuns && root.value(QLatin1String("depthBuffer")).toBool()) {
        out << "  " << padLabel(QStringLiteral("note"))
            << "\"depthBuffer\" is honoured on the daemon only (settings preview, OSD and popup decorations); "
               "the compositor implements no depth buffer for surface packs\n";
        out << "  " << padLabel(QString())
            << "and the surface family ships no depth module, so declare "
               "`layout(binding = 16) uniform sampler2D uDepthBuffer;` yourself, inside `#ifndef "
               "PLASMAZONES_KWIN` (the compositor branch compiles as GLSL ES 300 on a GLES context, which has no "
               "binding qualifier)\n";
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
    errors += reportPresetLints(
        out, rawPresetLints(root) + imageParamPresetLints(root) + presetLints(packDir, eff.presets, eff.parameters));

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
    bool fragReadsBackdrop = false;
    // The compositor stages that compiled, for the links at the end.
    std::optional<CompositorStage> mainFrag;
    if (QString raw; QFile::exists(eff.fragmentShaderPath)) {
        if (!readStageSource(out, fragLabel, eff.fragmentShaderPath, raw)) {
            ++errors;
        } else {
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
                // Recorded here, REPORTED after the compositor bake below, so the note does not
                // land between one stage's two dialect results. Scanned on the PACK's own source
                // rather than the expanded TU, because the shared helper DEFINES these names and
                // merely including it would then read as using them.
                fragReadsBackdrop =
                    raw.contains(QLatin1String("backdropTexel")) || raw.contains(QLatin1String("uBackdrop"));
            }
            // The COMPOSITOR branch of the same stage, from the same read and
            // the ONE include-root list built above, per the note on it.
            mainFrag = bakeCompositorStage(out, errors, fragLabel, PhosphorShaders::ShaderStage::Fragment, raw,
                                           eff.fragmentShaderPath, includePaths, surfaceMainAssembly(eff));
        }
    }

    // A NOTE, not a lint, and deliberately so: shared/surface_backdrop.glsl tells authors to gate
    // styling on uHasBackdrop for an explicit fallback, so reading the backdrop without the flag is
    // a SUPPORTED pattern. What an author cannot see is that the flag is a necessary precondition
    // rather than a hint — the compositor gates the whole capture on needsBackdrop, so without it
    // uHasBackdrop stays false and backdropTexel() returns the transparent fallback on every route.
    // The multipass arm hard-lints this for the two builtin passes that have backdropTexel as their
    // ONLY source; here it can only be a note. KNOWN BOUNDARY: a pack that calls backdropTexel from
    // a pack-local included helper rather than from its own fragment is missed, which is the price
    // of scanning the pack's own source instead of the expanded TU.
    if (fragReadsBackdrop && !root.value(QLatin1String("needsBackdrop")).toBool()) {
        out << "  " << padLabel(QStringLiteral("note"))
            << "reads the backdrop (backdropTexel/uBackdrop) but \"needsBackdrop\" is not true, so "
               "nothing is captured and uHasBackdrop stays false\n";
    }

    // ── multipass buffer passes ──
    // Buffer passes carry their own main() (no entry scaffold, no param preamble)
    // and bake on the daemon Qt-RHI path, same as overlay packs. The compositor
    // runtime executes them via the GL-FBO chain; both share this source.
    std::vector<CompositorStage> bufferStages;
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
            QString rawBuf;
            if (!readStageSource(out, label, buf, rawBuf)) {
                ++errors;
                continue;
            }
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
            if (std::optional<CompositorStage> stage =
                    bakeCompositorStage(out, errors, label, PhosphorShaders::ShaderStage::Fragment, rawBuf, buf,
                                        bufferIncludePaths, CompositorAssembly{})) {
                bufferStages.push_back(std::move(*stage));
            }
        }
    }

    // A declared vertex stage that compiled for the compositor, and whether one
    // was declared and failed to, for the main fragment's link below.
    DeclaredVertex declaredVert;

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
        if (QString rawVert; !vertPath.isEmpty() && QFile::exists(vertPath)) {
            const QString label = QFileInfo(vertPath).fileName();
            if (!readStageSource(out, label, vertPath, rawVert)) {
                ++errors;
                // An unreadable declared stage: the compositor falls back to its
                // built-in one, which the link below then uses.
            } else {
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
                // The COMPOSITOR branch, for a DECLARED vertex stage ONLY. The
                // shared surface.vert fallback resolved above is deliberately not
                // baked here: the compositor never resolves that file (it supplies
                // its own built-in stage), and the shared one reads qt_Matrix,
                // which is a daemon UBO member and undeclared under
                // PLASMAZONES_KWIN. Baking it would fail every bundled pack on a
                // stage that never runs there.
                if (vertPath == eff.vertexShaderPath) {
                    declaredVert =
                        bakeDeclaredVertex(out, errors, label, rawVert, vertPath, includePaths, CompositorAssembly{});
                }
            }
        }
    }

    // ── links ──
    // The main fragment against the vertex stage the compositor pairs it with:
    // the declared one when it compiled, the built-in one when the compositor
    // would have fallen back to it (no declared stage, or one it could not read
    // or expand), and none when the declared stage failed to compile, since the
    // program then fails live. The buffer passes always link against the
    // built-in fullscreen quad, whatever the pack declares.
    if (mainFrag) {
        const std::optional<CompositorStage> vertex = linkedVertex(
            declaredVert,
            builtinCompositorStage(QStringLiteral("the built-in vertex stage"), PhosphorShaders::ShaderStage::Vertex,
                                   SurfaceShaderRegistry::compositorVertexSource()));
        if (vertex) {
            errors += reportCompositorLink(out, *vertex, *mainFrag);
        }
    }
    for (const CompositorStage& buffer : bufferStages) {
        errors += reportCompositorLink(out,
                                       builtinCompositorStage(QStringLiteral("the built-in buffer vertex stage"),
                                                              PhosphorShaders::ShaderStage::Vertex,
                                                              SurfaceShaderRegistry::compositorBufferVertexSource()),
                                       buffer);
    }

    if (errors == 0) {
        out << "  → OK\n\n";
    } else {
        out << "  → " << errors << (errors == 1 ? " error\n\n" : " errors\n\n");
    }
    return errors;
}

} // namespace PlasmaZones::ShaderValidate
