// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantMap>
#include <QRectF>

namespace PlasmaZones {

/**
 * @brief Service for snapping zone geometry to grid and edges
 *
 * Provides two snapping modes:
 * - Grid snapping: Snaps to regular intervals (e.g., 10% grid)
 * - Edge snapping: Snaps to other zone edges and canvas boundaries
 *
 * Edge snapping takes priority over grid snapping. If an edge snaps to
 * another zone's edge, grid snapping is skipped for that edge.
 */
class SnappingService : public QObject
{
    Q_OBJECT

public:
    explicit SnappingService(QObject* parent = nullptr);
    ~SnappingService() override = default;

    // Settings
    bool gridSnappingEnabled() const
    {
        return m_gridSnappingEnabled;
    }
    void setGridSnappingEnabled(bool enabled);

    bool edgeSnappingEnabled() const
    {
        return m_edgeSnappingEnabled;
    }
    void setEdgeSnappingEnabled(bool enabled);

    qreal snapIntervalX() const
    {
        return m_snapIntervalX;
    }
    void setSnapIntervalX(qreal interval);
    qreal snapIntervalY() const
    {
        return m_snapIntervalY;
    }
    void setSnapIntervalY(qreal interval);

    // Backward compatibility: returns X interval
    qreal snapInterval() const
    {
        return m_snapIntervalX;
    }
    void setSnapInterval(qreal interval)
    {
        setSnapIntervalX(interval);
        setSnapIntervalY(interval);
    }

    /**
     * @brief Snap geometry for move operations (all edges move together)
     *
     * Preserves zone dimensions while snapping position.
     * Called from ZoneDragHandler.qml during drag operations.
     */
    Q_INVOKABLE QVariantMap snapGeometry(qreal x, qreal y, qreal width, qreal height, const QVariantList& allZones,
                                         const QString& excludeZoneId = QString());

    /**
     * @brief Snap geometry for resize operations (selective edge snapping)
     *
     * Only snaps the edges specified by the snap* parameters.
     * Called from ResizeHandles.qml during resize operations.
     */
    Q_INVOKABLE QVariantMap snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                  const QVariantList& allZones, const QString& excludeZoneId,
                                                  bool snapLeft, bool snapRight, bool snapTop, bool snapBottom);

Q_SIGNALS:
    void gridSnappingEnabledChanged();
    void edgeSnappingEnabledChanged();
    void snapIntervalXChanged();
    void snapIntervalYChanged();
    void snapIntervalChanged(); // For backward compatibility

private:
    /**
     * @brief Snap a single value to grid with boundary preference
     *
     * When snapping would result in a boundary (0.0 or 1.0), checks if the
     * adjacent non-boundary grid point is closer. This prevents "skipping"
     * the last grid point when approaching canvas edges.
     *
     * @param value The value to snap (0.0-1.0 relative coordinate)
     * @param interval The grid interval
     * @return The snapped value
     */
    qreal snapValueToGrid(qreal value, qreal interval) const;

    /**
     * @brief Snap selected edges to grid
     */
    QRectF snapToGridSelective(const QRectF& rect, bool snapLeft, bool snapRight, bool snapTop, bool snapBottom);

    /**
     * @brief Snap selected edges to other zone edges with tracking
     *
     * @param[out] leftSnapped Set to true if left edge snapped
     * @param[out] rightSnapped Set to true if right edge snapped
     * @param[out] topSnapped Set to true if top edge snapped
     * @param[out] bottomSnapped Set to true if bottom edge snapped
     */
    QRectF snapToEdgesSelectiveWithTracking(const QRectF& rect, const QVariantList& allZones,
                                            const QString& excludeZoneId, bool snapLeft, bool snapRight, bool snapTop,
                                            bool snapBottom, bool& leftSnapped, bool& rightSnapped, bool& topSnapped,
                                            bool& bottomSnapped);

    /**
     * @brief Validate and sanitize input geometry
     * @return true if geometry is valid, false if it should be rejected
     */
    bool validateGeometry(qreal x, qreal y, qreal width, qreal height) const;

    bool m_gridSnappingEnabled = true;
    bool m_edgeSnappingEnabled = true;
    qreal m_snapIntervalX = 0.1;
    qreal m_snapIntervalY = 0.1;
    qreal m_edgeThreshold = 0.02;
};

} // namespace PlasmaZones
