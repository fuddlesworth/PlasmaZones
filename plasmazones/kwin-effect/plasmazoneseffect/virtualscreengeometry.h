// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>

/// The absolute rect of a virtual screen, header-only so the rounding is
/// unit-testable without a compositor (same pattern as windowscreendecisions.h).
namespace PlasmaZones {

/// @p region is the virtual screen's fraction of its physical output (0..1, as
/// the daemon sends it) and @p physical the output's geometry in global
/// compositor coordinates. The edges are rounded and the size derived from
/// them, so two virtual screens that share an edge in fractions share it in
/// pixels too: rounding each size on its own left one-pixel gaps or overlaps.
///
/// The same arithmetic as the daemon's VirtualScreenDef::absoluteGeometry,
/// clamps included, so both processes cut a region alike. The daemon admits a
/// region that overshoots the output by its tolerance, and the clamps pull the
/// far edges back onto the output and the near edges onto its origin, and keep
/// each side at least one pixel.
inline QRect absoluteVirtualScreenRect(const QRectF& region, const QRect& physical)
{
    int left = physical.x() + qRound(region.x() * physical.width());
    int top = physical.y() + qRound(region.y() * physical.height());
    int right = physical.x() + qRound((region.x() + region.width()) * physical.width());
    int bottom = physical.y() + qRound((region.y() + region.height()) * physical.height());
    right = qMin(right, physical.x() + physical.width());
    bottom = qMin(bottom, physical.y() + physical.height());
    left = qMin(left, right - 1);
    top = qMin(top, bottom - 1);
    left = qMax(left, physical.x());
    top = qMax(top, physical.y());
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top));
}

/// Whether @p region is one the daemon would hold: the region half of
/// VirtualScreenDef::isValid, with its tolerance. The origin may sit a
/// tolerance before the output and the far edges a tolerance past it, and both
/// sides must be positive. absoluteVirtualScreenRect floors every side at one
/// pixel, so the rect it cuts cannot tell a malformed region from a sliver,
/// and only the region can.
inline bool virtualScreenRegionIsValid(const QRectF& region)
{
    constexpr qreal Tolerance = 1e-3;
    return region.x() >= -Tolerance && region.y() >= -Tolerance && region.width() > 0 && region.height() > 0
        && region.x() + region.width() <= 1.0 + Tolerance && region.y() + region.height() <= 1.0 + Tolerance;
}

/// A virtual screen's size in device pixels: @p region's share of an output
/// whose current mode is @p physicalPixelSize (already rotated, as KWin's
/// LogicalOutput::pixelSize reports it), cut over the pixel rect by
/// absoluteVirtualScreenRect, so the screen-shape rule fields read the same
/// size the daemon's VirtualScreenDef::absoluteGeometry gives. An empty
/// @p physicalPixelSize yields an empty size, where the one-pixel floor would
/// otherwise make up a 1x1 screen.
inline QSize virtualScreenPixelSize(const QRectF& region, const QSize& physicalPixelSize)
{
    if (physicalPixelSize.isEmpty()) {
        return QSize();
    }
    return absoluteVirtualScreenRect(region, QRect(QPoint(), physicalPixelSize)).size();
}

} // namespace PlasmaZones
