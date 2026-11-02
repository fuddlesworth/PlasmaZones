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
inline QRect absoluteVirtualScreenRect(const QRectF& region, const QRect& physical)
{
    const int left = physical.x() + qRound(region.x() * physical.width());
    const int top = physical.y() + qRound(region.y() * physical.height());
    const int right = physical.x() + qRound((region.x() + region.width()) * physical.width());
    const int bottom = physical.y() + qRound((region.y() + region.height()) * physical.height());
    return QRect(left, top, right - left, bottom - top);
}

/// A virtual screen's size in device pixels: @p region's share of an output
/// whose current mode is @p physicalPixelSize (already rotated, as KWin's
/// LogicalOutput::pixelSize reports it). The edges are rounded over the pixel
/// rect exactly as the daemon's VirtualScreenDef::absoluteGeometry rounds them,
/// so the screen-shape rule fields read the same size in both processes. An
/// empty @p physicalPixelSize yields an empty size.
inline QSize virtualScreenPixelSize(const QRectF& region, const QSize& physicalPixelSize)
{
    return absoluteVirtualScreenRect(region, QRect(QPoint(), physicalPixelSize)).size();
}

} // namespace PlasmaZones
