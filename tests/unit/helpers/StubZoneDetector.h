// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

namespace PlasmaZones {

/**
 * @brief Stub PhosphorZones::IZoneDetector for unit tests that need a WindowTrackingService
 *
 * All detection methods return empty/null results. Used by multiple test files
 * (snap assist, WTS lifecycle, virtual migration) to satisfy the constructor
 * dependency without requiring real zone detection logic.
 */
class StubZoneDetector : public PhosphorZones::IZoneDetector
{
    // Q_OBJECT is intentionally omitted here. PhosphorZones::IZoneDetector already has Q_OBJECT
    // and provides the meta-object infrastructure (vtable, signal dispatch, MOC
    // data). A concrete subclass only needs Q_OBJECT if it declares new signals
    // or slots of its own. StubZoneDetector adds neither, so omitting Q_OBJECT
    // is correct and avoids vtable/ODR violations when this header is included
    // from multiple translation units (e.g. Unity builds). The existing signals
    // declared in PhosphorZones::IZoneDetector remain fully functional via the base meta-object.
public:
    explicit StubZoneDetector(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

/**
 * @brief Create a test layout with equally-spaced horizontal zones
 * @param zoneCount Number of zones to create
 * @param parent QObject parent (typically the PhosphorZones::LayoutManager)
 * @return Newly created PhosphorZones::Layout with zoneCount zones
 */
inline PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

} // namespace PlasmaZones
