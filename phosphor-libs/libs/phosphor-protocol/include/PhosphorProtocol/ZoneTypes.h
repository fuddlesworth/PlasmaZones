// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>

namespace PhosphorProtocol {

/// D-Bus struct for zone geometry: (iiii)
struct ZoneGeometryRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    static ZoneGeometryRect fromRect(const QRect& r)
    {
        return {r.x(), r.y(), r.width(), r.height()};
    }
};

using ZoneGeometryList = QList<ZoneGeometryRect>;

/// D-Bus struct for empty zone info: (siiiiiibsssdd)
struct EmptyZoneEntry
{
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int borderWidth = 0;
    int borderRadius = 0;
    bool useCustomColors = false;
    QString highlightColor;
    QString inactiveColor;
    QString borderColor;
    double activeOpacity = 0.5;
    double inactiveOpacity = 0.3;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using EmptyZoneList = QList<EmptyZoneEntry>;

/// D-Bus struct for snap assist candidate: (ssss)
struct SnapAssistCandidate
{
    QString windowId;
    QString compositorHandle;
    QString icon;
    QString caption;
};

using SnapAssistCandidateList = QList<SnapAssistCandidate>;

/// D-Bus struct for named zone geometry: (siiii)
struct NamedZoneGeometry
{
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using NamedZoneGeometryList = QList<NamedZoneGeometry>;

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::ZoneGeometryRect)
Q_DECLARE_METATYPE(PhosphorProtocol::ZoneGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::EmptyZoneEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::EmptyZoneList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAssistCandidate)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAssistCandidateList)
Q_DECLARE_METATYPE(PhosphorProtocol::NamedZoneGeometry)
Q_DECLARE_METATYPE(PhosphorProtocol::NamedZoneGeometryList)
