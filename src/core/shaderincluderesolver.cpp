// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderincluderesolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace PlasmaZones {

namespace {

// Match #include "path" or #include <path> (optional whitespace)
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
    return QTextStream(&file).readAll();
}

QString expandIncludesRecursive(const QString& source, const QString& sourceFilename,
                                const QString& currentFileDir, const QStringList& includePaths,
                                int depth, QSet<QString>& seenCanonical, QString* outError)
{
    if (depth > ShaderIncludeResolver::MaxIncludeDepth) {
        if (outError) {
            *outError = QStringLiteral("Include depth exceeded (max %1)").arg(ShaderIncludeResolver::MaxIncludeDepth);
        }
        return QString();
    }

    QString result;
    const QStringList lines = source.split(QLatin1Char('\n'));
    int lineNumber = 0;

    for (const QString& line : lines) {
        ++lineNumber;

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

        QStringList searchDirs;
        if (quote == QLatin1Char('"')) {
            searchDirs.append(currentFileDir);
        }
        searchDirs.append(includePaths);

        QString resolvedPath;
        for (const QString& dir : searchDirs) {
            if (dir.isEmpty()) {
                continue;
            }
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
            // Circular include: skip (do not expand again) to avoid infinite loop
            result += QLatin1String("// [include skipped: circular] ") + line + QLatin1Char('\n');
            continue;
        }
        seenCanonical.insert(resolvedPath);

        QString included = tryReadFile(resolvedPath, outError);
        if (outError && !outError->isEmpty()) {
            return QString();
        }

        const QString includeFilename = QFileInfo(resolvedPath).fileName();
        const QString newCurrentDir = QFileInfo(resolvedPath).absolutePath();
        QString expanded = expandIncludesRecursive(
            included, includeFilename, newCurrentDir, includePaths, depth + 1, seenCanonical, outError);
        if (expanded.isNull()) {
            return QString();
        }
        seenCanonical.remove(resolvedPath);

        // Emit #line directives so GLSL errors reference original source files.
        // #line sets the line number for the NEXT line of source.
        result += QStringLiteral("#line 1 // %1\n").arg(includeFilename);
        result += expanded;
        if (!expanded.endsWith(QLatin1Char('\n'))) {
            result += QLatin1Char('\n');
        }
        // Restore parent file's line counter after the include.
        // lineNumber+1 because #line sets the number for the NEXT line.
        result += QStringLiteral("#line %1 // %2\n").arg(lineNumber + 1).arg(sourceFilename);
    }

    return result;
}

} // namespace

QString ShaderIncludeResolver::expandIncludes(const QString& source, const QString& currentFileDir,
                                              const QStringList& includePaths, QString* outError,
                                              const QString& sourceFilename)
{
    if (outError) {
        outError->clear();
    }
    QSet<QString> seenCanonical;
    const QString filename = sourceFilename.isEmpty() ? QStringLiteral("main") : sourceFilename;
    return expandIncludesRecursive(source, filename, currentFileDir, includePaths, 0, seenCanonical, outError);
}

} // namespace PlasmaZones
