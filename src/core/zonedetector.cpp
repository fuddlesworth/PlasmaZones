// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonedetector.h"
#include "zonehighlighter.h"
#include "logging.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace PlasmaZones {

ZoneDetector::ZoneDetector(ISettings* settings, QObject* parent)
    : IZoneDetector(parent)
    , m_settings(settings)
    , m_highlighter(std::make_unique<ZoneHighlighter>(this))
{
    // Forward highlighter signals for backward compatibility
    connect(m_highlighter.get(), &ZoneHighlighter::zoneHighlighted, this, &ZoneDetector::zoneHighlighted);
    connect(m_highlighter.get(), &ZoneHighlighter::highlightsCleared, this, &ZoneDetector::highlightsCleared);
}

ZoneDetector::~ZoneDetector() = default; // Defined here so ZoneHighlighter is fully defined

void ZoneDetector::setLayout(Layout* layout)
{
    if (m_layout != layout) {
        // Disconnect from old layout's destroyed signal
        if (m_layout) {
            qCDebug(lcZone) << "Disconnecting from previous layout";
            disconnect(m_layout, &QObject::destroyed, this, nullptr);
        }
        m_layout = layout;
        // Connect to new layout's destroyed signal to prevent dangling pointer
        if (m_layout) {
            qCInfo(lcZone) << "Layout set with" << m_layout->zones().size() << "zones";
            connect(m_layout, &QObject::destroyed, this, [this]() {
                qCDebug(lcZone) << "Layout destroyed, clearing";
                m_layout = nullptr;
                m_highlighter->clearHighlights();
                Q_EMIT layoutChanged();
            });
        } else {
            qCDebug(lcZone) << "Layout cleared (set to null)";
        }
        m_highlighter->clearHighlights();
        Q_EMIT layoutChanged();
    }
}

ZoneDetectionResult ZoneDetector::detectZone(const QPointF& cursorPos) const
{
    ZoneDetectionResult result;

    if (!m_layout) {
        qCDebug(lcZone) << "detectZone: No layout set";
        return result;
    }

    // First check if cursor is inside any zone
    Zone* containingZone = zoneAtPoint(cursorPos);
    if (containingZone) {
        result.primaryZone = containingZone;
        result.snapGeometry = containingZone->geometry();
        result.distance = 0;
        qCDebug(lcZone) << "Cursor at" << cursorPos << "is inside zone" << containingZone->id();
        return result;
    }

    // If not inside, find nearest zone
    Zone* nearest = nearestZone(cursorPos);
    if (nearest) {
        result.primaryZone = nearest;
        result.snapGeometry = nearest->geometry();
        result.distance = nearest->distanceToPoint(cursorPos);
        qCDebug(lcZone) << "Cursor at" << cursorPos << "nearest to zone" << nearest->id()
                        << "distance:" << result.distance;
    } else {
        qCDebug(lcZone) << "No zone found for cursor at" << cursorPos;
    }

    return result;
}

namespace {

// Check if two rects share an edge (left↔right or top↔bottom) within tolerance,
// with perpendicular overlap > 0. Used by both expandZonesByIntersection and areZonesAdjacent.
// minOverlapFraction: minimum perpendicular overlap as fraction of the smaller dimension (0.0–1.0).
bool sharesEdge(const QRectF& r1, const QRectF& r2, qreal tolerance, qreal minOverlapFraction = 0.0)
{
    // Left-Right adjacency (r1.right ≈ r2.left or r2.right ≈ r1.left)
    if (qAbs(r1.right() - r2.left()) <= tolerance || qAbs(r2.right() - r1.left()) <= tolerance) {
        qreal overlap = qMin(r1.bottom(), r2.bottom()) - qMax(r1.top(), r2.top());
        if (overlap > 0 && overlap >= qMin(r1.height(), r2.height()) * minOverlapFraction) {
            return true;
        }
    }
    // Top-Bottom adjacency (r1.bottom ≈ r2.top or r2.bottom ≈ r1.top)
    if (qAbs(r1.bottom() - r2.top()) <= tolerance || qAbs(r2.bottom() - r1.top()) <= tolerance) {
        qreal overlap = qMin(r1.right(), r2.right()) - qMax(r1.left(), r2.left());
        if (overlap > 0 && overlap >= qMin(r1.width(), r2.width()) * minOverlapFraction) {
            return true;
        }
    }
    return false;
}


// Expand seed zones to include all zones that intersect the bounding rectangle.
// Used by paint-to-span to fill gaps between user-painted zones.
// The bounding rect grows iteratively so transitive gaps get filled
// (e.g. painting zones 2 and 4 in dwindle fills zone 5 via zone 3).
QVector<Zone*> expandZonesByIntersection(Layout* layout, const QVector<Zone*>& seedZones)
{
    if (!layout || seedZones.isEmpty()) {
        return seedZones;
    }

    const auto& allZones = layout->zones();

    // Build initial bounding rect and selected set from seed zones (skip nulls)
    QRectF boundingRect;
    QSet<Zone*> selectedZones;

    for (auto* zone : seedZones) {
        if (!zone) {
            continue;
        }
        if (boundingRect.isEmpty()) {
            boundingRect = zone->geometry();
        } else {
            boundingRect = boundingRect.united(zone->geometry());
        }
        selectedZones.insert(zone);
    }

    if (selectedZones.isEmpty()) {
        return QVector<Zone*>();
    }

    // Iteratively expand to include all zones that intersect the bounding rect
    bool foundNew = true;
    int iterations = 0;
    const int maxIterations = 100;

    while (foundNew && iterations < maxIterations) {
        foundNew = false;
        iterations++;
        QRectF currentRect = boundingRect;

        for (auto* zone : allZones) {
            if (!zone || selectedZones.contains(zone)) {
                continue;
            }
            if (zone->geometry().intersects(currentRect)) {
                selectedZones.insert(zone);
                boundingRect = boundingRect.united(zone->geometry());
                foundNew = true;
            }
        }
    }

    if (iterations >= maxIterations) {
        qCWarning(lcZone) << "Max iterations reached in zone expansion - possible infinite loop";
    }

    // Preserve layout order for consistent output
    QVector<Zone*> result;
    for (auto* zone : allZones) {
        if (selectedZones.contains(zone)) {
            result.append(zone);
        }
    }
    return result;
}

} // namespace

ZoneDetectionResult ZoneDetector::detectMultiZone(const QPointF& cursorPos) const
{
    ZoneDetectionResult result;

    if (!m_layout) {
        return detectZone(cursorPos);
    }

    const auto& allZones = m_layout->zones();
    const qreal adjacentThreshold = m_settings->adjacentThreshold();

    // Separate overlapping zones (cursor inside) from edge-adjacent zones
    // (cursor outside but within threshold). Only edge-adjacent zones trigger multi-zone.
    QVector<Zone*> overlappingZones;
    QVector<Zone*> edgeAdjacentZones;

    for (auto* zone : allZones) {
        if (!zone) {
            continue;
        }
        if (zone->containsPoint(cursorPos)) {
            overlappingZones.append(zone);
        } else {
            qreal distance = zone->distanceToPoint(cursorPos);
            if (distance <= adjacentThreshold) {
                edgeAdjacentZones.append(zone);
            }
        }
    }

    // Get primary zone via smallest-area heuristic (handles overlap correctly)
    Zone* primaryZone = zoneAtPoint(cursorPos);

    // Store overlap info for callers
    result.overlappingZones = overlappingZones;

    // Multi-zone ONLY if there are edge-adjacent zones (cursor near a boundary between zones)
    if (!edgeAdjacentZones.isEmpty()) {
        // Combine primary (if any) + edge-adjacent zones directly.
        // Do NOT flood-fill expand: in tiling layouts every zone shares an edge
        // with its neighbor, so expandZonesByIntersection() cascades to ALL zones.
        // The seed zones already capture the zones the cursor is between.
        // (expandZonesByIntersection is still used by expandPaintedZonesToRect
        // for paint-to-span mode where rectangular gap-filling is needed.)
        QVector<Zone*> seedZones = edgeAdjacentZones;
        if (primaryZone && !seedZones.contains(primaryZone)) {
            seedZones.prepend(primaryZone);
        }

        if (seedZones.size() > 1) {
            if (!primaryZone) {
                // No containing zone — pick closest edge-adjacent as primary
                qreal minDistance = std::numeric_limits<qreal>::max();
                for (auto* zone : edgeAdjacentZones) {
                    qreal distance = zone->distanceToPoint(cursorPos);
                    if (distance < minDistance) {
                        minDistance = distance;
                        primaryZone = zone;
                    }
                }
            }

            result.primaryZone = primaryZone;
            result.adjacentZones = seedZones;
            result.isMultiZone = true;
            result.snapGeometry = combineZoneGeometries(seedZones);
            result.distance = 0;
            return result;
        }
    }

    // Fall through to single-zone detection, preserving overlap info
    ZoneDetectionResult singleResult = detectZone(cursorPos);
    singleResult.overlappingZones = overlappingZones;
    return singleResult;
}

QVector<Zone*> ZoneDetector::expandPaintedZonesToRect(const QVector<Zone*>& seedZones) const
{
    return expandZonesByIntersection(m_layout, seedZones);
}

Zone* ZoneDetector::zoneAtPoint(const QPointF& point) const
{
    if (!m_layout) {
        return nullptr;
    }

    return m_layout->zoneAtPoint(point);
}

Zone* ZoneDetector::nearestZone(const QPointF& point) const
{
    if (!m_layout) {
        return nullptr;
    }

    return m_layout->nearestZone(point);
}

QRectF ZoneDetector::combineZoneGeometries(const QVector<Zone*>& zones) const
{
    if (zones.isEmpty()) {
        return QRectF();
    }

    Zone* firstZone = zones.first();
    if (!firstZone) {
        return QRectF();
    }

    QRectF combined = firstZone->geometry();
    for (int i = 1; i < zones.size(); ++i) {
        if (zones[i]) {
            combined = combined.united(zones[i]->geometry());
        }
    }

    return combined;
}

void ZoneDetector::highlightZone(Zone* zone)
{
    m_highlighter->highlightZone(zone);
}

void ZoneDetector::highlightZones(const QVector<Zone*>& zones)
{
    m_highlighter->highlightZones(zones);
}

void ZoneDetector::clearHighlights()
{
    m_highlighter->clearHighlights();
}

bool ZoneDetector::areZonesAdjacent(Zone* zone1, Zone* zone2) const
{
    if (!zone1 || !zone2 || zone1 == zone2) {
        return false;
    }

    return sharesEdge(zone1->geometry(), zone2->geometry(), m_settings->adjacentThreshold(), 0.1);
}

qreal ZoneDetector::distanceToZoneEdge(const QPointF& point, Zone* zone) const
{
    if (!zone) {
        return std::numeric_limits<qreal>::max();
    }

    const QRectF& rect = zone->geometry();

    // If point is inside, calculate distance to nearest edge
    if (rect.contains(point)) {
        qreal distLeft = point.x() - rect.left();
        qreal distRight = rect.right() - point.x();
        qreal distTop = point.y() - rect.top();
        qreal distBottom = rect.bottom() - point.y();
        return std::min({distLeft, distRight, distTop, distBottom});
    }

    // Point is outside - use zone's distance calculation
    return zone->distanceToPoint(point);
}

} // namespace PlasmaZones
