// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include "zone.h"
#include "constants.h"
#include "screenmanager.h"
#include <QScreen>

namespace PlasmaZones {

namespace GeometryUtils {

QRectF calculateZoneGeometry(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    const QRectF screenGeom = screen->geometry();
    return zone->calculateAbsoluteGeometry(screenGeom);
}

QRectF clipZoneToAvailableArea(Zone* zone, QScreen* screen)
{
    return clipZoneToAvailableArea(zone, screen, Defaults::MinimumZoneDisplaySizePx);
}

QRectF clipZoneToAvailableArea(Zone* zone, QScreen* screen, int minSize)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Calculate absolute zone geometry
    QRectF absGeom = calculateZoneGeometry(zone, screen);

    // Get available geometry (excludes panels/docks)
    // Uses actualAvailableGeometry which queries PlasmaShell on Wayland for accurate panel info
    const QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);

    // Intersect zone with available geometry (clip to visible area)
    QRectF availableRect(availableGeom);
    QRectF clippedGeom = absGeom.intersected(availableRect);

    // Ensure the clipped geometry is valid (has reasonable size)
    if (clippedGeom.isEmpty() || clippedGeom.width() < minSize || clippedGeom.height() < minSize) {
        // Zone is completely outside or too small in available area
        // Hide it by setting empty size
        clippedGeom = QRectF(availableRect.topLeft(), QSizeF(0, 0));
    }

    return clippedGeom;
}

QRectF toOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }

    // The overlay window is positioned at screenGeom.topLeft(),
    // so subtract screen origin to get local coordinates
    const QRectF screenGeom = screen->geometry();
    return QRectF(geometry.x() - screenGeom.x(), geometry.y() - screenGeom.y(), geometry.width(), geometry.height());
}

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }

    // The overlay window covers the full screen (geometry()),
    // but zones are calculated relative to availableGeometry.
    // The geometry parameter is already in absolute screen coordinates
    // (calculated from availableGeometry), so we just need to convert
    // to overlay-local coordinates by subtracting the full screen origin.
    const QRectF screenGeom = screen->geometry();
    return QRectF(geometry.x() - screenGeom.x(), geometry.y() - screenGeom.y(), geometry.width(), geometry.height());
}

QRectF calculateZoneGeometryInAvailableArea(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use actualAvailableGeometry which excludes panels/taskbars (queries PlasmaShell on Wayland)
    const QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);
    return zone->calculateAbsoluteGeometry(availableGeom);
}

QRectF getZoneGeometryWithSpacing(Zone* zone, QScreen* screen, int spacing, bool useAvailableGeometry)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use available geometry (excludes panels/taskbars) or full screen geometry
    QRectF geom;
    if (useAvailableGeometry) {
        geom = calculateZoneGeometryInAvailableArea(zone, screen);
    } else {
        geom = calculateZoneGeometry(zone, screen);
    }

    // Apply zone spacing to all edges (including screen boundaries)
    if (spacing > 0) {
        geom = geom.adjusted(spacing / 2.0, spacing / 2.0, -spacing / 2.0, -spacing / 2.0);
    }

    return geom;
}

} // namespace GeometryUtils

} // namespace PlasmaZones
