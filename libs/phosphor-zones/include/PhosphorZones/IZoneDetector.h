// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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

namespace PhosphorZones {

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
 * @brief Pure-query subset of zone detection — read-only geometry lookup.
 *
 * ISP-compliant slice of @c IZoneDetector. Consumers that need to resolve
 * zones at a cursor / point without side-effects (editor paint-to-snap,
 * fixtures, geometry tests) depend on this minimal surface instead of the
 * full detector interface.
 *
 * Non-QObject by design — pure data queries have no lifecycle signals. The
 * full @c IZoneDetector interface (which carries signals) inherits from
 * this, so @c ZoneDetector satisfies both at once.
 */
class PHOSPHORZONES_EXPORT IZoneDetection
{
public:
    IZoneDetection() = default;
    virtual ~IZoneDetection();

    virtual Layout* layout() const = 0;

    virtual ZoneDetectionResult detectZone(const QPointF& cursorPos) const = 0;
    virtual ZoneDetectionResult detectMultiZone(const QPointF& cursorPos) const = 0;
    virtual Zone* zoneAtPoint(const QPointF& point) const = 0;
    virtual Zone* nearestZone(const QPointF& point) const = 0;

    /// Paint-to-snap: expand painted zones to include all zones intersecting
    /// the bounding rect (same raycasting algorithm as detectMultiZone and
    /// the editor).
    virtual QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>& seedZones) const = 0;

protected:
    IZoneDetection(const IZoneDetection&) = default;
    IZoneDetection& operator=(const IZoneDetection&) = default;
};

/**
 * @brief Abstract interface for zone detection + highlight lifecycle.
 *
 * Extends @c IZoneDetection with mutating highlight-management methods and
 * the Qt signals that accompany them. Split is ISP-friendly — callers that
 * only query (and don't care about highlights) take @c IZoneDetection*.
 */
class PHOSPHORZONES_EXPORT IZoneDetector : public QObject, public IZoneDetection
{
    Q_OBJECT

public:
    explicit IZoneDetector(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IZoneDetector() override;

    virtual void setLayout(Layout* layout) = 0;

    // Highlight management
    virtual void highlightZone(Zone* zone) = 0;
    virtual void highlightZones(const QVector<Zone*>& zones) = 0;
    virtual void clearHighlights() = 0;

Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void highlightsCleared();
};

} // namespace PhosphorZones

Q_DECLARE_METATYPE(PhosphorZones::ZoneDetectionResult)
