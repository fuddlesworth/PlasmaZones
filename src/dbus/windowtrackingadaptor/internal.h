// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRect>
#include <QHash>
#include <QString>
#include "../../core/constants.h"
#include "../../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
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
    obj[::PhosphorZones::ZoneJsonKeys::X] = rect.x();
    obj[::PhosphorZones::ZoneJsonKeys::Y] = rect.y();
    obj[::PhosphorZones::ZoneJsonKeys::Width] = rect.width();
    obj[::PhosphorZones::ZoneJsonKeys::Height] = rect.height();
    return obj;
}

inline QString serializeGeometryMap(const QHash<QString, WindowTrackingService::PreTileGeometry>& map,
                                    const WindowTrackingService* service)
{
    // Runtime map keys are either bare appIds (stable cross-session keys) or
    // full "appId|uuid" instance keys (per-runtime data). The output format is
    // appId-keyed for cross-session restore, so multiple runtime entries can
    // collide onto the same output slot.
    //
    // Two-pass to guarantee a fresh per-instance capture wins over any stale
    // appId-only entry left over from a prior session:
    //   pass 1 — seed the result from appId-only entries (the cross-session
    //            inheritance baseline)
    //   pass 2 — overwrite with per-instance entries so the freshest captured
    //            value for each appId wins
    //
    // Without the split, QHash iteration order decided the winner and a ghost
    // full-windowId entry from a prior session could overwrite the fresh one.
    auto serialize = [](const WindowTrackingService::PreTileGeometry& entry) -> QJsonObject {
        QJsonObject obj = rectToJsonObject(entry.geometry);
        if (!entry.connectorName.isEmpty()) {
            obj[QLatin1String("screen")] = PhosphorIdentity::VirtualScreenId::isVirtual(entry.connectorName)
                ? entry.connectorName
                : Utils::screenIdForName(entry.connectorName);
        }
        return obj;
    };

    QJsonObject result;
    // Pass 1: appId-only keys
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        const QString appId = service ? service->currentAppIdFor(it.key()) : it.key();
        if (appId.isEmpty()) {
            continue;
        }
        result[appId] = serialize(it.value());
    }
    // Pass 2: per-instance keys (these win over pass 1's cross-session baseline)
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (!it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        const QString appId = service ? service->currentAppIdFor(it.key()) : it.key();
        if (appId.isEmpty()) {
            continue;
        }
        result[appId] = serialize(it.value());
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
        obj[::PhosphorZones::ZoneJsonKeys::X] = it.value().geometry.x();
        obj[::PhosphorZones::ZoneJsonKeys::Y] = it.value().geometry.y();
        obj[::PhosphorZones::ZoneJsonKeys::Width] = it.value().geometry.width();
        obj[::PhosphorZones::ZoneJsonKeys::Height] = it.value().geometry.height();
        if (!it.value().connectorName.isEmpty()) {
            obj[QLatin1String("screen")] = PhosphorIdentity::VirtualScreenId::isVirtual(it.value().connectorName)
                ? it.value().connectorName
                : Utils::screenIdForName(it.value().connectorName);
        }
        result.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

} // namespace WindowTrackingInternal
} // namespace PlasmaZones
