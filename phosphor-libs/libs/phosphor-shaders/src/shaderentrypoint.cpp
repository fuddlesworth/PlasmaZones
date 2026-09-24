// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderEntryPoint.h>

#include <QRegularExpression>

namespace PhosphorShaders {

QString stripGlslComments(const QString& source)
{
    QString out;
    out.reserve(source.size());
    const int n = source.size();
    int i = 0;
    while (i < n) {
        const QChar c = source.at(i);
        // Line comment: drop everything up to (but not including) the newline,
        // which the outer loop copies so line numbering is preserved.
        if (c == QLatin1Char('/') && i + 1 < n && source.at(i + 1) == QLatin1Char('/')) {
            i += 2;
            while (i < n && source.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            continue;
        }
        // Block comment: drop the body but re-emit each contained newline so
        // the stripped source keeps the same line count as the original.
        if (c == QLatin1Char('/') && i + 1 < n && source.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(source.at(i) == QLatin1Char('*') && source.at(i + 1) == QLatin1Char('/'))) {
                if (source.at(i) == QLatin1Char('\n')) {
                    out.append(QLatin1Char('\n'));
                }
                ++i;
            }
            i += 2; // skip the closing */ (overshoot on an unterminated comment is harmless)
            continue;
        }
        out.append(c);
        ++i;
    }
    return out;
}

namespace {
// Whole-word `<name> ( ... ) {` — matches a definition, not a call
// (`return pZone(z);`). GLSL params contain no parens, so `[^)]*` safely spans a
// multi-line parameter list; `\b` stops `pZone2` matching `pZone`. Shared by
// definesFunction and composeEntryPoint so the entry-detection shape can't drift.
QRegularExpression entryDefinitionRegex(const QString& name)
{
    return QRegularExpression(QStringLiteral("\\b") + QRegularExpression::escape(name)
                              + QStringLiteral("\\s*\\([^)]*\\)\\s*\\{"));
}
} // namespace

bool definesMain(const QString& expandedSource)
{
    // `void main()` or `void main(void)`, K&R or same-line brace. The trailing
    // `{` requires a definition, not a declaration / stray identifier.
    static const QRegularExpression re(QStringLiteral("\\bvoid\\s+main\\s*\\(\\s*(?:void\\s*)?\\)\\s*\\{"));
    return re.match(stripGlslComments(expandedSource)).hasMatch();
}

bool definesFunction(const QString& expandedSource, const QString& functionName)
{
    if (functionName.isEmpty()) {
        return false;
    }
    return entryDefinitionRegex(functionName).match(stripGlslComments(expandedSource)).hasMatch();
}

QString composeEntryPoint(const QString& expandedSource, const QList<EntryCandidate>& candidates)
{
    // Author-supplied main() wins — the escape hatch every current pack uses.
    if (definesMain(expandedSource)) {
        return expandedSource;
    }
    // Strip once; every candidate probe reads the same comment-free source.
    const QString stripped = stripGlslComments(expandedSource);
    const auto definesIn = [&stripped](const QString& name) {
        if (name.isEmpty()) {
            return false;
        }
        return entryDefinitionRegex(name).match(stripped).hasMatch();
    };
    for (const EntryCandidate& cand : candidates) {
        if (!definesIn(cand.functionName)) {
            continue;
        }
        // A direction-dispatched pair (pIn requires pOut) only matches when
        // every required companion is also defined — a lone pIn falls through.
        bool allPresent = true;
        for (const QString& req : cand.alsoRequires) {
            if (!definesIn(req)) {
                allPresent = false;
                break;
            }
        }
        if (!allPresent) {
            continue;
        }
        QString out = expandedSource;
        if (!out.endsWith(QLatin1Char('\n'))) {
            out.append(QLatin1Char('\n'));
        }
        out.append(cand.generatedMain);
        if (!out.endsWith(QLatin1Char('\n'))) {
            out.append(QLatin1Char('\n'));
        }
        return out;
    }
    // No main(), no recognised entry — return unchanged; the compiler's
    // missing-main() error (mapped via the resolver's #line legend) is the
    // right diagnostic, not a silent rewrite.
    return expandedSource;
}

QString assembleEntryPoint(const QString& raw, const QString& prologue, const QList<EntryCandidate>& candidates)
{
    if (candidates.isEmpty() || definesMain(raw)) {
        return raw;
    }
    return composeEntryPoint(prologue + raw, candidates);
}

} // namespace PhosphorShaders
