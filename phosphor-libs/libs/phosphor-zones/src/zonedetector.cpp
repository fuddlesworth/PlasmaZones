// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorZones/ZoneHighlighter.h>
#include "zoneslogging.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace PhosphorZones {

ZoneDetector::ZoneDetector(QObject* parent)
    : IZoneDetector(parent)
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
            qCDebug(PhosphorZones::lcZonesLib) << "Disconnecting from previous layout";
            disconnect(m_layout, &QObject::destroyed, this, nullptr);
        }
        m_layout = layout;
        // Connect to new layout's destroyed signal to prevent dangling pointer
        if (m_layout) {
            qCInfo(PhosphorZones::lcZonesLib) << "Layout set with" << m_layout->zones().size() << "zones";
            // The lambda captures `this` and dereferences `m_highlighter`. Safe
            // because `m_highlighter` is a unique_ptr member whose lifetime
            // strictly follows ZoneDetector's — `this` cannot outlive the
            // highlighter, so the deref can never dangle.
            connect(m_layout, &QObject::destroyed, this, [this]() {
                qCDebug(PhosphorZones::lcZonesLib) << "Layout destroyed, clearing";
                m_layout = nullptr;
                m_highlighter->clearHighlights();
                Q_EMIT layoutChanged();
            });
        } else {
            qCDebug(PhosphorZones::lcZonesLib) << "Layout cleared (set to null)";
        }
        m_highlighter->clearHighlights();
        Q_EMIT layoutChanged();
    }
}

ZoneDetectionResult ZoneDetector::detectZone(const QPointF& cursorPos) const
{
    ZoneDetectionResult result;

    if (!m_layout) {
        qCDebug(PhosphorZones::lcZonesLib) << "detectZone: No layout set";
        return result;
    }

    // Use center-distance heuristic to resolve overlapping zones.
    // Unlike zoneAtPoint() (smallest-area-wins), this lets the user reach
    // background zones by dragging toward their center.
    Zone* containingZone = resolveOverlappingZone(cursorPos);
    if (containingZone) {
        result.primaryZone = containingZone;
        result.snapGeometry = containingZone->geometry();
        result.distance = 0;
        qCDebug(PhosphorZones::lcZonesLib) << "Cursor at" << cursorPos << "is inside zone" << containingZone->id();
        return result;
    }

    // If not inside, find nearest zone
    Zone* nearest = nearestZone(cursorPos);
    if (nearest) {
        result.primaryZone = nearest;
        result.snapGeometry = nearest->geometry();
        result.distance = nearest->distanceToPoint(cursorPos);
        qCDebug(PhosphorZones::lcZonesLib)
            << "Cursor at" << cursorPos << "nearest to zone" << nearest->id() << "distance:" << result.distance;
    } else {
        qCDebug(PhosphorZones::lcZonesLib) << "No zone found for cursor at" << cursorPos;
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

// Minimum fraction of a zone's area that must lie within the bounding rect for
// the zone to be included during expansion.  A gap-filler zone that sits entirely
// between the seed zones has ratio 1.0; a large background zone that merely
// overlaps has a much lower ratio (e.g. 0.3).  The threshold must be strictly
// greater-than so that zones covering exactly half the bounding rect (a common
// layout — two sub-zones tiling the top half of a full-screen background) are
// excluded.
constexpr qreal kExpansionOverlapThreshold = 0.5;

// Expand seed zones to include all zones that intersect the bounding rectangle.
// Used by paint-to-span to fill gaps between user-painted zones.
// The bounding rect grows iteratively so transitive gaps get filled
// (e.g. painting zones 2 and 4 in dwindle fills zone 5 via zone 3).
//
// Complexity: O(n²) where n = layout zone count. The selectedZones set
// grows monotonically — each iteration that finds a new zone adds at
// least one entry and no zone can be re-added, so the loop is bounded
// by the number of layout zones. Each iteration re-scans every zone in
// @p layout against the growing bounding rect.
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

    // Iteratively expand to include all zones that intersect the bounding
    // rect. The loop is naturally bounded by the zone count — selectedZones
    // grows monotonically and no zone can be inserted twice, so once every
    // eligible zone has been absorbed no further iteration adds to the set
    // and foundNew goes false.
    bool foundNew = true;
    int iterations = 0;

    while (foundNew) {
        foundNew = false;
        ++iterations;
        QRectF currentRect = boundingRect;

        for (auto* zone : allZones) {
            if (!zone || selectedZones.contains(zone)) {
                continue;
            }
            QRectF zoneGeom = zone->geometry();
            if (zoneGeom.intersects(currentRect)) {
                // Only include zones that are substantially within the bounding rect.
                // This prevents large background/overlay zones from being pulled in
                // when spanning adjacent sub-zones (e.g. zones 7 & 9 should not
                // pull in a larger zone 2 underneath them).
                QRectF intersection = zoneGeom.intersected(currentRect);
                qreal intersectionArea = intersection.width() * intersection.height();
                qreal zoneArea = zoneGeom.width() * zoneGeom.height();
                if (zoneArea > 0 && intersectionArea / zoneArea > kExpansionOverlapThreshold) {
                    selectedZones.insert(zone);
                    boundingRect = boundingRect.united(zoneGeom);
                    foundNew = true;
                }
            }
        }

        // Monotonic-growth sanity check: an iteration that runs past the
        // layout's zone count means our convergence invariant is broken.
        // If this fires, a zone is being re-inserted or the set is
        // shrinking — both are bugs. Log and break rather than spin.
        if (iterations > allZones.size()) {
            qCWarning(PhosphorZones::lcZonesLib) << "Zone expansion: iterations exceed zone count (" << iterations
                                                 << ">" << allZones.size() << "), convergence invariant broken";
            break;
        }
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

Zone* ZoneDetector::resolveOverlappingZone(const QPointF& point) const
{
    if (!m_layout) {
        return nullptr;
    }

    // Collect all zones containing the point
    QVector<Zone*> containing;
    for (auto* zone : m_layout->zones()) {
        if (zone && zone->containsPoint(point)) {
            containing.append(zone);
        }
    }

    if (containing.isEmpty()) {
        return nullptr;
    }
    if (containing.size() == 1) {
        return containing.first();
    }

    // Multiple overlapping zones: use normalized distance-to-center.
    // For each zone, compute how far the cursor is from the zone's center
    // as a proportion of the zone's half-dimensions (0.0 = at center, 1.0 = at edge).
    // The zone with the lowest normalized distance wins, so the user can "reach"
    // a background zone by dragging toward its center.
    // Tiebreaker: when two zones have the same score (e.g. concentric zones with
    // identical centers), prefer the smaller zone for deterministic behavior.
    Zone* best = nullptr;
    qreal bestScore = std::numeric_limits<qreal>::max();
    qreal bestArea = std::numeric_limits<qreal>::max();

    for (auto* zone : containing) {
        const QRectF& geom = zone->geometry();
        qreal halfW = geom.width() * 0.5;
        qreal halfH = geom.height() * 0.5;
        if (halfW <= 0 || halfH <= 0) {
            continue;
        }

        QPointF center = geom.center();
        qreal nx = (point.x() - center.x()) / halfW; // -1..+1
        qreal ny = (point.y() - center.y()) / halfH; // -1..+1
        qreal score = nx * nx + ny * ny; // Squared normalized distance, 0 at center

        qreal area = geom.width() * geom.height();
        // Shift by +1.0 so qFuzzyCompare handles near-zero scores
        // correctly (the concentric-zones tiebreaker fires when the
        // cursor sits exactly at a shared centre, where both scores
        // are ~0 and strict == would miss the "equal" case).
        if (score < bestScore || (qFuzzyCompare(score + 1.0, bestScore + 1.0) && area < bestArea)) {
            bestScore = score;
            bestArea = area;
            best = zone;
        }
    }

    return best;
}

ZoneDetectionResult ZoneDetector::detectMultiZone(const QPointF& cursorPos) const
{
    ZoneDetectionResult result;

    if (!m_layout) {
        return detectZone(cursorPos);
    }

    const auto& allZones = m_layout->zones();
    const qreal adjacentThreshold = m_adjacentThreshold;

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
            // Don't include the primary zone if it's a large background zone
            // that fully contains all edge-adjacent zones. Including it would
            // expand the span to cover the entire background zone, making the
            // window larger than the user intended (e.g. spanning sub-zones
            // 7 & 9 should not pull in a larger zone 2 underneath them).
            QRectF adjacentBounds;
            for (auto* z : edgeAdjacentZones) {
                adjacentBounds = adjacentBounds.isEmpty() ? z->geometry() : adjacentBounds.united(z->geometry());
            }
            if (!primaryZone->geometry().contains(adjacentBounds)) {
                seedZones.prepend(primaryZone);
            }
        }

        if (seedZones.size() > 1) {
            if (!primaryZone || !seedZones.contains(primaryZone)) {
                // No containing zone, or primary was excluded as a background zone
                // — pick closest edge-adjacent as primary
                primaryZone = nullptr;
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

    return sharesEdge(zone1->geometry(), zone2->geometry(), m_adjacentThreshold, 0.1);
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

} // namespace PhosphorZones
