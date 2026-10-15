// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QGuiApplication>
#include <QLatin1String>
#include <QScreen>
#include <QString>
#include <QUuid>
#include <limits>
#include <optional>

namespace PhosphorPlacement {

/// The KWin desktop-filter rule shared by every desktop-scoped placement query:
/// a filter of 0 (or negative) disables filtering entirely, and a window desktop
/// of 0 means "on all desktops" (sticky) and passes every filter; otherwise the
/// window's desktop must equal the filter. Centralised so the sticky-0 semantics
/// are spelled exactly once instead of re-derived at each query site.
inline bool desktopMatchesFilter(int windowDesktop, int desktopFilter)
{
    return desktopFilter <= 0 || windowDesktop == 0 || windowDesktop == desktopFilter;
}

/// Whether a store keyed under @p keyActivity belongs to the activity in view.
/// An empty key activity (no activities, a keyless walk) is every activity's.
inline bool activityInView(const QString& keyActivity, const QString& currentActivity)
{
    return keyActivity.isEmpty() || keyActivity == currentActivity;
}

inline std::optional<QUuid> parseUuid(const QString& str)
{
    if (str.isEmpty()) {
        return std::nullopt;
    }
    QUuid uuid = QUuid::fromString(str);
    return uuid.isNull() ? std::nullopt : std::optional<QUuid>(uuid);
}

/// Squared distance from @p point to the nearest point of @p rect; 0 inside it.
inline qint64 squaredDistanceToRect(const QPoint& point, const QRect& rect)
{
    const qint64 dx =
        point.x() < rect.left() ? rect.left() - point.x() : (point.x() > rect.right() ? point.x() - rect.right() : 0);
    const qint64 dy =
        point.y() < rect.top() ? rect.top() - point.y() : (point.y() > rect.bottom() ? point.y() - rect.bottom() : 0);
    return dx * dx + dy * dy;
}

/// @p geometry moved (never resized) so it lies inside @p bounds, its top-left edge winning when it is larger.
inline QRect clampToRect(const QRect& geometry, const QRect& bounds)
{
    QRect adjusted = geometry;
    if (adjusted.right() > bounds.right()) {
        adjusted.moveRight(bounds.right());
    }
    if (adjusted.left() < bounds.left()) {
        adjusted.moveLeft(bounds.left());
    }
    if (adjusted.bottom() > bounds.bottom()) {
        adjusted.moveBottom(bounds.bottom());
    }
    if (adjusted.top() < bounds.top()) {
        adjusted.moveTop(bounds.top());
    }
    return adjusted;
}

inline QScreen* findNearestScreen(const QPoint& point)
{
    QScreen* nearest = nullptr;
    qint64 minDist = std::numeric_limits<qint64>::max();
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        const qint64 dist = squaredDistanceToRect(point, screen->geometry());
        if (dist < minDist) {
            minDist = dist;
            nearest = screen;
        }
    }
    return nearest ? nearest : QGuiApplication::primaryScreen();
}

} // namespace PhosphorPlacement
