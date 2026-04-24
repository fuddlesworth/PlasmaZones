// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngineApi/GeometryUtils.h>
#include <PhosphorEngineApi/JsonKeys.h>
#include <PhosphorEngineApi/EngineTypes.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>

namespace PhosphorEngineApi {
namespace GeometryUtils {

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }
    return PhosphorGeometry::availableAreaToOverlayCoordinates(geometry, screen->geometry());
}

QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries)
{
    if (entries.isEmpty()) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    for (const ZoneAssignmentEntry& entry : entries) {
        QJsonObject obj;
        obj[JsonKeys::WindowId] = entry.windowId;
        obj[JsonKeys::SourceZoneId] = entry.sourceZoneId;
        obj[JsonKeys::TargetZoneId] = entry.targetZoneId;
        if (!entry.targetZoneIds.isEmpty()) {
            QJsonArray zoneIdsArr;
            for (const QString& zid : entry.targetZoneIds)
                zoneIdsArr.append(zid);
            obj[JsonKeys::TargetZoneIds] = zoneIdsArr;
        }
        obj[JsonKeys::X] = entry.targetGeometry.x();
        obj[JsonKeys::Y] = entry.targetGeometry.y();
        obj[JsonKeys::Width] = entry.targetGeometry.width();
        obj[JsonKeys::Height] = entry.targetGeometry.height();
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace GeometryUtils
} // namespace PhosphorEngineApi
