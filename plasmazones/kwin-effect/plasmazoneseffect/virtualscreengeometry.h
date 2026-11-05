// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QList>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>

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

/// The daemon's tolerance for a region's float noise, VirtualScreenDef::Tolerance.
inline constexpr qreal VIRTUAL_SCREEN_TOLERANCE = 1e-3;

/// Whether @p region passes the region half of VirtualScreenDef::isValid, with
/// its tolerance. The origin may sit a tolerance before the output and the far
/// edges a tolerance past it, and both sides must be positive.
/// absoluteVirtualScreenRect floors every side at one pixel, so the rect it cuts
/// cannot tell a malformed region from a sliver, and only the region can.
inline bool virtualScreenRegionIsValid(const QRectF& region)
{
    return region.x() >= -VIRTUAL_SCREEN_TOLERANCE && region.y() >= -VIRTUAL_SCREEN_TOLERANCE && region.width() > 0
        && region.height() > 0 && region.x() + region.width() <= 1.0 + VIRTUAL_SCREEN_TOLERANCE
        && region.y() + region.height() <= 1.0 + VIRTUAL_SCREEN_TOLERANCE;
}

/// Whether a virtual screen with @p id and @p region passes
/// VirtualScreenDef::isValid on the two fields that decide it from a
/// getVirtualScreenConfig reply: a virtual id (a non-empty physical id before
/// the separator) and a valid region. The physical id isValid also wants is the
/// one the effect asked for, and the index is virtualScreenConfigIsAdmitted's.
inline bool virtualScreenDefIsAdmitted(const QString& id, const QRectF& region)
{
    return PhosphorIdentity::VirtualScreenId::isVirtual(id) && virtualScreenRegionIsValid(region);
}

/// One virtual screen as getVirtualScreenConfig sends it.
struct VirtualScreenReply
{
    QString id;
    int index = -1;
    QRectF region;
};

/// Whether @p screens, the reply for @p physicalScreenId, is a config the daemon
/// would hold: VirtualScreenConfig::isValid over what the reply carries. No
/// screens passes, as the daemon's removal does. Otherwise at least two, each
/// admitted by virtualScreenDefIsAdmitted with a non-negative index and the id
/// VirtualScreenId::make gives for that index, no id or index twice, no two
/// regions overlapping by more than the tolerance on both axes, and areas that
/// sum to the output within the tolerance. The daemon's cap on screens per
/// output is not in the reply and is not checked.
///
/// All or nothing, like the daemon: one bad screen refuses the whole config,
/// because the rest of it no longer tiles the output.
inline bool virtualScreenConfigIsAdmitted(const QString& physicalScreenId, const QList<VirtualScreenReply>& screens)
{
    if (screens.isEmpty()) {
        return true;
    }
    if (screens.size() < 2) {
        return false;
    }
    QSet<QString> seenIds;
    QSet<int> seenIndices;
    for (const VirtualScreenReply& screen : screens) {
        if (!virtualScreenDefIsAdmitted(screen.id, screen.region) || screen.index < 0 || physicalScreenId.isEmpty()) {
            return false;
        }
        if (screen.id != PhosphorIdentity::VirtualScreenId::make(physicalScreenId, screen.index)) {
            return false;
        }
        if (seenIds.contains(screen.id) || seenIndices.contains(screen.index)) {
            return false;
        }
        seenIds.insert(screen.id);
        seenIndices.insert(screen.index);
    }
    for (int i = 0; i < screens.size(); ++i) {
        for (int j = i + 1; j < screens.size(); ++j) {
            const QRectF intersection = screens[i].region.intersected(screens[j].region);
            if (intersection.width() > VIRTUAL_SCREEN_TOLERANCE && intersection.height() > VIRTUAL_SCREEN_TOLERANCE) {
                return false;
            }
        }
    }
    qreal totalArea = 0.0;
    for (const VirtualScreenReply& screen : screens) {
        totalArea += screen.region.width() * screen.region.height();
    }
    return totalArea >= 1.0 - VIRTUAL_SCREEN_TOLERANCE && totalArea <= 1.0 + VIRTUAL_SCREEN_TOLERANCE;
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
