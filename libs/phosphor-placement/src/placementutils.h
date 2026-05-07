// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <QUuid>
#include <optional>

namespace PhosphorPlacement {

inline constexpr QLatin1String kZoneSelectorIdPrefix{"zone-selector:"};

inline std::optional<QUuid> parseUuid(const QString& str)
{
    if (str.isEmpty()) {
        return std::nullopt;
    }
    QUuid uuid = QUuid::fromString(str);
    return uuid.isNull() ? std::nullopt : std::optional<QUuid>(uuid);
}

inline QScreen* findNearestScreen(const QPoint& point)
{
    QScreen* nearest = nullptr;
    int minDist = INT_MAX;
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        QRect geom = screen->geometry();
        int dx = 0, dy = 0;
        if (point.x() < geom.left())
            dx = geom.left() - point.x();
        else if (point.x() > geom.right())
            dx = point.x() - geom.right();
        if (point.y() < geom.top())
            dy = geom.top() - point.y();
        else if (point.y() > geom.bottom())
            dy = point.y() - geom.bottom();
        int dist = dx * dx + dy * dy;
        if (dist < minDist) {
            minDist = dist;
            nearest = screen;
        }
    }
    return nearest ? nearest : QGuiApplication::primaryScreen();
}

} // namespace PhosphorPlacement
