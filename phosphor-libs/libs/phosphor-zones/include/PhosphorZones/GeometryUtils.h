// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorGeometry/GeometryUtils.h>
#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/EdgeGaps.h>

#include <QRect>
#include <QRectF>
#include <QString>
#include <QVariantMap>

class QScreen;

namespace PhosphorZones {
class Layout;
class Zone;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorZones {
namespace GeometryUtils {

using PhosphorGeometry::availableAreaToOverlayCoordinates;
using PhosphorGeometry::clampZonesToScreen;
using PhosphorGeometry::enforceMinSizes;
using PhosphorGeometry::rectToJson;
using PhosphorGeometry::removeRectOverlaps;
using PhosphorGeometry::snapToRect;

PHOSPHORZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);

PHOSPHORZONES_EXPORT QRectF getZoneGeometryWithGaps(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, int innerGap,
                                                    const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                    bool useAvailableGeometry = true, const QString& screenId = {});

PHOSPHORZONES_EXPORT QRectF getZoneGeometryWithGaps(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    const QRect& screenGeometry, const QRect& availableGeometry,
                                                    int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                    bool useAvailableGeometry = true, const QString& screenId = {});

PHOSPHORZONES_EXPORT QRect getZoneGeometryForScreen(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, const QString& screenId,
                                                    PhosphorZones::Layout* layout, int zonePadding,
                                                    const ::PhosphorLayout::EdgeGaps& outerGaps);

PHOSPHORZONES_EXPORT QRectF getZoneGeometryForScreenF(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                      QScreen* screen, const QString& screenId,
                                                      PhosphorZones::Layout* layout, int zonePadding,
                                                      const ::PhosphorLayout::EdgeGaps& outerGaps);

/// The logical scale of the output @p screenId lives on (a virtual screen
/// answers for its physical output), falling back to @p screen when the id does
/// not resolve. Zero when there is no manager, or when neither the id nor
/// @p screen resolves, and every geometry entry point reads zero as "the
/// layout's last recalculation scale". This is the scale a layout whose fixed
/// geometry counts device pixels divides by.
PHOSPHORZONES_EXPORT qreal screenScale(PhosphorScreens::ScreenManager* mgr, const QString& screenId,
                                       QScreen* screen = nullptr);

PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                    QScreen* screen);

/// The rect @p layout resolves against on @p screenId: its full or available
/// geometry as the manager reports it. An id the manager cannot resolve falls
/// back to the QScreen it names, except a virtual-screen id, which answers an
/// empty rect, the same rule getZoneGeometryForScreenF follows.
PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                    const QString& screenId);

PHOSPHORZONES_EXPORT QRectF extractZoneGeometry(const QVariantMap& zone);
PHOSPHORZONES_EXPORT void setZoneGeometry(QVariantMap& zone, const QRectF& rect);

} // namespace GeometryUtils
} // namespace PhosphorZones
