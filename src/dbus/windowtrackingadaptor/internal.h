// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRect>
#include <QHash>
#include <QString>
#include "../../core/utils.h"
#include "../../core/windowtrackingservice.h"

namespace PlasmaZones {
namespace WindowTrackingInternal {

inline QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray arr;
    for (const QString& s : list) {
        arr.append(s);
    }
    return arr;
}

inline QJsonObject rectToJsonObject(const QRect& rect)
{
    QJsonObject obj;
    obj[QLatin1String("x")] = rect.x();
    obj[QLatin1String("y")] = rect.y();
    obj[QLatin1String("width")] = rect.width();
    obj[QLatin1String("height")] = rect.height();
    return obj;
}

inline QString serializeGeometryMap(const QHash<QString, WindowTrackingService::PreTileGeometry>& map)
{
    QJsonObject result;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        QJsonObject obj = rectToJsonObject(it.value().geometry);
        if (!it.value().connectorName.isEmpty()) {
            obj[QLatin1String("screen")] = Utils::screenIdForName(it.value().connectorName);
        }
        result[Utils::extractAppId(it.key())] = obj;
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

/**
 * Serialize geometry map preserving full windowId keys.
 * Returns a JSON array of {windowId, x, y, width, height, screen} objects.
 * Used for daemon-only restarts where KWin UUIDs are stable, so
 * multi-instance apps keep per-window pre-tile geometry.
 */
inline QString serializeGeometryMapFull(const QHash<QString, WindowTrackingService::PreTileGeometry>& map)
{
    QJsonArray result;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        // Only serialize full windowId entries (contain '|'), skip appId-only duplicates
        if (!it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        QJsonObject obj;
        obj[QLatin1String("windowId")] = it.key();
        obj[QLatin1String("x")] = it.value().geometry.x();
        obj[QLatin1String("y")] = it.value().geometry.y();
        obj[QLatin1String("width")] = it.value().geometry.width();
        obj[QLatin1String("height")] = it.value().geometry.height();
        if (!it.value().connectorName.isEmpty()) {
            obj[QLatin1String("screen")] = Utils::screenIdForName(it.value().connectorName);
        }
        result.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

} // namespace WindowTrackingInternal
} // namespace PlasmaZones
