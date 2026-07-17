// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the shader-pack validators — see packvalidatorcommon.h.

#include "packvalidatorcommon.h"

#include "../daemon/rendering/zoneentryscaffold.h"

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;
using PhosphorSurfaceShaders::SurfaceShaderEffect;

namespace PlasmaZones::ShaderValidate {

std::optional<QString> confinedPackPath(const QString& packDir, const QString& rel)
{
    if (rel.isEmpty()) {
        return std::nullopt;
    }
    const QString lexicalRoot = QDir::cleanPath(QDir(packDir).absolutePath()) + QStringLiteral("/");
    const QString abs = QDir::cleanPath(QDir(packDir).filePath(rel));
    const QString canonicalRoot = QFileInfo(QDir(packDir).absolutePath()).canonicalFilePath();
    const QString canonicalSelf = QFileInfo(abs).canonicalFilePath();
    const bool useCanonical = !canonicalRoot.isEmpty() && !canonicalSelf.isEmpty();
    const QString candidate = useCanonical ? canonicalSelf : abs;
    const QString comparisonRoot = useCanonical ? (canonicalRoot + QStringLiteral("/")) : lexicalRoot;
    if (!candidate.startsWith(comparisonRoot)) {
        return std::nullopt;
    }
    return abs;
}

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

} // namespace PlasmaZones::ShaderValidate
