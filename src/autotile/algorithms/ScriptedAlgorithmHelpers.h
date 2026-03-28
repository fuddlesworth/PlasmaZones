// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJSValue>
#include <QRect>
#include <QString>
#include <QVector>

namespace PlasmaZones {
namespace ScriptedHelpers {

/**
 * @brief Parsed script metadata from // @key value comment lines
 */
struct ScriptMetadata
{
    QString name;
    QString description;
    QString zoneNumberDisplay;
    qreal defaultSplitRatio = 0.0; ///< 0.0 = unset, falls back to algorithm default
    int defaultMaxWindows = 0; ///< 0 = unset, falls back to algorithm default
    int minimumWindows = 0; ///< 0 = unset, falls back to algorithm default
    int masterZoneIndex = -1;
    bool supportsMasterCount = false;
    bool supportsSplitRatio = false;
    bool supportsMemory = false;
    bool producesOverlappingZones = false;
    bool centerLayout = false;
    QString builtinId; ///< Optional: register as built-in algorithm ID instead of "script:filename"
};

/**
 * @brief Parse // @key value metadata comments from script source
 * @param source Script source code
 * @param filePath File path for diagnostic messages
 * @return Parsed metadata struct
 */
ScriptMetadata parseMetadata(const QString& source, const QString& filePath);

/**
 * @brief JS source for the applyTreeGeometry(node, rect, gap) built-in helper
 */
QString treeHelperJs();

/**
 * @brief JS source for the lShapeLayout(...) built-in helper
 */
QString lShapeHelperJs();

/**
 * @brief JS source for the deckLayout(area, count, focusedFraction, horizontal) built-in helper
 */
QString deckHelperJs();

/**
 * @brief JS source for the distributeEvenly(start, total, count, gap) built-in helper
 *
 * Returns an array of {pos, size} objects distributing items evenly with
 * the last item filling the remainder to avoid rounding gaps.
 */
QString distributeEvenlyHelperJs();

/**
 * @brief Convert a JS array of {x, y, width, height} objects to QRects
 * @param result JS value (should be an array)
 * @param scriptId Script identifier for warning messages
 * @param maxZones Maximum number of zones to accept
 * @return Vector of validated QRects
 */
QVector<QRect> jsArrayToRects(const QJSValue& result, const QString& scriptId, int maxZones);

/**
 * @brief Clamp zones to the given area, using full area as fallback for zones entirely outside
 * @param zones Input zones from JS
 * @param area Bounding area to clamp to
 * @param scriptId Script identifier for warning messages
 * @return Vector of clamped QRects
 */
QVector<QRect> clampZonesToArea(const QVector<QRect>& zones, const QRect& area, const QString& scriptId);

} // namespace ScriptedHelpers
} // namespace PlasmaZones
