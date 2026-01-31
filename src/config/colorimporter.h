// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QColor>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Result of a color import operation
 *
 * Contains the extracted colors and success status.
 * All colors have appropriate alpha values pre-applied.
 */
struct PLASMAZONES_EXPORT ColorImportResult {
    bool success = false;
    QColor highlightColor;
    QColor inactiveColor;
    QColor borderColor;
    QColor numberColor;
};

/**
 * @brief Imports color schemes from external files
 *
 * Supports:
 * - Pywal colors.json format
 * - Simple color list (one hex color per line)
 *
 * Handles color file parsing and extraction.
 * It does not modify settings or emit signals.
 */
class PLASMAZONES_EXPORT ColorImporter
{
public:
    /**
     * @brief Import colors from a file
     * @param filePath Path to the color file (JSON or plain text)
     * @return ColorImportResult with success status and extracted colors
     */
    static ColorImportResult importFromFile(const QString& filePath);

private:
    /**
     * @brief Parse pywal JSON format
     * @param content File content as string
     * @return ColorImportResult (success=false if not valid pywal format)
     */
    static ColorImportResult parsePywalJson(const QString& content);

    /**
     * @brief Parse simple color list (one hex per line)
     * @param content File content as string
     * @return ColorImportResult (success=false if not enough valid colors)
     */
    static ColorImportResult parseColorList(const QString& content);
};

} // namespace PlasmaZones
