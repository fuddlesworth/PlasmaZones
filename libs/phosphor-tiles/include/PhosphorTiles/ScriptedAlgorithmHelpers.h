// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/AlgorithmMetadata.h>

#include <QJSValue>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace PhosphorTiles {
namespace ScriptedHelpers {

/**
 * @brief Definition of a custom algorithm parameter
 *
 * Declared by scripts via a JS-exported `customParams` array:
 *   var customParams = [
 *       { name: "gap", type: "number", default: 8, min: 0, max: 50, description: "Gap size" },
 *       { name: "wrap", type: "bool", default: true, description: "Wrap around" },
 *       { name: "mode", type: "enum", default: "auto", options: ["auto","manual"], description: "..." }
 *   ];
 */
struct CustomParamDef
{
    QString name; ///< Parameter name (camelCase, used as key in params.custom)
    QString type; ///< "number", "bool", or "enum"
    QVariant defaultValue; ///< Default value (qreal for number, bool for bool, QString for enum)
    QString description; ///< Human-readable description for settings UI
    qreal minValue = 0.0; ///< Minimum for number type (0.0 if unset)
    qreal maxValue = 1.0; ///< Maximum for number type (1.0 if unset)
    QStringList enumOptions; ///< Valid options for enum type

    /// Convert to QVariantMap for QML consumption (name, type, defaultValue, description, etc.)
    QVariantMap toVariantMap() const
    {
        QVariantMap m;
        m[QLatin1String("name")] = name;
        m[QLatin1String("type")] = type;
        m[QLatin1String("defaultValue")] = defaultValue;
        m[QLatin1String("description")] = description;
        if (type == QLatin1String("number")) {
            m[QLatin1String("minValue")] = minValue;
            m[QLatin1String("maxValue")] = maxValue;
        } else if (type == QLatin1String("enum")) {
            m[QLatin1String("enumOptions")] = QVariant(enumOptions);
        }
        return m;
    }
};

/**
 * @brief Parsed script metadata from // @key value comment lines
 */
struct ScriptMetadata
{
    QString name;
    QString description;
    PhosphorLayout::ZoneNumberDisplay zoneNumberDisplay = PhosphorLayout::ZoneNumberDisplay::RendererDecides;
    qreal defaultSplitRatio = 0.0; ///< 0.0 = unset, falls back to algorithm default
    int defaultMaxWindows = 0; ///< 0 = unset, falls back to algorithm default
    int minimumWindows = 0; ///< 0 = unset, falls back to algorithm default
    int masterZoneIndex = -1;
    bool supportsMasterCount = false;
    bool supportsSplitRatio = false;
    bool supportsMemory = false;
    bool producesOverlappingZones = false;
    bool centerLayout = false;
    bool supportsMinSizes = true; ///< Default true — most algorithms support min sizes
    QString builtinId; ///< Optional: register as built-in algorithm ID instead of "script:filename"
    QVector<CustomParamDef> customParams; ///< Algorithm-declared custom parameters
};

/**
 * @brief Parse // @key value metadata comments from script source
 * @param source Script source code
 * @param filePath File path for diagnostic messages
 * @return Parsed metadata struct
 */
ScriptMetadata parseMetadata(const QString& source, const QString& filePath);

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
} // namespace PhosphorTiles
