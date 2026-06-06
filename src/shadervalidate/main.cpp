// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plasmazones-shader-validate — offline validator for zone shader packs (T1.2).
//
// A `luau-analyze` equivalent for GLSL packs: it parses metadata.json with the
// SAME parser the daemon uses (ShaderRegistry::parsePackMetadata, T1.1 auto-slot
// included), reproduces the runtime's GLSL assembly (entry scaffold + generated
// pz_<id> preamble + include expansion), and bakes every stage through headless
// glslang (QShaderBaker, CPU-only — no GPU, no Wayland, no compositor). It is the
// CI gate for the bundled set and a pre-commit-friendly tool for pack authors.
//
// Usage:
//   plasmazones-shader-validate <path> [<path> ...]
// where each <path> is either a pack directory (contains metadata.json) or a
// root that holds pack subdirectories. Exits non-zero if any pack has an error.

#include "../daemon/rendering/zoneentryscaffold.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

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

using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;

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

// Cheap Levenshtein for the "did you mean" hint on an undeclared pz_ symbol.
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

// If the glslang diagnostic names an undeclared `pz_<x>` and a declared param is
// a near match, surface the suggestion — the friction T1.2 exists to remove.
void appendDidYouMean(QTextStream& out, const QString& diagnostic, const ShaderRegistry::ShaderInfo& info)
{
    static const QRegularExpression re(QStringLiteral("'(pz_[A-Za-z0-9_]+)' : undeclared identifier"));
    QStringList declared;
    for (const ShaderRegistry::ParameterInfo& p : info.parameters) {
        declared << QStringLiteral("pz_") + p.id;
    }
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

// Compile one stage through the exact runtime assembly and print OK/ERROR.
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

    // effect.frag goes through the entry scaffold (which may wrap a pzZone/pzImage
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
    // The pz_<id> preamble is spliced only into the scaffolded main fragment, the
    // single stage that reads parameters by name.
    if (useScaffold && !preamble.isEmpty()) {
        expanded = PhosphorShaders::spliceAfterVersion(expanded, preamble);
    }

    const ShaderCompiler::Result result = ShaderCompiler::compile(expanded.toUtf8(), stage);
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
    appendDidYouMean(out, result.error, info);
    return 1;
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
        // An id that isn't a valid GLSL identifier gets no pz_ define and no lane
        // (parseShaderMetadata leaves its slot at -1) — surface the real fault, and
        // skip the collision check so two such params don't false-collide on "-1".
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no pz_ define)")
                         .arg(p.id);
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
        errors += compileStage(out, QStringLiteral("effect.frag"), info.sourcePath, QShader::FragmentStage,
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

bool isPackDir(const QString& dir)
{
    return QFile::exists(QDir(dir).filePath(QStringLiteral("metadata.json")));
}

} // namespace

int main(int argc, char** argv)
{
    QTextStream out(stdout);
    QTextStream errStream(stderr);

    QStringList args;
    bool quiet = false; // --quiet/-q: print only failing packs (clean pre-commit output)
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--quiet") || a == QLatin1String("-q")) {
            quiet = true;
        } else {
            args << a;
        }
    }
    if (args.isEmpty()) {
        errStream << "usage: plasmazones-shader-validate [--quiet] <pack-dir-or-root> [...]\n";
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

    int totalErrors = 0;
    int failedPacks = 0;
    for (const QString& pack : packs) {
        QString report;
        QTextStream reportStream(&report);
        const int e = validatePack(pack, reportStream);
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
