// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_zones.cpp
 * @brief Unit tests for PhosphorZones::Layout zone operations, serialization, visibility, geometry cache
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <PhosphorZones/Layout.h>
#include "core/layoutworker/layoutcomputeservice.h"
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;

class TestLayoutZones : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::Layout* createLayoutWithZones(int zoneCount, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(QStringLiteral("test"), parent);
        for (int i = 0; i < zoneCount; ++i) {
            auto* zone = new PhosphorZones::Zone();
            zone->setRelativeGeometry(QRectF(qreal(i) / zoneCount, 0.0, 1.0 / zoneCount, 1.0));
            layout->addZone(zone);
        }
        return layout;
    }

    PhosphorZones::Zone* createZoneWithGeometry(const QRectF& geometry, QObject* parent = nullptr)
    {
        auto* zone = new PhosphorZones::Zone(parent);
        zone->setGeometry(geometry);
        zone->setRelativeGeometry(geometry);
        return zone;
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: zoneAtPoint
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_zoneAtPoint_overlappingZones_selectsSmallest()
    {
        PhosphorZones::Layout layout(QStringLiteral("Overlap"));

        auto* bigZone = createZoneWithGeometry(QRectF(0, 0, 1000, 1000));
        layout.addZone(bigZone);

        auto* smallZone = createZoneWithGeometry(QRectF(100, 100, 200, 200));
        layout.addZone(smallZone);

        PhosphorZones::Zone* result = layout.zoneAtPoint(QPointF(150, 150));
        QVERIFY(result != nullptr);
        QCOMPARE(result, smallZone);
    }

    void testLayout_zoneAtPoint_identicalSizeOverlap()
    {
        PhosphorZones::Layout layout(QStringLiteral("IdenticalOverlap"));

        auto* zone1 = createZoneWithGeometry(QRectF(0, 0, 100, 100));
        layout.addZone(zone1);

        auto* zone2 = createZoneWithGeometry(QRectF(50, 50, 100, 100));
        layout.addZone(zone2);

        PhosphorZones::Zone* result = layout.zoneAtPoint(QPointF(75, 75));
        QVERIFY(result != nullptr);
        QCOMPARE(result, zone1);
    }

    void testLayout_zoneAtPoint_noContainingZone_returnsNull()
    {
        PhosphorZones::Layout layout(QStringLiteral("NoMatch"));

        auto* zone = createZoneWithGeometry(QRectF(0, 0, 100, 100));
        layout.addZone(zone);

        PhosphorZones::Zone* result = layout.zoneAtPoint(QPointF(500, 500));
        QVERIFY(result == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: PhosphorZones::Zone add/remove renumbering
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_addZone_renumbersZones()
    {
        PhosphorZones::Layout layout(QStringLiteral("Renumber"));

        auto* z1 = new PhosphorZones::Zone();
        z1->setRelativeGeometry(QRectF(0, 0, 0.5, 1));
        layout.addZone(z1);
        QCOMPARE(z1->zoneNumber(), 1);

        auto* z2 = new PhosphorZones::Zone();
        z2->setRelativeGeometry(QRectF(0.5, 0, 0.5, 1));
        layout.addZone(z2);
        QCOMPARE(z2->zoneNumber(), 2);

        QCOMPARE(layout.zone(0)->zoneNumber(), 1);
        QCOMPARE(layout.zone(1)->zoneNumber(), 2);
    }

    void testLayout_removeZone_renumbersRemaining()
    {
        PhosphorZones::Layout layout(QStringLiteral("RemoveRenumber"));

        auto* z1 = new PhosphorZones::Zone();
        z1->setRelativeGeometry(QRectF(0, 0, 0.33, 1));
        layout.addZone(z1);

        auto* z2 = new PhosphorZones::Zone();
        z2->setRelativeGeometry(QRectF(0.33, 0, 0.33, 1));
        layout.addZone(z2);

        auto* z3 = new PhosphorZones::Zone();
        z3->setRelativeGeometry(QRectF(0.66, 0, 0.34, 1));
        layout.addZone(z3);

        QCOMPARE(z3->zoneNumber(), 3);

        layout.removeZone(z2);

        QCOMPARE(layout.zoneCount(), 2);
        QCOMPARE(layout.zone(0)->zoneNumber(), 1);
        QCOMPARE(layout.zone(1)->zoneNumber(), 2);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Serialization roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_serialization_roundtrip_allProperties()
    {
        PhosphorZones::Layout original(QStringLiteral("Roundtrip Test"));
        original.setDescription(QStringLiteral("A test layout"));
        original.setZonePadding(12);
        original.setOuterGap(8);
        original.setUsePerSideOuterGap(true);
        original.setOuterGapTop(5);
        original.setOuterGapBottom(10);
        original.setOuterGapLeft(15);
        original.setOuterGapRight(20);
        original.setShowZoneNumbers(false);
        original.setHiddenFromSelector(true);
        original.setAllowedScreens({QStringLiteral("screen1"), QStringLiteral("screen2")});
        original.setAllowedDesktops({1, 3, 5});
        original.setAllowedActivities({QStringLiteral("activity1")});
        original.setAutoAssign(true);
        original.setUseFullScreenGeometry(true);

        auto* zone1 = new PhosphorZones::Zone();
        zone1->setRelativeGeometry(QRectF(0, 0, 0.5, 1));
        zone1->setName(QStringLiteral("Left"));
        original.addZone(zone1);

        auto* zone2 = new PhosphorZones::Zone();
        zone2->setRelativeGeometry(QRectF(0.5, 0, 0.5, 1));
        zone2->setName(QStringLiteral("Right"));
        original.addZone(zone2);

        QJsonObject json = original.toJson();

        PhosphorZones::Layout* restored = PhosphorZones::Layout::fromJson(json);
        QVERIFY(restored != nullptr);

        QCOMPARE(restored->id(), original.id());
        QCOMPARE(restored->name(), QStringLiteral("Roundtrip Test"));
        QCOMPARE(restored->description(), QStringLiteral("A test layout"));
        QCOMPARE(restored->zonePadding(), 12);
        QCOMPARE(restored->outerGap(), 8);
        QVERIFY(restored->usePerSideOuterGap());
        QCOMPARE(restored->outerGapTop(), 5);
        QCOMPARE(restored->outerGapBottom(), 10);
        QCOMPARE(restored->outerGapLeft(), 15);
        QCOMPARE(restored->outerGapRight(), 20);
        QVERIFY(!restored->showZoneNumbers());
        QVERIFY(restored->hiddenFromSelector());
        QCOMPARE(restored->allowedScreens(), QStringList({QStringLiteral("screen1"), QStringLiteral("screen2")}));
        QCOMPARE(restored->allowedDesktops(), QList<int>({1, 3, 5}));
        QCOMPARE(restored->allowedActivities(), QStringList({QStringLiteral("activity1")}));
        QVERIFY(restored->autoAssign());
        QVERIFY(restored->useFullScreenGeometry());
        QCOMPARE(restored->zoneCount(), 2);
        QCOMPARE(restored->zone(0)->name(), QStringLiteral("Left"));
        QCOMPARE(restored->zone(1)->name(), QStringLiteral("Right"));

        delete restored;
    }

    void testLayout_serialization_perSideGaps_minus1_preserved()
    {
        PhosphorZones::Layout original(QStringLiteral("Gaps"));
        original.setUsePerSideOuterGap(true);
        original.setOuterGapTop(10);

        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        original.addZone(zone);

        QJsonObject json = original.toJson();

        PhosphorZones::Layout* restored = PhosphorZones::Layout::fromJson(json);
        QVERIFY(restored != nullptr);

        QCOMPARE(restored->outerGapTop(), 10);
        QCOMPARE(restored->outerGapBottom(), -1);
        QCOMPARE(restored->outerGapLeft(), -1);
        QCOMPARE(restored->outerGapRight(), -1);

        delete restored;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Visibility filtering
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_visibility_hiddenFromSelector()
    {
        PhosphorZones::Layout layout(QStringLiteral("Hidden"));
        QVERIFY(!layout.hiddenFromSelector());

        layout.setHiddenFromSelector(true);
        QVERIFY(layout.hiddenFromSelector());

        QSignalSpy spy(&layout, &PhosphorZones::Layout::hiddenFromSelectorChanged);
        layout.setHiddenFromSelector(false);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!layout.hiddenFromSelector());
    }

    void testLayout_visibility_allowedScreens_emptyMeansAll()
    {
        PhosphorZones::Layout layout(QStringLiteral("AllScreens"));
        QVERIFY(layout.allowedScreens().isEmpty());

        layout.setAllowedScreens({QStringLiteral("DP-1")});
        QCOMPARE(layout.allowedScreens().size(), 1);

        layout.setAllowedScreens({});
        QVERIFY(layout.allowedScreens().isEmpty());
    }

    void testLayout_visibility_allowedDesktops_emptyMeansAll()
    {
        PhosphorZones::Layout layout(QStringLiteral("AllDesktops"));
        QVERIFY(layout.allowedDesktops().isEmpty());

        layout.setAllowedDesktops({1, 2, 3});
        QCOMPARE(layout.allowedDesktops().size(), 3);

        layout.setAllowedDesktops({});
        QVERIFY(layout.allowedDesktops().isEmpty());
    }

    void testLayout_visibility_allowedActivities_emptyMeansAll()
    {
        PhosphorZones::Layout layout(QStringLiteral("AllActivities"));
        QVERIFY(layout.allowedActivities().isEmpty());

        layout.setAllowedActivities({QStringLiteral("act1"), QStringLiteral("act2")});
        QCOMPARE(layout.allowedActivities().size(), 2);

        layout.setAllowedActivities({});
        QVERIFY(layout.allowedActivities().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Geometry cache
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_recalculateGeometries_cachedGeometry_noRedundantWork()
    {
        PhosphorZones::Layout layout(QStringLiteral("Cache"));
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout.addZone(zone);

        QRectF screenGeom(0, 0, 1920, 1080);

        LayoutComputeService::recalculateSync(&layout, screenGeom);
        QRectF firstGeom = zone->geometry();
        QVERIFY(!firstGeom.isEmpty());

        QCOMPARE(layout.lastRecalcGeometry(), screenGeom);

        zone->setGeometry(QRectF(0, 0, 100, 100));
        LayoutComputeService::recalculateSync(&layout, screenGeom);
        QCOMPARE(zone->geometry(), QRectF(0, 0, 100, 100));

        QRectF newScreenGeom(0, 0, 2560, 1440);
        LayoutComputeService::recalculateSync(&layout, newScreenGeom);
        QCOMPARE(layout.lastRecalcGeometry(), newScreenGeom);
        QCOMPARE(zone->geometry(), QRectF(0, 0, 2560, 1440));
    }
};

QTEST_MAIN(TestLayoutZones)
#include "test_layout_zones.moc"
