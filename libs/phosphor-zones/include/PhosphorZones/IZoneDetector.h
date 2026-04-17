// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// IZoneDetector + ZoneDetectionResult — the zone-detection contract.
//
// Split out of interfaces.h so zones-related code can include just the
// detector interface without pulling ISettings / IOverlayService /
// ILayoutManager. Prerequisite for moving ZoneDetector + this interface
// into a standalone phosphor-zones library without dragging the rest of
// PZ's service interfaces along.

#include <phosphorzones_export.h>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace PlasmaZones {

class Zone;
class Layout;

/**
 * @brief Result of zone detection
 *
 * Defined here so interfaces can use it without circular dependencies.
 */
struct PHOSPHORZONES_EXPORT ZoneDetectionResult
{
    Zone* primaryZone = nullptr; // Main zone to snap to
    QVector<Zone*> adjacentZones; // Adjacent zones for multi-zone snap
    QVector<Zone*> overlappingZones; // All zones containing cursor point (overlap info)
    QRectF snapGeometry; // Combined geometry for snapping
    qreal distance = -1; // Distance to zone edge
    bool isMultiZone = false; // Whether snapping to multiple zones
};

/**
 * @brief Abstract interface for zone detection
 */
class PHOSPHORZONES_EXPORT IZoneDetector : public QObject
{
    Q_OBJECT

public:
    explicit IZoneDetector(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IZoneDetector() override;

    virtual Layout* layout() const = 0;
    virtual void setLayout(Layout* layout) = 0;

    // Zone detection
    virtual ZoneDetectionResult detectZone(const QPointF& cursorPos) const = 0;
    virtual ZoneDetectionResult detectMultiZone(const QPointF& cursorPos) const = 0;
    virtual Zone* zoneAtPoint(const QPointF& point) const = 0;
    virtual Zone* nearestZone(const QPointF& point) const = 0;

    // Paint-to-snap: expand painted zones to include all zones intersecting the bounding rect
    // (same raycasting algorithm as detectMultiZone and the editor)
    virtual QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>& seedZones) const = 0;

    // Highlight management
    virtual void highlightZone(Zone* zone) = 0;
    virtual void highlightZones(const QVector<Zone*>& zones) = 0;
    virtual void clearHighlights() = 0;

Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void highlightsCleared();
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::ZoneDetectionResult)
