// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include <PhosphorLayoutApi/AlgorithmMetadata.h>

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace PhosphorTiles {
namespace ScriptedHelpers {

/**
 * @brief Definition of a custom algorithm parameter, declared by a script's
 *        `metadata.customParams` list.
 */
struct CustomParamDef
{
    QString name; ///< Parameter name (camelCase, used as key in ctx.custom)
    QString type; ///< "number", "bool", or "enum"
    QVariant defaultValue; ///< Default value
    QString description; ///< Human-readable description for settings UI
    qreal minValue = 0.0; ///< Minimum for number type
    qreal maxValue = 1.0; ///< Maximum for number type
    QStringList enumOptions; ///< Valid options for enum type

    /// Convert to QVariantMap for QML consumption.
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
 * @brief Parsed metadata from a scripted algorithm's `metadata` table.
 */
struct ScriptMetadata
{
    QString name;
    QString description;
    PhosphorLayout::ZoneNumberDisplay zoneNumberDisplay = PhosphorLayout::ZoneNumberDisplay::RendererDecides;
    qreal defaultSplitRatio = 0.0; ///< 0.0 = unset, falls back to algorithm default
    int defaultMaxWindows = 0; ///< 0 = unset
    int minimumWindows = 0; ///< 0 = unset
    int masterZoneIndex = -1;
    bool supportsMasterCount = false;
    bool supportsSplitRatio = false;
    bool supportsMemory = false;
    bool supportsScriptState = false; ///< Persists an opaque ctx.state bag across retiles (non-tree memory)
    bool producesOverlappingZones = false;
    bool centerLayout = false;
    bool supportsSingleWindow = false; ///< Owns the single-window case (else calculateZones() fills the work area)
    bool supportsMinSizes = true; ///< Default true — most algorithms support min sizes
    QString id; ///< Optional algorithm id (else "script:filename")
    QVector<CustomParamDef> customParams;
};

/**
 * @brief Clamp zones to the given area, using the full area as a fallback for
 *        zones that fall entirely outside it. Engine-agnostic.
 */
PHOSPHORTILES_EXPORT QVector<QRect> clampZonesToArea(const QVector<QRect>& zones, const QRect& area,
                                                     const QString& scriptId);

} // namespace ScriptedHelpers
} // namespace PhosphorTiles
