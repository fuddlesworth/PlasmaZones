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
static const QRegularExpression includeRegex(
    QStringLiteral("^\\s*#include\\s+([\"<])([^\">]+)[\">]\\s*$"));

QString tryReadFile(const QString& path, QString* outError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (outError) {
            *outError = QStringLiteral("Cannot open include: ") + path + QStringLiteral(" (")
                + file.errorString() + QLatin1Char(')');
        }
        return QString();
    }
    return QTextStream(&file).readAll();
}

QString expandIncludesRecursive(const QString& source,
                                const QString& currentFileDir,
                                const QStringList& includePaths,
                                int depth,
                                QSet<QString>& seenCanonical,
                                QString* outError)
{
    if (depth > ShaderIncludeResolver::MaxIncludeDepth) {
        if (outError) {
            *outError = QStringLiteral("Include depth exceeded (max %1)")
                            .arg(ShaderIncludeResolver::MaxIncludeDepth);
        }
        return QString();
    }

    QString result;
    const QStringList lines = source.split(QLatin1Char('\n'));

    for (const QString& line : lines) {
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

        QString newCurrentDir = QFileInfo(resolvedPath).absolutePath();
        QString expanded = expandIncludesRecursive(included, newCurrentDir, includePaths,
                                                   depth + 1, seenCanonical, outError);
        if (expanded.isNull()) {
            return QString();
        }
        seenCanonical.remove(resolvedPath);

        result += expanded;
        if (!expanded.endsWith(QLatin1Char('\n'))) {
            result += QLatin1Char('\n');
        }
    }

    return result;
}

} // namespace

QString ShaderIncludeResolver::expandIncludes(const QString& source,
                                              const QString& currentFileDir,
                                              const QStringList& includePaths,
                                              QString* outError)
{
    if (outError) {
        outError->clear();
    }
    QSet<QString> seenCanonical;
    return expandIncludesRecursive(source, currentFileDir, includePaths, 0, seenCanonical, outError);
}

} // namespace PlasmaZones
