// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "colorimporter.h"
#include "../core/constants.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace PlasmaZones {

ColorImportResult ColorImporter::importFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ColorImportResult result;
        result.errorMessage = QObject::tr("Could not open file: %1").arg(filePath);
        return result;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();

    if (content.isEmpty()) {
        ColorImportResult result;
        result.errorMessage = QObject::tr("File is empty: %1").arg(filePath);
        return result;
    }

    // Try JSON format first (pywal)
    if (filePath.endsWith(QStringLiteral(".json"))) {
        ColorImportResult result = parsePywalJson(content);
        if (result.success) {
            return result;
        }
        // If JSON parsing failed, return that error (don't fall back for .json files)
        return result;
    }

    // Fall back to simple color list for non-JSON files
    return parseColorList(content);
}

ColorImportResult ColorImporter::parsePywalJson(const QString& content)
{
    ColorImportResult result;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (doc.isNull()) {
        result.errorMessage = QObject::tr("Invalid JSON: %1").arg(parseError.errorString());
        return result;
    }

    QJsonObject root = doc.object();

    // Pywal can store colors either in a "colors" object or at the root level
    QJsonObject colors = root[JsonKeys::Colors].toObject();
    if (colors.isEmpty()) {
        // Try root level (some pywal versions)
        colors = root;
    }

    if (colors.isEmpty()) {
        result.errorMessage = QObject::tr("No colors found in JSON file");
        return result;
    }

    // Extract pywal colors - color4 (accent), color0 (background), color7 (foreground)
    QString color4Str = colors[QStringLiteral("color4")].toString();
    QString color0Str = colors[QStringLiteral("color0")].toString();
    QString color7Str = colors[QStringLiteral("color7")].toString();

    if (color4Str.isEmpty() || color0Str.isEmpty() || color7Str.isEmpty()) {
        result.errorMessage = QObject::tr("Missing required colors (color0, color4, color7) in pywal file");
        return result;
    }

    QColor accent(color4Str);
    QColor bg(color0Str);
    QColor fg(color7Str);

    if (!accent.isValid() || !bg.isValid() || !fg.isValid()) {
        result.errorMessage = QObject::tr("Invalid color values in pywal file");
        return result;
    }

    // Apply standard alpha values
    accent.setAlpha(Defaults::HighlightAlpha);
    bg.setAlpha(Defaults::InactiveAlpha);
    fg.setAlpha(Defaults::BorderAlpha);

    result.success = true;
    result.highlightColor = accent;
    result.inactiveColor = bg;
    result.borderColor = fg;
    result.numberColor = fg;  // Same as border but without modified alpha
    result.numberColor.setAlpha(255);

    return result;
}

ColorImportResult ColorImporter::parseColorList(const QString& content)
{
    ColorImportResult result;

    QStringList lines = content.split(QRegularExpression(QStringLiteral("[\r\n]+")));
    // Filter out empty lines
    lines.removeAll(QString());

    if (lines.size() < 8) {
        result.errorMessage = QObject::tr("Color file needs at least 8 colors (found %1)").arg(lines.size());
        return result;
    }

    QColor accent(lines[4].trimmed());
    QColor bg(lines[0].trimmed());
    QColor fg(lines[7].trimmed());

    if (!accent.isValid() || !bg.isValid() || !fg.isValid()) {
        result.errorMessage = QObject::tr("Invalid color format in color list file");
        return result;
    }

    // Apply standard alpha values
    accent.setAlpha(Defaults::HighlightAlpha);
    bg.setAlpha(Defaults::InactiveAlpha);
    fg.setAlpha(Defaults::BorderAlpha);

    result.success = true;
    result.highlightColor = accent;
    result.inactiveColor = bg;
    result.borderColor = fg;
    result.numberColor = fg;
    result.numberColor.setAlpha(255);

    return result;
}

} // namespace PlasmaZones
