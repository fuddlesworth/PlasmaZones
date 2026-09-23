// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QGuiApplication>
#include <QLatin1String>
#include <QScreen>
#include <QString>
#include <QUuid>
#include <climits>
#include <optional>

namespace PhosphorPlacement {

inline constexpr QLatin1String kZoneSelectorIdPrefix{"zone-selector:"};

/// The KWin desktop-filter rule shared by every desktop-scoped placement query:
/// a filter of 0 (or negative) disables filtering entirely, and a window desktop
/// of 0 means "on all desktops" (sticky) and passes every filter; otherwise the
/// window's desktop must equal the filter. Centralised so the sticky-0 semantics
/// are spelled exactly once instead of re-derived at each query site.
inline bool desktopMatchesFilter(int windowDesktop, int desktopFilter)
{
    return desktopFilter <= 0 || windowDesktop == 0 || windowDesktop == desktopFilter;
}

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
