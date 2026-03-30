// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"

namespace PlasmaZones {

/**
 * @brief Stub IZoneDetector for unit tests that need a WindowTrackingService
 *
 * All detection methods return empty/null results. Used by multiple test files
 * (snap assist, WTS lifecycle, virtual migration) to satisfy the constructor
 * dependency without requiring real zone detection logic.
 */
class StubZoneDetector : public IZoneDetector
{
    // No Q_OBJECT needed — this stub adds no signals/slots beyond IZoneDetector.
    // Omitting Q_OBJECT avoids MOC/vtable issues when included from multiple TUs.
public:
    explicit StubZoneDetector(QObject* parent = nullptr)
        : IZoneDetector(parent)
    {
    }
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override
    {
        m_layout = layout;
    }
    ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>&) const override
    {
        return {};
    }
    void highlightZone(Zone*) override
    {
    }
    void highlightZones(const QVector<Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    Layout* m_layout = nullptr;
};

/**
 * @brief Create a test layout with equally-spaced horizontal zones
 * @param zoneCount Number of zones to create
 * @param parent QObject parent (typically the LayoutManager)
 * @return Newly created Layout with zoneCount zones
 */
inline Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

} // namespace PlasmaZones
