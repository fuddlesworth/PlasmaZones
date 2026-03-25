// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_zone_detector_overlap.cpp
 * @brief Unit tests for ZoneDetector with overlapping zones (sub-zones on background zones)
 *
 * Regression tests for the bug where spanning adjacent sub-zones (e.g. 7 & 9)
 * incorrectly pulled in a larger background zone (e.g. zone 2), making the
 * window larger than desired.
 */

#include <QTest>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QScopedPointer>

#include "core/layout.h"
#include "core/zone.h"
#include "core/zonedetector.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

class TestZoneDetectorOverlap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_settings = new StubSettings(nullptr);
        m_detector = new ZoneDetector(m_settings, nullptr);
        m_layout = new Layout(QStringLiteral("OverlapTest"), nullptr);
    }

    void cleanup()
    {
        delete m_detector;
        delete m_settings;
        delete m_layout;
        m_detector = nullptr;
        m_settings = nullptr;
        m_layout = nullptr;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: detectMultiZone should not include large background zone
    // when cursor is between two sub-zones on top of it
    // ═══════════════════════════════════════════════════════════════════════

    void testDetectMultiZone_subZonesOnBackground_excludesBackground()
    {
        // Layout on 1000x1000 screen:
        //   bgZone (background): full area (0,0)-(1000,1000)
        //   subLeft:  (0,0)-(490,300) — left sub-zone
        //   subRight: (510,0)-(1000,300) — right sub-zone
        //   Gap: 20px at x=490-510
        //
        // Cursor at (500, 150): in gap, inside bgZone, within threshold of both sub-zones

        auto* bgZone = new Zone(m_layout);
        bgZone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        m_layout->addZone(bgZone);

        auto* subLeft = new Zone(m_layout);
        subLeft->setRelativeGeometry(QRectF(0.0, 0.0, 0.49, 0.3));
        m_layout->addZone(subLeft);

        auto* subRight = new Zone(m_layout);
        subRight->setRelativeGeometry(QRectF(0.51, 0.0, 0.49, 0.3));
        m_layout->addZone(subRight);

        m_layout->recalculateZoneGeometries(QRectF(0, 0, 1000, 1000));
        m_detector->setLayout(m_layout);

        QPointF cursorInGap(500, 150);
        ZoneDetectionResult result = m_detector->detectMultiZone(cursorInGap);

        QVERIFY2(result.isMultiZone, "Should detect multi-zone at sub-zone border");

        bool hasLeft = result.adjacentZones.contains(subLeft);
        bool hasRight = result.adjacentZones.contains(subRight);
        bool hasBg = result.adjacentZones.contains(bgZone);

        QVERIFY2(hasLeft, "Should include left sub-zone");
        QVERIFY2(hasRight, "Should include right sub-zone");
        QVERIFY2(!hasBg, "Should NOT include large background zone");

        // Combined geometry should be the union of sub-zones only
        QRectF expectedGeom = subLeft->geometry().united(subRight->geometry());
        QCOMPARE(result.snapGeometry, expectedGeom);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: expandPaintedZonesToRect should not pull in background zone
    // ═══════════════════════════════════════════════════════════════════════

    void testExpandPaintedZones_subZonesOnBackground_excludesBackground()
    {
        // Sub-zones cover 30% height, background covers 100% — ratio < 0.5
        auto* bgZone = new Zone(m_layout);
        bgZone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        m_layout->addZone(bgZone);

        auto* subLeft = new Zone(m_layout);
        subLeft->setRelativeGeometry(QRectF(0.0, 0.0, 0.49, 0.3));
        m_layout->addZone(subLeft);

        auto* subRight = new Zone(m_layout);
        subRight->setRelativeGeometry(QRectF(0.51, 0.0, 0.49, 0.3));
        m_layout->addZone(subRight);

        m_layout->recalculateZoneGeometries(QRectF(0, 0, 1000, 1000));
        m_detector->setLayout(m_layout);

        QVector<Zone*> painted = {subLeft, subRight};
        QVector<Zone*> expanded = m_detector->expandPaintedZonesToRect(painted);

        QVERIFY2(!expanded.contains(bgZone), "expandPaintedZonesToRect should NOT pull in large background zone");
        QCOMPARE(expanded.size(), 2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: expandPaintedZones still fills gaps between non-overlapping zones
    // ═══════════════════════════════════════════════════════════════════════

    void testExpandPaintedZones_gapFilling_stillWorks()
    {
        auto* z1 = new Zone(m_layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.33, 1.0));
        m_layout->addZone(z1);

        auto* z2 = new Zone(m_layout);
        z2->setRelativeGeometry(QRectF(0.33, 0.0, 0.34, 1.0));
        m_layout->addZone(z2);

        auto* z3 = new Zone(m_layout);
        z3->setRelativeGeometry(QRectF(0.67, 0.0, 0.33, 1.0));
        m_layout->addZone(z3);

        m_layout->recalculateZoneGeometries(QRectF(0, 0, 900, 900));
        m_detector->setLayout(m_layout);

        QVector<Zone*> painted = {z1, z3};
        QVector<Zone*> expanded = m_detector->expandPaintedZonesToRect(painted);

        QCOMPARE(expanded.size(), 3);
        QVERIFY(expanded.contains(z1));
        QVERIFY2(expanded.contains(z2), "Gap zone should be included via expansion");
        QVERIFY(expanded.contains(z3));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P0: exact-50% boundary — sub-zones tile top half of background zone
    // The background zone's intersection ratio is exactly 0.5; it must be excluded.
    // ═══════════════════════════════════════════════════════════════════════

    void testExpandPaintedZones_exact50PercentOverlap_excludesBackground()
    {
        // bgZone: full screen (1.0 x 1.0)
        // subLeft / subRight: each 50% width, 50% height → bounding rect = 100% width, 50% height
        // Intersection ratio of bgZone = (W * 0.5H) / (W * H) = 0.5 exactly
        auto* bgZone = new Zone(m_layout);
        bgZone->setRelativeGeometry(QRectF(0.0, 0.0, 1.0, 1.0));
        m_layout->addZone(bgZone);

        auto* subLeft = new Zone(m_layout);
        subLeft->setRelativeGeometry(QRectF(0.0, 0.0, 0.49, 0.5));
        m_layout->addZone(subLeft);

        auto* subRight = new Zone(m_layout);
        subRight->setRelativeGeometry(QRectF(0.51, 0.0, 0.49, 0.5));
        m_layout->addZone(subRight);

        m_layout->recalculateZoneGeometries(QRectF(0, 0, 1000, 1000));
        m_detector->setLayout(m_layout);

        QVector<Zone*> painted = {subLeft, subRight};
        QVector<Zone*> expanded = m_detector->expandPaintedZonesToRect(painted);

        QVERIFY2(!expanded.contains(bgZone),
                 "Background zone at exactly 50% overlap ratio must be excluded (strict >)");
        QCOMPARE(expanded.size(), 2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: detectMultiZone with non-overlapping zones still works normally
    // ═══════════════════════════════════════════════════════════════════════

    void testDetectMultiZone_nonOverlapping_worksNormally()
    {
        auto* z1 = new Zone(m_layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.49, 1.0));
        m_layout->addZone(z1);

        auto* z2 = new Zone(m_layout);
        z2->setRelativeGeometry(QRectF(0.51, 0.0, 0.49, 1.0));
        m_layout->addZone(z2);

        m_layout->recalculateZoneGeometries(QRectF(0, 0, 1000, 1000));
        m_detector->setLayout(m_layout);

        QPointF cursorInGap(500, 500);
        ZoneDetectionResult result = m_detector->detectMultiZone(cursorInGap);

        QVERIFY2(result.isMultiZone, "Should detect multi-zone between adjacent zones");
        QCOMPARE(result.adjacentZones.size(), 2);
    }

private:
    StubSettings* m_settings = nullptr;
    ZoneDetector* m_detector = nullptr;
    Layout* m_layout = nullptr;
};

QTEST_MAIN(TestZoneDetectorOverlap)
#include "test_zone_detector_overlap.moc"
