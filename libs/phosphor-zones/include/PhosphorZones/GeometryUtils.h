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

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorZones {
namespace GeometryUtils {

using PhosphorGeometry::availableAreaToOverlayCoordinates;
using PhosphorGeometry::enforceWindowMinSizes;
using PhosphorGeometry::rectToJson;
using PhosphorGeometry::removeZoneOverlaps;
using PhosphorGeometry::snapToRect;

PHOSPHORZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);

PHOSPHORZONES_EXPORT QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, int innerGap,
                                                    const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                    bool useAvailableGeometry = true, const QString& screenId = {});

PHOSPHORZONES_EXPORT QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    const QRect& screenGeometry, const QRect& availableGeometry,
                                                    int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                    bool useAvailableGeometry = true, const QString& screenId = {});

PHOSPHORZONES_EXPORT QRect getZoneGeometryForScreen(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, const QString& screenId,
                                                    PhosphorZones::Layout* layout, int zonePadding,
                                                    const ::PhosphorLayout::EdgeGaps& outerGaps);

PHOSPHORZONES_EXPORT QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                      QScreen* screen, const QString& screenId,
                                                      PhosphorZones::Layout* layout, int zonePadding,
                                                      const ::PhosphorLayout::EdgeGaps& outerGaps);

PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, QScreen* screen);

PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, const QString& screenId);

PHOSPHORZONES_EXPORT QRectF extractZoneGeometry(const QVariantMap& zone);
PHOSPHORZONES_EXPORT void setZoneGeometry(QVariantMap& zone, const QRectF& rect);

} // namespace GeometryUtils
} // namespace PhosphorZones
