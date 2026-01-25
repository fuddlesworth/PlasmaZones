// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SnappingService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QtMath>

using namespace PlasmaZones;

SnappingService::SnappingService(QObject* parent)
    : QObject(parent)
    , m_snapIntervalX(EditorConstants::DefaultSnapInterval)
    , m_snapIntervalY(EditorConstants::DefaultSnapInterval)
    , m_edgeThreshold(EditorConstants::EdgeThreshold)
{
}

void SnappingService::setGridSnappingEnabled(bool enabled)
{
    if (m_gridSnappingEnabled != enabled) {
        m_gridSnappingEnabled = enabled;
        Q_EMIT gridSnappingEnabledChanged();
    }
}

void SnappingService::setEdgeSnappingEnabled(bool enabled)
{
    if (m_edgeSnappingEnabled != enabled) {
        m_edgeSnappingEnabled = enabled;
        Q_EMIT edgeSnappingEnabledChanged();
    }
}

void SnappingService::setSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_snapIntervalX, interval)) {
        m_snapIntervalX = interval;
        Q_EMIT snapIntervalXChanged();
        Q_EMIT snapIntervalChanged(); // For backward compatibility
    }
}

void SnappingService::setSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_snapIntervalY, interval)) {
        m_snapIntervalY = interval;
        Q_EMIT snapIntervalYChanged();
        Q_EMIT snapIntervalChanged(); // For backward compatibility
    }
}

bool SnappingService::validateGeometry(qreal x, qreal y, qreal width, qreal height) const
{
    // Reject invalid or degenerate geometry
    if (!qIsFinite(x) || !qIsFinite(y) || !qIsFinite(width) || !qIsFinite(height)) {
        qCDebug(PlasmaZones::lcSnapping) << "Rejected non-finite geometry - x:" << x << "y:" << y << "w:" << width
                                         << "h:" << height;
        return false;
    }
    if (width <= 0 || height <= 0) {
        qCDebug(PlasmaZones::lcSnapping) << "Rejected non-positive dimensions - w:" << width << "h:" << height;
        return false;
    }
    // Allow some tolerance for coordinates slightly outside [0,1] due to floating point
    constexpr qreal tolerance = 0.001;
    if (x < -tolerance || y < -tolerance || x + width > 1.0 + tolerance || y + height > 1.0 + tolerance) {
        qCDebug(PlasmaZones::lcSnapping) << "Rejected out-of-bounds geometry - x:" << x << "y:" << y
                                         << "right:" << (x + width) << "bottom:" << (y + height);
        return false;
    }
    return true;
}

qreal SnappingService::snapValueToGrid(qreal value, qreal interval) const
{
    qreal snapped = qRound(value / interval) * interval;
    snapped = qBound(0.0, snapped, 1.0);

    // Grid snapping should not snap to canvas boundaries (0.0 or 1.0).
    // Edge snapping handles boundaries; grid snapping returns interior grid points only.
    if (qFuzzyCompare(snapped, 1.0)) {
        // Would snap to right/bottom boundary - return previous grid point instead
        qreal prevGridPoint = qFloor(value / interval) * interval;
        return qBound(0.0, prevGridPoint, 1.0 - interval);
    } else if (qFuzzyCompare(snapped, 0.0)) {
        // Would snap to left/top boundary - return next grid point instead
        qreal nextGridPoint = qCeil(value / interval) * interval;
        return qBound(interval, nextGridPoint, 1.0);
    }

    return snapped;
}

QVariantMap SnappingService::snapGeometry(qreal x, qreal y, qreal width, qreal height, const QVariantList& allZones,
                                          const QString& excludeZoneId)
{
    using namespace PlasmaZones::JsonKeys;

    // Validate input geometry
    if (!validateGeometry(x, y, width, height)) {
        // Return input unchanged for invalid geometry
        QVariantMap result;
        result[X] = x;
        result[Y] = y;
        result[Width] = width;
        result[Height] = height;
        return result;
    }

    // For move operations (all 4 edges), we need to preserve dimensions
    // Snap position (left/top) only, then derive right/bottom from original size
    QRectF rect(x, y, width, height);
    qreal originalWidth = width;
    qreal originalHeight = height;

    // Snap left and top edges to determine new position
    bool leftSnapped = false, rightSnapped = false, topSnapped = false, bottomSnapped = false;

    if (m_edgeSnappingEnabled) {
        // First, try snapping the left edge to zone edges
        QList<qreal> verticalEdges = {0.0, 1.0};
        QList<qreal> horizontalEdges = {0.0, 1.0};

        for (const QVariant& zoneVar : allZones) {
            QVariantMap zone = zoneVar.toMap();
            QString zoneId = zone[Id].toString();
            if (zoneId == excludeZoneId)
                continue;

            qreal zx = zone[X].toDouble();
            qreal zy = zone[Y].toDouble();
            qreal zw = zone[Width].toDouble();
            qreal zh = zone[Height].toDouble();

            verticalEdges << zx << (zx + zw);
            horizontalEdges << zy << (zy + zh);
        }

        qreal left = rect.left();
        qreal top = rect.top();
        qreal right = rect.right();
        qreal bottom = rect.bottom();

        // Find closest snap points for all edges
        qreal closestLeft = left, closestRight = right, closestTop = top, closestBottom = bottom;
        qreal minLeftDist = m_edgeThreshold, minRightDist = m_edgeThreshold;
        qreal minTopDist = m_edgeThreshold, minBottomDist = m_edgeThreshold;

        for (qreal edge : verticalEdges) {
            qreal leftDist = qAbs(left - edge);
            if (leftDist < minLeftDist) {
                minLeftDist = leftDist;
                closestLeft = edge;
            }
            qreal rightDist = qAbs(right - edge);
            if (rightDist < minRightDist) {
                minRightDist = rightDist;
                closestRight = edge;
            }
        }

        for (qreal edge : horizontalEdges) {
            qreal topDist = qAbs(top - edge);
            if (topDist < minTopDist) {
                minTopDist = topDist;
                closestTop = edge;
            }
            qreal bottomDist = qAbs(bottom - edge);
            if (bottomDist < minBottomDist) {
                minBottomDist = bottomDist;
                closestBottom = edge;
            }
        }

        // For moves: prefer snapping edges that are closer to their snap targets
        // This maintains the zone's size while snapping to the nearest edge
        if (minLeftDist < m_edgeThreshold && minLeftDist <= minRightDist) {
            left = closestLeft;
            leftSnapped = true;
        } else if (minRightDist < m_edgeThreshold) {
            // Snap right edge and calculate left from original width
            left = closestRight - originalWidth;
            rightSnapped = true;
        }

        if (minTopDist < m_edgeThreshold && minTopDist <= minBottomDist) {
            top = closestTop;
            topSnapped = true;
        } else if (minBottomDist < m_edgeThreshold) {
            // Snap bottom edge and calculate top from original height
            top = closestBottom - originalHeight;
            bottomSnapped = true;
        }

        rect = QRectF(left, top, originalWidth, originalHeight);
    }

    // Grid snapping for position only (preserve dimensions)
    // Uses boundary-preference logic to avoid skipping last grid point
    if (m_gridSnappingEnabled) {
        qreal left = rect.left();
        qreal top = rect.top();

        // Only grid-snap if that edge wasn't already edge-snapped
        if (!leftSnapped && !rightSnapped) {
            left = snapValueToGrid(left, m_snapIntervalX);
        }
        if (!topSnapped && !bottomSnapped) {
            top = snapValueToGrid(top, m_snapIntervalY);
        }

        // Calculate grid-aligned max positions to avoid floating-point issues
        // For example: if height=0.1 and grid=0.1, max top should be exactly 0.9
        // Using raw (1.0 - originalHeight) could give 0.8999999 due to FP precision
        qreal maxLeft = 1.0 - originalWidth;
        qreal maxTop = 1.0 - originalHeight;

        // Snap max bounds to nearest grid point if very close (within 1e-9)
        if (m_snapIntervalX > 0) {
            qreal gridAlignedMaxLeft = std::floor(maxLeft / m_snapIntervalX) * m_snapIntervalX;
            if (std::abs(maxLeft - gridAlignedMaxLeft - m_snapIntervalX) < 1e-9) {
                // maxLeft is very close to the next grid point
                gridAlignedMaxLeft += m_snapIntervalX;
            }
            maxLeft = gridAlignedMaxLeft;
        }
        if (m_snapIntervalY > 0) {
            qreal gridAlignedMaxTop = std::floor(maxTop / m_snapIntervalY) * m_snapIntervalY;
            if (std::abs(maxTop - gridAlignedMaxTop - m_snapIntervalY) < 1e-9) {
                // maxTop is very close to the next grid point
                gridAlignedMaxTop += m_snapIntervalY;
            }
            maxTop = gridAlignedMaxTop;
        }

        // Clamp position to grid-aligned valid range
        left = qBound(0.0, left, maxLeft);
        top = qBound(0.0, top, maxTop);

        rect = QRectF(left, top, originalWidth, originalHeight);
    }

    // Final bounds clamping (use slightly relaxed bound to account for FP precision)
    qreal finalX = qBound(0.0, rect.x(), 1.0 - originalWidth + 1e-9);
    qreal finalY = qBound(0.0, rect.y(), 1.0 - originalHeight + 1e-9);

    QVariantMap result;
    result[X] = finalX;
    result[Y] = finalY;
    result[Width] = originalWidth;
    result[Height] = originalHeight;
    return result;
}

QVariantMap SnappingService::snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                   const QVariantList& allZones, const QString& excludeZoneId,
                                                   bool snapLeft, bool snapRight, bool snapTop, bool snapBottom)
{
    using namespace PlasmaZones::JsonKeys;

    // Validate input geometry
    if (!validateGeometry(x, y, width, height)) {
        // Return input unchanged for invalid geometry
        QVariantMap result;
        result[X] = x;
        result[Y] = y;
        result[Width] = width;
        result[Height] = height;
        return result;
    }

    QRectF rect(x, y, width, height);

    // Edge snapping takes priority - check which edges snap to zone edges first
    bool leftEdgeSnapped = false, rightEdgeSnapped = false, topEdgeSnapped = false, bottomEdgeSnapped = false;

    if (m_edgeSnappingEnabled) {
        rect = snapToEdgesSelectiveWithTracking(rect, allZones, excludeZoneId, snapLeft, snapRight, snapTop, snapBottom,
                                                leftEdgeSnapped, rightEdgeSnapped, topEdgeSnapped, bottomEdgeSnapped);
    }

    // Grid snapping only applies to edges that didn't edge-snap
    if (m_gridSnappingEnabled) {
        rect = snapToGridSelective(rect, !leftEdgeSnapped && snapLeft, !rightEdgeSnapped && snapRight,
                                   !topEdgeSnapped && snapTop, !bottomEdgeSnapped && snapBottom);
    }

    QVariantMap result;
    result[X] = rect.x();
    result[Y] = rect.y();
    result[Width] = rect.width();
    result[Height] = rect.height();
    return result;
}

QRectF SnappingService::snapToEdgesSelectiveWithTracking(const QRectF& rect, const QVariantList& allZones,
                                                         const QString& excludeZoneId, bool snapLeft, bool snapRight,
                                                         bool snapTop, bool snapBottom, bool& leftSnapped,
                                                         bool& rightSnapped, bool& topSnapped, bool& bottomSnapped)
{
    // Initialize output tracking flags
    leftSnapped = false;
    rightSnapped = false;
    topSnapped = false;
    bottomSnapped = false;

    QList<qreal> verticalEdges = {0.0, 1.0};
    QList<qreal> horizontalEdges = {0.0, 1.0};

    using namespace PlasmaZones::JsonKeys;

    for (const QVariant& zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[Id].toString();
        if (zoneId == excludeZoneId)
            continue;

        qreal zx = zone[X].toDouble();
        qreal zy = zone[Y].toDouble();
        qreal zw = zone[Width].toDouble();
        qreal zh = zone[Height].toDouble();

        verticalEdges << zx << (zx + zw);
        horizontalEdges << zy << (zy + zh);
    }

    qreal left = rect.left();
    qreal top = rect.top();
    qreal right = rect.right();
    qreal bottom = rect.bottom();

    qreal closestLeft = left;
    qreal closestRight = right;
    qreal closestTop = top;
    qreal closestBottom = bottom;
    qreal minLeftDist = m_edgeThreshold;
    qreal minRightDist = m_edgeThreshold;
    qreal minTopDist = m_edgeThreshold;
    qreal minBottomDist = m_edgeThreshold;

    if (snapLeft || snapRight) {
        for (qreal edge : verticalEdges) {
            if (snapLeft) {
                qreal leftDist = qAbs(left - edge);
                if (leftDist < minLeftDist) {
                    minLeftDist = leftDist;
                    closestLeft = edge;
                }
            }
            if (snapRight) {
                qreal rightDist = qAbs(right - edge);
                if (rightDist < minRightDist) {
                    minRightDist = rightDist;
                    closestRight = edge;
                }
            }
        }
    }

    if (snapTop || snapBottom) {
        for (qreal edge : horizontalEdges) {
            if (snapTop) {
                qreal topDist = qAbs(top - edge);
                if (topDist < minTopDist) {
                    minTopDist = topDist;
                    closestTop = edge;
                }
            }
            if (snapBottom) {
                qreal bottomDist = qAbs(bottom - edge);
                if (bottomDist < minBottomDist) {
                    minBottomDist = bottomDist;
                    closestBottom = edge;
                }
            }
        }
    }

    // Apply snapping and track which edges actually snapped
    if (snapLeft && minLeftDist < m_edgeThreshold) {
        left = closestLeft;
        leftSnapped = true;
    }
    if (snapRight && minRightDist < m_edgeThreshold) {
        right = closestRight;
        rightSnapped = true;
    }
    if (snapTop && minTopDist < m_edgeThreshold) {
        top = closestTop;
        topSnapped = true;
    }
    if (snapBottom && minBottomDist < m_edgeThreshold) {
        bottom = closestBottom;
        bottomSnapped = true;
    }

    // Enforce minimum zone size (5% of canvas)
    constexpr qreal minSize = EditorConstants::MinZoneSize;
    qreal width = right - left;
    qreal height = bottom - top;

    if (width < minSize) {
        // Adjust the edge that was snapped, or both if neither was
        if (rightSnapped && !leftSnapped) {
            left = qMax(0.0, right - minSize);
        } else {
            right = qMin(1.0, left + minSize);
        }
        width = right - left;
    }
    if (height < minSize) {
        if (bottomSnapped && !topSnapped) {
            top = qMax(0.0, bottom - minSize);
        } else {
            bottom = qMin(1.0, top + minSize);
        }
        height = bottom - top;
    }

    return QRectF(left, top, width, height);
}

QRectF SnappingService::snapToGridSelective(const QRectF& rect, bool snapLeft, bool snapRight, bool snapTop,
                                            bool snapBottom)
{
    // Validate input
    if (rect.width() <= 0 || rect.height() <= 0 || !qIsFinite(rect.left()) || !qIsFinite(rect.top())
        || !qIsFinite(rect.width()) || !qIsFinite(rect.height())) {
        return rect;
    }

    // If no edges need snapping, return as-is
    if (!snapLeft && !snapRight && !snapTop && !snapBottom) {
        return rect;
    }

    qreal left = rect.left();
    qreal top = rect.top();
    qreal right = rect.right();
    qreal bottom = rect.bottom();

    // Only snap the specified edges to grid using boundary-preference logic
    if (snapLeft) {
        left = snapValueToGrid(left, m_snapIntervalX);
    }
    if (snapTop) {
        top = snapValueToGrid(top, m_snapIntervalY);
    }
    if (snapRight) {
        right = snapValueToGrid(right, m_snapIntervalX);
    }
    if (snapBottom) {
        bottom = snapValueToGrid(bottom, m_snapIntervalY);
    }

    // Handle edge cases where snapping causes invalid geometry
    // Enforce minimum zone size (5% of canvas)
    constexpr qreal minSize = EditorConstants::MinZoneSize;

    if (right - left < minSize) {
        if (snapRight && !snapLeft) {
            // Right edge snapped - adjust right to maintain minimum
            right = qMin(1.0, left + minSize);
        } else if (snapLeft && !snapRight) {
            // Left edge snapped - adjust left to maintain minimum
            left = qMax(0.0, right - minSize);
        } else {
            // Both snapped - prefer moving right edge
            right = qMin(1.0, left + minSize);
            if (right - left < minSize) {
                left = qMax(0.0, right - minSize);
            }
        }
    }
    if (bottom - top < minSize) {
        if (snapBottom && !snapTop) {
            bottom = qMin(1.0, top + minSize);
        } else if (snapTop && !snapBottom) {
            top = qMax(0.0, bottom - minSize);
        } else {
            bottom = qMin(1.0, top + minSize);
            if (bottom - top < minSize) {
                top = qMax(0.0, bottom - minSize);
            }
        }
    }

    // Calculate final dimensions
    qreal width = right - left;
    qreal height = bottom - top;

    // Ensure dimensions don't exceed bounds
    if (left + width > 1.0) {
        width = 1.0 - left;
    }
    if (top + height > 1.0) {
        height = 1.0 - top;
    }

    // Final validation
    if (width < minSize || height < minSize || !qIsFinite(left) || !qIsFinite(top) || !qIsFinite(width)
        || !qIsFinite(height)) {
        // Fallback to original rect if snapping produced invalid result
        qCDebug(PlasmaZones::lcSnapping) << "Snapping produced invalid result, using original rect - computed w:"
                                         << width << "h:" << height;
        return rect;
    }

    return QRectF(left, top, width, height);
}
