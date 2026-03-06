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

inline QString serializeGeometryMap(const QHash<QString, QRect>& map)
{
    QJsonObject result;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        result[Utils::extractAppId(it.key())] = rectToJsonObject(it.value());
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

} // namespace WindowTrackingInternal
} // namespace PlasmaZones
