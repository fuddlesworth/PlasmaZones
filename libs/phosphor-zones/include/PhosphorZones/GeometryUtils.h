// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/EngineTypes.h>
#include <PhosphorEngineApi/IGeometrySettings.h>
#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/EdgeGaps.h>

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

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

PHOSPHORZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);
PHOSPHORZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry);

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
                                                    PhosphorZones::Layout* layout,
                                                    PhosphorEngineApi::IGeometrySettings* settings);

PHOSPHORZONES_EXPORT QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                      QScreen* screen, const QString& screenId,
                                                      PhosphorZones::Layout* layout,
                                                      PhosphorEngineApi::IGeometrySettings* settings);

PHOSPHORZONES_EXPORT int getEffectiveZonePadding(PhosphorZones::Layout* layout,
                                                 PhosphorEngineApi::IGeometrySettings* settings,
                                                 const QString& screenId = {});

PHOSPHORZONES_EXPORT QRect snapToRect(const QRectF& rf);

PHOSPHORZONES_EXPORT ::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout,
                                                                      PhosphorEngineApi::IGeometrySettings* settings,
                                                                      const QString& screenId = {});

PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, QScreen* screen);

PHOSPHORZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, const QString& screenId);

PHOSPHORZONES_EXPORT QRectF extractZoneGeometry(const QVariantMap& zone);
PHOSPHORZONES_EXPORT void setZoneGeometry(QVariantMap& zone, const QRectF& rect);

PHOSPHORZONES_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold,
                                                int innerGap = 0);

PHOSPHORZONES_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                             int innerGap = 0);

PHOSPHORZONES_EXPORT QString rectToJson(const QRect& rect);

PHOSPHORZONES_EXPORT QString serializeZoneAssignments(const QVector<PhosphorEngineApi::ZoneAssignmentEntry>& entries);

} // namespace GeometryUtils
} // namespace PhosphorZones
