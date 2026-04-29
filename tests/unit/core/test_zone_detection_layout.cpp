// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_zone_detection_layout.cpp
 * @brief Unit tests for zone detection: zoneAtPoint, zonesInRect, nearestZone, zoneById
 */

#include <QTest>
#include <QString>
#include <QUuid>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QScopedPointer>

#include <PhosphorZones/Layout.h>
#include "core/layoutworker/layoutcomputeservice.h"
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;

class TestZoneDetectionLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), nullptr);
    }

    void cleanup()
    {
        delete m_layout;
        m_layout = nullptr;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: zoneAtPoint with overlapping zones
    // ═══════════════════════════════════════════════════════════════════════

    void testZoneAtPoint_overlappingZones_selectsSmallest()
    {
        auto* largeZone = new PhosphorZones::Zone(m_layout);
        largeZone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        largeZone->setZoneNumber(1);
        m_layout->addZone(largeZone);

        auto* smallZone = new PhosphorZones::Zone(m_layout);
        smallZone->setRelativeGeometry(QRectF(0.2, 0.2, 0.3, 0.3));
        smallZone->setZoneNumber(2);
        m_layout->addZone(smallZone);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 1000, 1000));

        QPointF point(350, 350);
        PhosphorZones::Zone* detected = m_layout->zoneAtPoint(point);

        QVERIFY(detected != nullptr);
        QCOMPARE(detected->zoneNumber(), 2);
    }

    void testZoneAtPoint_identicalSizeOverlap()
    {
        auto* zone1 = new PhosphorZones::Zone(m_layout);
        zone1->setRelativeGeometry(QRectF(0.1, 0.1, 0.5, 0.5));
        zone1->setZoneNumber(1);
        m_layout->addZone(zone1);

        auto* zone2 = new PhosphorZones::Zone(m_layout);
        zone2->setRelativeGeometry(QRectF(0.1, 0.1, 0.5, 0.5));
        zone2->setZoneNumber(2);
        m_layout->addZone(zone2);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 1000, 1000));

        QPointF point(350, 350);
        PhosphorZones::Zone* detected = m_layout->zoneAtPoint(point);

        QVERIFY(detected != nullptr);
        QCOMPARE(detected->zoneNumber(), 1);
    }

    void testZoneAtPoint_noContainingZone_returnsNull()
    {
        auto* zone = new PhosphorZones::Zone(m_layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 0.5));
        zone->setZoneNumber(1);
        m_layout->addZone(zone);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 1000, 1000));

        QPointF point(800, 800);
        PhosphorZones::Zone* detected = m_layout->zoneAtPoint(point);

        QVERIFY(detected == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: nearestZone
    // ═══════════════════════════════════════════════════════════════════════

    void testNearestZone_maxDistanceRespected()
    {
        auto* zone = new PhosphorZones::Zone(m_layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 0.1, 0.1));
        zone->setZoneNumber(1);
        m_layout->addZone(zone);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 1000, 1000));

        QPointF farPoint(900, 900);
        PhosphorZones::Zone* nearest = m_layout->nearestZone(farPoint, 50.0);

        QVERIFY(nearest == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: zonesInRect
    // ═══════════════════════════════════════════════════════════════════════

    void testZonesInRect_partialOverlap()
    {
        auto* zone1 = new PhosphorZones::Zone(m_layout);
        zone1->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 1.0));
        zone1->setZoneNumber(1);
        m_layout->addZone(zone1);

        auto* zone2 = new PhosphorZones::Zone(m_layout);
        zone2->setRelativeGeometry(QRectF(0.5, 0.0, 0.5, 1.0));
        zone2->setZoneNumber(2);
        m_layout->addZone(zone2);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 1000, 1000));

        QRectF queryRect(400, 100, 200, 200);
        QVector<PhosphorZones::Zone*> result = m_layout->zonesInRect(queryRect);

        QCOMPARE(result.size(), 2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: findZoneById with braces / without braces
    // ═══════════════════════════════════════════════════════════════════════

    void testFindZoneById_withBraces()
    {
        auto* zone = new PhosphorZones::Zone(m_layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        zone->setZoneNumber(1);
        m_layout->addZone(zone);

        QUuid id = zone->id();
        QString withBraces = id.toString(QUuid::WithBraces);
        QString withoutBraces = id.toString(QUuid::WithoutBraces);

        PhosphorZones::Zone* found1 = m_layout->zoneById(QUuid::fromString(withBraces));
        PhosphorZones::Zone* found2 = m_layout->zoneById(QUuid::fromString(withoutBraces));

        QVERIFY(found1 != nullptr);
        QVERIFY(found2 != nullptr);
        QCOMPARE(found1, found2);
        QCOMPARE(found1->zoneNumber(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P2: zoneById with null UUID
    // ═══════════════════════════════════════════════════════════════════════

    void testLayoutZoneById_nullUuid()
    {
        auto* zone = new PhosphorZones::Zone(m_layout);
        zone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        zone->setZoneNumber(1);
        m_layout->addZone(zone);

        PhosphorZones::Zone* result = m_layout->zoneById(QUuid());
        QVERIFY(result == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: Multi-zone snap cascade containment
    // ═══════════════════════════════════════════════════════════════════════

    void testMultiZoneSnap_cascadeContainedToAdjacentZones()
    {
        auto* z1 = new PhosphorZones::Zone(m_layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.33, 1.0));
        z1->setZoneNumber(1);
        m_layout->addZone(z1);

        auto* z2 = new PhosphorZones::Zone(m_layout);
        z2->setRelativeGeometry(QRectF(0.33, 0.0, 0.34, 1.0));
        z2->setZoneNumber(2);
        m_layout->addZone(z2);

        auto* z3 = new PhosphorZones::Zone(m_layout);
        z3->setRelativeGeometry(QRectF(0.67, 0.0, 0.33, 1.0));
        z3->setZoneNumber(3);
        m_layout->addZone(z3);

        LayoutComputeService::recalculateSync(m_layout, QRectF(0, 0, 900, 900));

        // Rect covering only zone 1 and zone 2 boundary (not zone 3).
        // With 900px width and zone boundaries at 0.33/0.67, zone edges are
        // approximately at 297 and 603. Rect from x=200, width=200 spans 200-400px.
        QRectF queryRect(200, 100, 200, 200);
        QVector<PhosphorZones::Zone*> result = m_layout->zonesInRect(queryRect);

        bool hasZone1 = false;
        bool hasZone2 = false;
        bool hasZone3 = false;
        for (PhosphorZones::Zone* z : result) {
            if (z->zoneNumber() == 1)
                hasZone1 = true;
            if (z->zoneNumber() == 2)
                hasZone2 = true;
            if (z->zoneNumber() == 3)
                hasZone3 = true;
        }
        QVERIFY(hasZone1);
        QVERIFY(hasZone2);
        QVERIFY(!hasZone3);
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

QTEST_MAIN(TestZoneDetectionLayout)
#include "test_zone_detection_layout.moc"
