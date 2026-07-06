// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plasmazones-shader-validate — offline validator for shader packs (T1.2).
//
// A `luau-analyze` equivalent for GLSL packs: it parses metadata.json with the
// SAME parser the runtime uses, reproduces the runtime's GLSL assembly (entry
// scaffold + generated p_<id> preamble + include expansion), and bakes every
// stage through headless glslang (QShaderBaker, CPU-only — no GPU, no Wayland,
// no compositor). It is the CI gate for the bundled sets and a pre-commit-
// friendly tool for pack authors.
//
// Three authoring models, selected by --overlay (default) / --animation /
// --surface:
//   • zone/overlay packs (--overlay, the default, data/shaders/*):
//     ShaderRegistry::parsePackMetadata + the zone entry scaffold (pZone/pImage);
//     validates the frag, multipass buffer passes, and the vertex stage on the
//     daemon Qt-RHI path.
//   • animation/transition packs (--animation, data/animations/*):
//     AnimationShaderEffect + the animation entry scaffold (pTransition / pIn+pOut)
//     + paramPreamble; validates effect.frag on the daemon Qt-RHI path. (The
//     kwin-effect classic-GL branch is not baked — see validateAnimationPack.)
//   • surface/decoration packs (--surface, data/surface/*):
//     SurfaceShaderEffect + paramPreamble; validates effect.frag, buffer
//     passes, and the shared vertex stage on the daemon Qt-RHI path — see
//     validateSurfacePack.
//
// Usage:
//   plasmazones-shader-validate [--quiet] [--overlay|--animation|--surface] <path> [<path> ...]
// where each <path> is either a pack directory (contains metadata.json) or a
// root that holds pack subdirectories. Exits non-zero if any pack has an error.

#include "../daemon/rendering/zoneentryscaffold.h"

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
#include <QRegularExpression>
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

namespace {

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

// Report a compiled stage's outcome: "OK", or "ERROR" with the glslang
// diagnostics mapped to the author's file/line (T1.3 #line) plus the
// did-you-mean hint. @p declared is the list of generated `p_<id>` names.
// Returns 1 on failure, 0 on success. Shared by the zone and animation paths.
int reportCompile(QTextStream& out, const QString& label, const ShaderCompiler::Result& result,
                  const QStringList& declared)
{
    if (result.success) {
        out << "  " << label.leftJustified(14) << "OK\n";
        return 0;
    }
    out << "  " << label.leftJustified(14) << "ERROR\n";
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

// Compile one ZONE stage through the exact runtime assembly and print OK/ERROR.
// Returns 1 on failure, 0 on success.
int compileStage(QTextStream& out, const QString& label, const QString& path, QShader::Stage stage,
                 const QStringList& includePaths, bool useScaffold, const QString& preamble,
                 const ShaderRegistry::ShaderInfo& info)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out << "  " << label.leftJustified(14) << "ERROR\n    cannot read " << path << "\n";
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
        out << "  " << label.leftJustified(14) << "ERROR\n    include expansion failed: " << err << "\n";
        return 1;
    }
    // The p_<id> preamble is spliced only into the scaffolded main fragment, the
    // single stage that reads parameters by name.
    if (useScaffold && !preamble.isEmpty()) {
        expanded = PhosphorShaders::spliceAfterVersion(expanded, preamble);
    }

    const ShaderCompiler::Result result = ShaderCompiler::compile(expanded.toUtf8(), stage);
    return reportCompile(out, label, result, declaredParamNames(info.parameters));
}

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
            if (!QFile::exists(QDir(packDir).filePath(bufName))) {
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
// OpenGL-target compiler. The PLASMAZONES_KWIN uniform plumbing lives entirely
// in the shared animation_uniforms.glsl — identical for every pack — while each
// pack's authored body is fully covered by the daemon bake here.
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
    // the metadata `fragmentShader` field, resolved relative to the pack dir
    // (QDir::filePath returns an absolute input unchanged). The runtime does NOT
    // default to effect.frag — a pack that omits the field is unreachable, so an
    // empty path stays empty and is caught by isValid() below.
    if (!eff.fragmentShaderPath.isEmpty()) {
        eff.fragmentShaderPath = QDir(packDir).filePath(eff.fragmentShaderPath);
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
    if (QFile::exists(eff.fragmentShaderPath)) {
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
    // Mirror SurfaceShaderRegistry::parseEffect: the fragment path comes from the
    // metadata `fragmentShader` field, resolved relative to the pack dir.
    if (!eff.fragmentShaderPath.isEmpty()) {
        eff.fragmentShaderPath = QDir(packDir).filePath(eff.fragmentShaderPath);
    }
    // fromJson leaves buffer paths relative (the registry's parseEffect resolves
    // them); resolve here against the pack dir, same as fragmentShaderPath.
    for (QString& b : eff.bufferShaderPaths) {
        if (!b.isEmpty()) {
            b = QDir(packDir).filePath(b);
        }
    }
    // fromJson leaves an explicit `vertexShader` path relative too (the registry's
    // parseEffect resolves it, surfaceshaderregistry.cpp); resolve here against the
    // pack dir so the vertex stage below finds a custom vertex shader instead of
    // probing it against the CWD, missing it, and passing a malformed stage.
    if (!eff.vertexShaderPath.isEmpty()) {
        eff.vertexShaderPath = QDir(packDir).filePath(eff.vertexShaderPath);
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
        if (!wrap.isEmpty() && wrap != QLatin1String("clamp") && wrap != QLatin1String("repeat")
            && wrap != QLatin1String("mirror")) {
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
            if (!bufName.isEmpty() && !QFile::exists(QDir(packDir).filePath(bufName))) {
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
            const QStringList includePaths = {QFileInfo(packDir).absolutePath() + QStringLiteral("/shared")};
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
        const QStringList includePaths = {QFileInfo(packDir).absolutePath() + QStringLiteral("/shared")};
        for (const QString& buf : eff.bufferShaderPaths) {
            if (!QFile::exists(buf)) {
                continue; // missing buffers already linted above
            }
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
                ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), includePaths, &err);
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
        const QStringList includePaths = {QFileInfo(packDir).absolutePath() + QStringLiteral("/shared")};
        QString vertPath = eff.vertexShaderPath;
        if (vertPath.isEmpty()) {
            const QString vertLocal = QDir(packDir).filePath(QStringLiteral("surface.vert"));
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

bool isPackDir(const QString& dir)
{
    return QFile::exists(QDir(dir).filePath(QStringLiteral("metadata.json")));
}

// Write the generated `p_<id>` autocomplete sidecar for one pack (T2.2). The
// sidecar (p_generated.glsl) is an editor-only aid: an author #includes it for
// glslls / glsl-language-server autocomplete, and the include resolver skips it
// at load (ShaderIncludeResolver::GeneratedPreambleInclude), so it neither ships
// nor affects the compiled shader. Returns 0 on success, 1 on error.
int emitPreamble(const QString& packDir, bool animationMode, bool surfaceMode, bool quiet, QTextStream& out,
                 QTextStream& errStream)
{
    const QString name = QFileInfo(packDir).fileName();
    QString preamble;
    // glslls needs the UBO declarations the p_<id> defines reference, so the
    // sidecar pulls in the right base header per authoring model.
    QString baseHeader;

    if (surfaceMode) {
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (!metaFile.open(QIODevice::ReadOnly)) {
            errStream << name << ": cannot read metadata.json\n";
            return 1;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        if (!doc.isObject()) {
            errStream << name << ": invalid metadata.json\n";
            return 1;
        }
        SurfaceShaderEffect eff = SurfaceShaderEffect::fromJson(doc.object());
        eff.sourceDir = QDir(packDir).absolutePath();
        if (!eff.isValid()) {
            errStream << name << ": invalid metadata.json (missing required field id / fragmentShader)\n";
            return 1;
        }
        preamble = SurfaceShaderRegistry::paramPreamble(eff);
        baseHeader = QStringLiteral("surface_uniforms.glsl");
    } else if (animationMode) {
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (!metaFile.open(QIODevice::ReadOnly)) {
            errStream << name << ": cannot read metadata.json\n";
            return 1;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        if (!doc.isObject()) {
            errStream << name << ": invalid metadata.json\n";
            return 1;
        }
        AnimationShaderEffect eff = AnimationShaderEffect::fromJson(doc.object());
        eff.sourceDir = QDir(packDir).absolutePath();
        // Mirror the validate path: reject metadata missing the required id /
        // fragmentShader fields rather than silently emitting a sidecar from a
        // half-parsed pack. (isValid() checks field presence, not file
        // existence, so emit-preamble-before-writing-the-shader still works.)
        if (!eff.isValid()) {
            errStream << name << ": invalid metadata.json (missing required field id / fragmentShader)\n";
            return 1;
        }
        preamble = AnimationShaderRegistry::paramPreamble(eff);
        baseHeader = QStringLiteral("animation_uniforms.glsl");
    } else {
        QString parseErr;
        const ShaderRegistry::ShaderInfo info = ShaderRegistry::parsePackMetadata(packDir, &parseErr);
        if (!parseErr.isEmpty()) {
            errStream << name << ": " << parseErr << "\n";
            return 1;
        }
        preamble = ShaderRegistry::paramPreamble(info);
        baseHeader = QStringLiteral("common.glsl");
    }

    const QLatin1String sidecarName(ShaderIncludeResolver::GeneratedPreambleInclude);
    QString sidecar;
    QTextStream s(&sidecar);
    s << "// GENERATED by `plasmazones-shader-validate --emit-preamble` — DO NOT EDIT.\n"
      << "//\n"
      << "// Editor-only autocomplete aid for the `p_<id>` named parameters. Add\n"
      << "//     #include \"" << sidecarName << "\"\n"
      << "// near the top of your shader so glslls / glsl-language-server resolves\n"
      << "// p_<id>. The loader skips this include at runtime — the real preamble is\n"
      << "// generated then — so it neither ships nor affects the compiled shader.\n"
      << "// Re-run --emit-preamble after you change the pack's parameters.\n"
      << "#include <" << baseHeader << ">\n"
      << "\n"
      << (preamble.isEmpty() ? QStringLiteral("// (this pack declares no named parameters)\n") : preamble);
    s.flush();

    const QString sidecarPath = QDir(packDir).filePath(sidecarName);
    QFile f(sidecarPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errStream << name << ": cannot write " << sidecarPath << ": " << f.errorString() << "\n";
        return 1;
    }
    f.write(sidecar.toUtf8());
    if (!quiet) {
        out << "wrote " << sidecarPath << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QTextStream out(stdout);
    QTextStream errStream(stderr);

    QStringList args;
    bool quiet = false; // --quiet/-q: print only failing packs (clean pre-commit output)
    // System selection: --overlay (default) vs --animation. Two distinct
    // authoring models (different metadata schema + entry convention), so the
    // mode is explicit and symmetric; later flag wins if both are given.
    bool animationMode = false;
    // --surface: surface-layer packs (data/surface/*) — the window border /
    // rounded-corner category. Mutually exclusive with --overlay / --animation.
    bool surfaceMode = false;
    // --emit-preamble: don't validate — write each pack's `p_<id>` autocomplete
    // sidecar (T2.2) for editor tooling.
    bool emitMode = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--quiet") || a == QLatin1String("-q")) {
            quiet = true;
        } else if (a == QLatin1String("--animation") || a == QLatin1String("-a")) {
            animationMode = true;
            surfaceMode = false;
        } else if (a == QLatin1String("--surface") || a == QLatin1String("-s")) {
            surfaceMode = true;
            animationMode = false;
        } else if (a == QLatin1String("--overlay") || a == QLatin1String("-o")) {
            animationMode = false;
            surfaceMode = false;
        } else if (a == QLatin1String("--emit-preamble")) {
            emitMode = true;
        } else {
            args << a;
        }
    }
    if (args.isEmpty()) {
        errStream << "usage: plasmazones-shader-validate [--quiet] [--overlay|--animation|--surface] "
                     "[--emit-preamble] <pack-dir-or-root> [...]\n"
                  << "  --overlay         zone/overlay packs (data/shaders/*)        [default]\n"
                  << "  --animation       transition/animation packs (data/animations/*)\n"
                  << "  --surface         surface-layer packs (data/surface/*)\n"
                  << "  --emit-preamble   write each pack's p_generated.glsl autocomplete sidecar (no validation)\n";
        return 2;
    }

    // Expand each argument into a list of pack directories: a pack dir is taken as
    // itself; anything else is treated as a root and scanned one level deep.
    QStringList packs;
    for (const QString& arg : args) {
        const QFileInfo fi(arg);
        if (!fi.exists() || !fi.isDir()) {
            errStream << "not a directory: " << arg << "\n";
            return 2;
        }
        if (isPackDir(arg)) {
            packs << QDir(arg).absolutePath();
            continue;
        }
        const QStringList subdirs = QDir(arg).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& sub : subdirs) {
            const QString subPath = QDir(arg).filePath(sub);
            if (isPackDir(subPath)) {
                packs << QDir(subPath).absolutePath();
            }
        }
    }

    if (packs.isEmpty()) {
        errStream << "no shader packs (directories with metadata.json) found under the given path(s)\n";
        return 2;
    }

    // Emit mode: write each pack's autocomplete sidecar and exit (no validation).
    if (emitMode) {
        int failed = 0;
        for (const QString& pack : packs) {
            if (emitPreamble(pack, animationMode, surfaceMode, quiet, out, errStream) != 0) {
                ++failed;
            }
        }
        out.flush();
        if (failed > 0) {
            errStream << failed << " of " << packs.size() << " pack(s) failed.\n";
            return 1;
        }
        errStream << "wrote p_generated.glsl for " << packs.size() << " pack(s).\n";
        return 0;
    }

    int totalErrors = 0;
    int failedPacks = 0;
    for (const QString& pack : packs) {
        QString report;
        QTextStream reportStream(&report);
        const int e = surfaceMode ? validateSurfacePack(pack, reportStream)
            : animationMode       ? validateAnimationPack(pack, reportStream)
                                  : validatePack(pack, reportStream);
        reportStream.flush();
        totalErrors += e;
        if (e > 0) {
            ++failedPacks;
        }
        // In quiet mode only failing packs are printed — the rest is summarized.
        if (!quiet || e > 0) {
            out << report;
        }
    }
    out.flush();

    if (totalErrors == 0) {
        errStream << "All " << packs.size() << " pack(s) OK.\n";
        return 0;
    }
    errStream << failedPacks << " of " << packs.size() << " pack(s) failed (" << totalErrors << " error(s) total).\n";
    return 1;
}
