// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonedetector.h"
#include "zonehighlighter.h"
#include "logging.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace PlasmaZones {

ZoneDetector::ZoneDetector(QObject* parent)
    : IZoneDetector(parent)
    , m_highlighter(std::make_unique<ZoneHighlighter>(this))
{
    // Forward highlighter signals for backward compatibility
    connect(m_highlighter.get(), &ZoneHighlighter::zoneHighlighted, this, &ZoneDetector::zoneHighlighted);
    connect(m_highlighter.get(), &ZoneHighlighter::zonesHighlighted, this, &ZoneDetector::zonesHighlighted);
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

void ZoneDetector::setAdjacentThreshold(qreal threshold)
{
    threshold = qMax(0.0, threshold);
    if (!qFuzzyCompare(m_adjacentThreshold, threshold)) {
        m_adjacentThreshold = threshold;
        Q_EMIT adjacentThresholdChanged();
    }
}

void ZoneDetector::setEdgeThreshold(qreal threshold)
{
    threshold = qMax(0.0, threshold);
    if (!qFuzzyCompare(m_edgeThreshold, threshold)) {
        m_edgeThreshold = threshold;
        Q_EMIT edgeThresholdChanged();
    }
}

void ZoneDetector::setMultiZoneEnabled(bool enabled)
{
    if (m_multiZoneEnabled != enabled) {
        m_multiZoneEnabled = enabled;
        Q_EMIT multiZoneEnabledChanged();
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

ZoneDetectionResult ZoneDetector::detectMultiZone(const QPointF& cursorPos) const
{
    ZoneDetectionResult result;

    if (!m_layout || !m_multiZoneEnabled) {
        return detectZone(cursorPos);
    }

    // Find all zones near the cursor (within threshold or containing cursor)
    // These are the zones the user is "between"
    QVector<Zone*> nearbyZones;
    const auto& allZones = m_layout->zones();

    for (auto* zone : allZones) {
        if (!zone) {
            continue;
        }

        // Include zone if cursor is inside it or near it (within threshold)
        qreal distance = zone->distanceToPoint(cursorPos);
        if (distance <= m_adjacentThreshold || zone->containsPoint(cursorPos)) {
            nearbyZones.append(zone);
        }
    }

    // If we found 2+ nearby zones, use raycast algorithm to find minimal rectangle
    if (nearbyZones.size() >= 2) {
        // Calculate initial bounding rectangle of nearby zones
        QRectF boundingRect;
        Zone* primaryZone = nullptr;
        qreal minDistance = std::numeric_limits<qreal>::max();

        for (auto* zone : nearbyZones) {
            if (boundingRect.isEmpty()) {
                boundingRect = zone->geometry();
            } else {
                boundingRect = boundingRect.united(zone->geometry());
            }

            // Find the nearest zone as primary
            qreal distance = zone->distanceToPoint(cursorPos);
            if (distance < minDistance) {
                minDistance = distance;
                primaryZone = zone;
            }
        }

        // Iteratively expand the rectangle to include all zones that intersect with it
        QSet<Zone*> selectedZones;
        for (auto* zone : nearbyZones) {
            selectedZones.insert(zone);
        }

        // Keep expanding until no new zones are found
        bool foundNew = true;
        int iterations = 0;
        const int maxIterations = 100; // Safety limit to prevent infinite loops

        while (foundNew && iterations < maxIterations) {
            foundNew = false;
            iterations++;
            QRectF currentRect = boundingRect;

            // Find all zones that intersect with the current bounding rectangle
            for (auto* zone : allZones) {
                if (!zone || selectedZones.contains(zone)) {
                    continue;
                }

                const QRectF& zoneGeom = zone->geometry();
                // Check if zone intersects with the bounding rectangle
                // Use intersects() to find any overlapping zones
                if (zoneGeom.intersects(currentRect)) {
                    selectedZones.insert(zone);
                    // Expand bounding rectangle to include this zone
                    boundingRect = boundingRect.united(zoneGeom);
                    foundNew = true;
                }
            }
        }

        if (iterations >= maxIterations) {
            qCWarning(lcZone) << "Max iterations reached in detectMultiZone - possible infinite expansion loop";
        }

        qCDebug(lcZone) << "Multi-zone detection found" << selectedZones.size() << "zones after" << iterations
                        << "iterations";

        // Convert set to vector (maintain order for consistent output)
        QVector<Zone*> zonesInRect;
        for (auto* zone : allZones) {
            if (selectedZones.contains(zone)) {
                zonesInRect.append(zone);
            }
        }

        // If we found multiple zones, combine them
        if (zonesInRect.size() > 1 && primaryZone) {
            result.primaryZone = primaryZone;
            result.adjacentZones = zonesInRect;
            result.isMultiZone = true;
            result.snapGeometry = combineZoneGeometries(zonesInRect);
            result.distance = 0;
            return result;
        }
    }

    // No multi-zone detected - fall back to single zone detection
    return detectZone(cursorPos);
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

QVector<Zone*> ZoneDetector::zonesNearEdge(const QPointF& point) const
{
    QVector<Zone*> result;

    if (!m_layout) {
        return result;
    }

    const auto& zones = m_layout->zones();
    for (auto* zone : zones) {
        if (zone->containsPoint(point) || distanceToZoneEdge(point, zone) <= m_adjacentThreshold) {
            result.append(zone);
        }
    }

    // Sort by distance to point
    std::sort(result.begin(), result.end(), [&point](Zone* a, Zone* b) {
        return a->distanceToPoint(point) < b->distanceToPoint(point);
    });

    return result;
}

bool ZoneDetector::isNearZoneEdge(const QPointF& point, Zone* zone) const
{
    if (!zone) {
        return false;
    }

    return distanceToZoneEdge(point, zone) <= m_edgeThreshold;
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

    const QRectF& r1 = zone1->geometry();
    const QRectF& r2 = zone2->geometry();

    // Check if zones share an edge (within threshold)
    // Use a stricter threshold for adjacency (adjacentThreshold is for cursor proximity, not zone adjacency)
    // Zones are adjacent if they share an edge within 5 pixels (much stricter than cursor proximity)
    qreal adjacencyTolerance = 5.0;

    // Left-Right adjacency (vertical edge between zones)
    if (qAbs(r1.right() - r2.left()) <= adjacencyTolerance || qAbs(r2.right() - r1.left()) <= adjacencyTolerance) {
        // Check vertical overlap - zones must overlap significantly, not just touch
        qreal overlap = qMin(r1.bottom(), r2.bottom()) - qMax(r1.top(), r2.top());
        qreal minHeight = qMin(r1.height(), r2.height());
        // Require at least 10% overlap to consider zones adjacent
        if (overlap > 0 && overlap >= minHeight * 0.1) {
            return true;
        }
    }

    // Top-Bottom adjacency (horizontal edge between zones)
    if (qAbs(r1.bottom() - r2.top()) <= adjacencyTolerance || qAbs(r2.bottom() - r1.top()) <= adjacencyTolerance) {
        // Check horizontal overlap - zones must overlap significantly, not just touch
        qreal overlap = qMin(r1.right(), r2.right()) - qMax(r1.left(), r2.left());
        qreal minWidth = qMin(r1.width(), r2.width());
        // Require at least 10% overlap to consider zones adjacent
        if (overlap > 0 && overlap >= minWidth * 0.1) {
            return true;
        }
    }

    return false;
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
