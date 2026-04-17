// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zone.h"
#include "layout.h"
#include "interfaces.h"
#include "constants.h"
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <memory>

namespace PlasmaZones {
class ZoneHighlighter;
} // namespace PlasmaZones

namespace PlasmaZones {

// ZoneDetectionResult is defined in interfaces.h

/**
 * @brief Efficient zone detection for window snapping
 *
 * The ZoneDetector provides optimized algorithms for determining
 * which zone(s) a window should snap to based on cursor position.
 * It supports:
 * - Single zone snapping (standard)
 * - Multi-zone snapping (adjacent zones)
 * - Edge detection for zone border snapping
 * - Configurable detection thresholds
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
 */
class PLASMAZONES_EXPORT ZoneDetector : public IZoneDetector
{
    Q_OBJECT

    Q_PROPERTY(Layout* layout READ layout WRITE setLayout NOTIFY layoutChanged)

public:
    explicit ZoneDetector(QObject* parent = nullptr);
    ~ZoneDetector() override; // Defined in .cpp to allow unique_ptr with forward declaration

    // IZoneDetector interface implementation
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override;

    /**
     * @brief Set the adjacency threshold used by detectMultiZone / areZonesAdjacent.
     *
     * Zones whose edges are within @p px pixels of each other are treated as
     * adjacent for multi-zone selection. The daemon typically wires this to
     * the live Settings value (via the adjacentThresholdChanged signal) so
     * users can tune detection sensitivity without reconstructing the
     * detector. Fine-grained injection of the int, rather than a whole
     * ISettings pointer, keeps ZoneDetector independent of the settings
     * layer — prerequisite for moving it into a standalone phosphor-zones
     * library.
     */
    void setAdjacentThreshold(int px)
    {
        m_adjacentThreshold = px;
    }
    int adjacentThreshold() const
    {
        return m_adjacentThreshold;
    }

    Q_INVOKABLE ZoneDetectionResult detectZone(const QPointF& cursorPos) const override;
    Q_INVOKABLE ZoneDetectionResult detectMultiZone(const QPointF& cursorPos) const override;
    /**
     * @brief Expand painted zones to include all zones that intersect the bounding rect.
     * Uses the same raycasting/intersection algorithm as detectMultiZone and the editor.
     * When user paints over zones 1 and 2, this returns zones 1,2,3,4 if 3 and 4 also
     * intersect the minimal rectangle spanning the painted zones.
     */
    Q_INVOKABLE QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>& seedZones) const override;
    Q_INVOKABLE Zone* zoneAtPoint(const QPointF& point) const override;
    Q_INVOKABLE Zone* nearestZone(const QPointF& point) const override;

    // Note: Highlighting methods removed - use ZoneHighlighter instead
    // These methods are kept for backward compatibility but delegate to ZoneHighlighter
    Q_INVOKABLE void highlightZone(Zone* zone) override;
    Q_INVOKABLE void highlightZones(const QVector<Zone*>& zones) override;
    Q_INVOKABLE void clearHighlights() override;

Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void highlightsCleared();

private:
    QRectF combineZoneGeometries(const QVector<Zone*>& zones) const;
    bool areZonesAdjacent(Zone* zone1, Zone* zone2) const;
    qreal distanceToZoneEdge(const QPointF& point, Zone* zone) const;
    Zone* resolveOverlappingZone(const QPointF& point) const;

    Layout* m_layout = nullptr;
    int m_adjacentThreshold = Defaults::AdjacentThreshold;

    // UI state management
    std::unique_ptr<class ZoneHighlighter> m_highlighter;
};

} // namespace PlasmaZones

// Q_DECLARE_METATYPE is in interfaces.h
