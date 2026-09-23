// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace PhosphorShaders {

namespace {

static const QRegularExpression includeRegex(QStringLiteral("^\\s*#include\\s+([\"<])([^\">]+)[\">]\\s*$"));

QString tryReadFile(const QString& path, QString* outError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) {
            *outError = QStringLiteral("Cannot open include: ") + path + QStringLiteral(" (") + file.errorString()
                + QLatin1Char(')');
        }
        return QString();
    }
    // Return a guaranteed non-null result on success (readAll() yields a null
    // QString for an empty file), so the caller distinguishes a genuine
    // open-failure (null) from a valid empty include (empty, non-null) WITHOUT
    // depending on whether an error sink was supplied.
    const QString content = QTextStream(&file).readAll();
    return content.isNull() ? QStringLiteral("") : content;
}

QString expandIncludesRecursive(const QString& source, const QString& currentFileDir, const QStringList& includePaths,
                                int depth, QSet<QString>& seenCanonical, QString* outError,
                                QStringList* outIncludedPaths, int thisSourceIndex, int& nextSourceIndex,
                                QStringList* outSourcePaths)
{
    if (depth > ShaderIncludeResolver::MaxIncludeDepth) {
        if (outError) {
            *outError = QStringLiteral("Include depth exceeded (max %1)").arg(ShaderIncludeResolver::MaxIncludeDepth);
        }
        return QString();
    }

    QString result;
    const QStringList lines = source.split(QLatin1Char('\n'));

    // `li` is the 0-based index into `lines`; source line number is `li + 1`.
    for (int li = 0; li < lines.size(); ++li) {
        const QString& line = lines.at(li);
        // Cheap reject before the regex: the vast majority of shader lines are
        // not #include directives, and QRegularExpression::match() allocates a
        // fresh pcre2 match-data block per call. A substring pre-check skips the
        // matcher for ~all non-include lines, cutting the dominant source of
        // temporary allocations during shader warm-bake. The regex still does
        // the authoritative validation for any line that passes this guard.
        if (!line.contains(QLatin1String("#include"))) {
            result += line + QLatin1Char('\n');
            continue;
        }
        QRegularExpressionMatch match = includeRegex.match(line);
        if (!match.hasMatch()) {
            result += line + QLatin1Char('\n');
            continue;
        }

        const QChar quote = match.capturedView(1).at(0);
        const QString includeName = match.captured(2).trimmed();
        if (includeName.isEmpty()) {
            if (outError) {
                *outError = QStringLiteral("Empty include path");
            }
            return QString();
        }

        // The generated `p_<id>` autocomplete sidecar is an editor-only aid (the
        // real preamble is spliced at load), so a reference to it is a no-op
        // here — skip it by reserved basename rather than resolving or erroring.
        // One comment line preserves the parent's line numbering, mirroring the
        // circular-skip below. See ShaderIncludeResolver::GeneratedPreambleInclude.
        if (QFileInfo(includeName).fileName() == QLatin1String(ShaderIncludeResolver::GeneratedPreambleInclude)) {
            result += QLatin1String("// [include skipped: generated preamble] ") + line + QLatin1Char('\n');
            continue;
        }

        QStringList searchDirs;
        if (quote == QLatin1Char('"')) {
            searchDirs.append(currentFileDir);
        }
        searchDirs.append(includePaths);

        QString resolvedPath;
        for (const QString& dir : searchDirs) {
            if (dir.isEmpty())
                continue;
            QString candidate = QDir(dir).absoluteFilePath(includeName);
            QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                resolvedPath = info.canonicalFilePath();
                break;
            }
        }

        if (resolvedPath.isEmpty()) {
            if (outError) {
                *outError = QStringLiteral("Include not found: ") + includeName;
            }
            return QString();
        }

        if (seenCanonical.contains(resolvedPath)) {
            // A circular skip emits one comment line in place of one #include
            // line, so the parent's natural line numbering is preserved — no
            // #line fixup needed here.
            result += QLatin1String("// [include skipped: circular] ") + line + QLatin1Char('\n');
            continue;
        }
        seenCanonical.insert(resolvedPath);

        // Each textual inclusion gets its own GLSL source-string number so the
        // #line directives below let glslang (and the GL driver on the
        // kwin-effect path) attribute diagnostics to the included file rather
        // than the flattened blob. Source string 0 is the top-level shader;
        // each include takes the next number in resolution order, in lockstep
        // with the outSourcePaths legend and outIncludedPaths.
        const int childSourceIndex = ++nextSourceIndex;
        if (outIncludedPaths) {
            // Record the resolved canonical path even if the file
            // turns out to be unreadable below — caller fingerprinting
            // needs to track every path the resolver attempted, so a
            // deleted-include-file doesn't quietly produce the same
            // fingerprint as an unchanged build.
            outIncludedPaths->append(resolvedPath);
        }
        if (outSourcePaths) {
            while (outSourcePaths->size() <= childSourceIndex) {
                outSourcePaths->append(QString());
            }
            (*outSourcePaths)[childSourceIndex] = resolvedPath;
        }

        QString included = tryReadFile(resolvedPath, outError);
        // Null = open-failure (the file resolved on disk but couldn't be read).
        // Guard on the result itself, not on outError, so the failure isn't
        // swallowed — silently inlining an empty include — when no error sink was
        // passed (expandIncludes defaults outError to nullptr).
        if (included.isNull())
            return QString();

        QString newCurrentDir = QFileInfo(resolvedPath).absolutePath();
        QString expanded =
            expandIncludesRecursive(included, newCurrentDir, includePaths, depth + 1, seenCanonical, outError,
                                    outIncludedPaths, childSourceIndex, nextSourceIndex, outSourcePaths);
        if (expanded.isNull())
            return QString();
        seenCanonical.remove(resolvedPath);

        // Bracket the inlined include with #line directives:
        //   • `#line 1 <child>`     — the include's first line is line 1 of its
        //                             own source string.
        //   • `#line <li+2> <this>` — the parent resumes at the line *after* the
        //                             #include directive. The directive is the
        //                             1-based source line `li+1`, so the next
        //                             parent line is `li+2`.
        // Integer source-string numbers (not filenames) keep this portable
        // across glslang and the GL drivers the kwin-effect path compiles
        // through; callers map the number back to a path via outSourcePaths.
        // These directives only ever follow earlier source lines (every shader
        // opens with its own `#version`, and includes come after), so no #line
        // is ever emitted before `#version`.
        result += QStringLiteral("#line 1 %1\n").arg(childSourceIndex);
        result += expanded;
        if (!expanded.endsWith(QLatin1Char('\n')))
            result += QLatin1Char('\n');
        result += QStringLiteral("#line %1 %2\n").arg(li + 2).arg(thisSourceIndex);
    }

    return result;
}

} // namespace

QString ShaderIncludeResolver::expandIncludes(const QString& source, const QString& currentFileDir,
                                              const QStringList& includePaths, QString* outError,
                                              QStringList* outIncludedPaths, QStringList* outSourcePaths)
{
    if (outError)
        outError->clear();
    if (outSourcePaths) {
        // Index 0 is the top-level source string. The resolver isn't told the
        // top-level file's path (only its directory), so it's seeded empty; the
        // caller fills it. Includes append at indices ≥ 1.
        outSourcePaths->clear();
        outSourcePaths->append(QString());
    }
    QSet<QString> seenCanonical;
    int nextSourceIndex = 0; // top-level source string is 0; first include becomes 1
    return expandIncludesRecursive(source, currentFileDir, includePaths, 0, seenCanonical, outError, outIncludedPaths,
                                   /*thisSourceIndex=*/0, nextSourceIndex, outSourcePaths);
}

} // namespace PhosphorShaders
