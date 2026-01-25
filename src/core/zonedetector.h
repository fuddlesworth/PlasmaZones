// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zone.h"
#include "layout.h"
#include "interfaces.h"
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
    Q_PROPERTY(
        qreal adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)
    Q_PROPERTY(qreal edgeThreshold READ edgeThreshold WRITE setEdgeThreshold NOTIFY edgeThresholdChanged)
    Q_PROPERTY(bool multiZoneEnabled READ multiZoneEnabled WRITE setMultiZoneEnabled NOTIFY multiZoneEnabledChanged)

public:
    explicit ZoneDetector(QObject* parent = nullptr);
    ~ZoneDetector() override; // Defined in .cpp to allow unique_ptr with forward declaration

    // IZoneDetector interface implementation
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override;

    qreal adjacentThreshold() const override
    {
        return m_adjacentThreshold;
    }
    void setAdjacentThreshold(qreal threshold) override;

    qreal edgeThreshold() const override
    {
        return m_edgeThreshold;
    }
    void setEdgeThreshold(qreal threshold) override;

    bool multiZoneEnabled() const override
    {
        return m_multiZoneEnabled;
    }
    void setMultiZoneEnabled(bool enabled) override;

    Q_INVOKABLE ZoneDetectionResult detectZone(const QPointF& cursorPos) const override;
    Q_INVOKABLE ZoneDetectionResult detectMultiZone(const QPointF& cursorPos) const override;
    Q_INVOKABLE Zone* zoneAtPoint(const QPointF& point) const override;
    Q_INVOKABLE Zone* nearestZone(const QPointF& point) const override;

    Q_INVOKABLE QVector<Zone*> zonesNearEdge(const QPointF& point) const override;
    Q_INVOKABLE bool isNearZoneEdge(const QPointF& point, Zone* zone) const override;

    Q_INVOKABLE QRectF combineZoneGeometries(const QVector<Zone*>& zones) const override;

    // Note: Highlighting methods removed - use ZoneHighlighter instead
    // These methods are kept for backward compatibility but delegate to ZoneHighlighter
    Q_INVOKABLE void highlightZone(Zone* zone) override;
    Q_INVOKABLE void highlightZones(const QVector<Zone*>& zones) override;
    Q_INVOKABLE void clearHighlights() override;

    // Access to highlighter for direct use (SRP: delegate UI concerns)
    class ZoneHighlighter* highlighter() const
    {
        return m_highlighter.get();
    }

Q_SIGNALS:
    void layoutChanged();
    void adjacentThresholdChanged();
    void edgeThresholdChanged();
    void multiZoneEnabledChanged();
    void zoneHighlighted(Zone* zone);
    void zonesHighlighted(const QVector<Zone*>& zones);
    void highlightsCleared();

private:
    bool areZonesAdjacent(Zone* zone1, Zone* zone2) const;
    qreal distanceToZoneEdge(const QPointF& point, Zone* zone) const;

    Layout* m_layout = nullptr;
    qreal m_adjacentThreshold = 20.0; // Pixels from edge for adjacent detection
    qreal m_edgeThreshold = 10.0; // Pixels for edge detection
    bool m_multiZoneEnabled = true;

    // UI state management (separated concern - SRP)
    std::unique_ptr<class ZoneHighlighter> m_highlighter;
};

} // namespace PlasmaZones

// Q_DECLARE_METATYPE is in interfaces.h
