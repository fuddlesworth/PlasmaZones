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
        return ColorImportResult{};
    }

    QTextStream stream(&file);
    QString content = stream.readAll();

    // Try JSON format first (pywal)
    if (filePath.endsWith(QStringLiteral(".json"))) {
        ColorImportResult result = parsePywalJson(content);
        if (result.success) {
            return result;
        }
    }

    // Fall back to simple color list
    return parseColorList(content);
}

ColorImportResult ColorImporter::parsePywalJson(const QString& content)
{
    ColorImportResult result;

    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (doc.isNull()) {
        return result;
    }

    QJsonObject colors = doc.object()[JsonKeys::Colors].toObject();
    if (colors.isEmpty()) {
        return result;
    }

    // Extract pywal colors
    QColor accent(colors[QStringLiteral("color4")].toString());
    QColor bg(colors[QStringLiteral("color0")].toString());
    QColor fg(colors[QStringLiteral("color7")].toString());

    if (!accent.isValid() || !bg.isValid() || !fg.isValid()) {
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
    if (lines.size() < 8) {
        return result;
    }

    QColor accent(lines[4].trimmed());
    QColor bg(lines[0].trimmed());
    QColor fg(lines[7].trimmed());

    if (!accent.isValid() || !bg.isValid() || !fg.isValid()) {
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
