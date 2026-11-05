// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/JsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace PhosphorEngine {
namespace GeometryUtils {

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
        // Carry the authoritative target screen when the producer stamped one.
        // Without this key the receive side re-derives the screen from
        // geometry.center(), which races with VS swap/rotate — the exact
        // re-derivation the producers' targetScreenId stamps exist to avoid.
        if (!entry.targetScreenId.isEmpty()) {
            obj[JsonKeys::TargetScreenId] = entry.targetScreenId;
        }
        if (entry.virtualDesktop > 0) {
            obj[JsonKeys::VirtualDesktop] = entry.virtualDesktop;
        }
        if (entry.restatement) {
            obj[JsonKeys::Restatement] = true;
        }
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<ZoneAssignmentEntry> deserializeZoneAssignments(const QString& json, QString* errorString)
{
    if (errorString) {
        errorString->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (errorString) {
            *errorString = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                        : QStringLiteral("payload is not a JSON array");
        }
        return {};
    }

    QVector<ZoneAssignmentEntry> entries;
    const QJsonArray arr = doc.array();
    entries.reserve(arr.size());
    for (const QJsonValue& val : arr) {
        const QJsonObject obj = val.toObject();
        ZoneAssignmentEntry entry;
        entry.windowId = obj.value(JsonKeys::WindowId).toString();
        entry.targetZoneId = obj.value(JsonKeys::TargetZoneId).toString();
        entry.sourceZoneId = obj.value(JsonKeys::SourceZoneId).toString();
        const QJsonArray zoneIdsArr = obj.value(JsonKeys::TargetZoneIds).toArray();
        for (const QJsonValue& v : zoneIdsArr) {
            entry.targetZoneIds.append(v.toString());
        }
        entry.targetGeometry = QRect(obj.value(JsonKeys::X).toInt(), obj.value(JsonKeys::Y).toInt(),
                                     obj.value(JsonKeys::Width).toInt(), obj.value(JsonKeys::Height).toInt());
        // Missing key (producer had no authoritative screen) parses to empty,
        // which keeps the historical geometry-derived resolution on receive.
        entry.targetScreenId = obj.value(JsonKeys::TargetScreenId).toString();
        // Boundary hygiene: a missing key parses to 0 (current-desktop default);
        // clamp a nonsensical negative wire value to the same default so no
        // negative desktop ever reaches the commit path.
        entry.virtualDesktop = qMax(0, obj.value(JsonKeys::VirtualDesktop).toInt());
        entry.restatement = obj.value(JsonKeys::Restatement).toBool();
        // A span is the primary followed by the rest, as the serializer writes
        // it. An empty member, or a span another zone leads, would commit a
        // zone the entry does not name (F459).
        const bool spanValid = entry.targetZoneIds.isEmpty()
            || (entry.targetZoneIds.first() == entry.targetZoneId
                && std::none_of(entry.targetZoneIds.cbegin(), entry.targetZoneIds.cend(), [](const QString& id) {
                       return id.isEmpty();
                   }));
        if (!entry.windowId.isEmpty() && !entry.targetZoneId.isEmpty() && spanValid) {
            entries.append(entry);
        }
    }
    return entries;
}

QRect carryRectOntoArea(const QRect& rect, const QRect& fromArea, const QRect& toArea)
{
    const int w = qMin(rect.width(), toArea.width());
    const int h = qMin(rect.height(), toArea.height());
    int x = toArea.x();
    int y = toArea.y();
    if (fromArea.isValid() && fromArea.width() > 0 && fromArea.height() > 0) {
        const double relX = static_cast<double>(rect.x() - fromArea.x()) / fromArea.width();
        const double relY = static_cast<double>(rect.y() - fromArea.y()) / fromArea.height();
        x = toArea.x() + qRound(relX * toArea.width());
        y = toArea.y() + qRound(relY * toArea.height());
    }
    x = qBound(toArea.left(), x, toArea.right() - w + 1);
    y = qBound(toArea.top(), y, toArea.bottom() - h + 1);
    return QRect(x, y, w, h);
}

} // namespace GeometryUtils
} // namespace PhosphorEngine
