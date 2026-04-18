// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_overlaps.cpp
 * @brief Unit tests for GeometryUtils::removeZoneOverlaps and gap preservation
 */

#include <QTest>
#include <QRect>
#include <QVector>
#include <QSize>

#include "core/geometryutils.h"
#include "core/constants.h"

using namespace PlasmaZones;

class TestGeometryUtilsOverlaps : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // removeZoneOverlaps tests
    // ═══════════════════════════════════════════════════════════════════════════

    void test_noOverlap_noChange()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 400, 900),
            QRect(400, 0, 400, 900),
        };
        const QVector<QRect> original = zones;

        GeometryUtils::removeZoneOverlaps(zones);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_horizontalOverlap_resolved()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),
            QRect(400, 0, 500, 900),
        };

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY2(!zones[0].intersects(zones[1]),
                 qPrintable(QStringLiteral("Zones still overlap after removeZoneOverlaps: "
                                           "A(%1,%2,%3,%4) B(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[1].x())
                                .arg(zones[1].y())
                                .arg(zones[1].width())
                                .arg(zones[1].height())));

        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[1].width() > 0);
    }

    void test_verticalOverlap_resolved()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 900, 600),
            QRect(0, 500, 900, 600),
        };

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY2(!zones[0].intersects(zones[1]),
                 qPrintable(QStringLiteral("Zones still overlap vertically after removeZoneOverlaps: "
                                           "A(%1,%2,%3,%4) B(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[1].x())
                                .arg(zones[1].y())
                                .arg(zones[1].width())
                                .arg(zones[1].height())));

        QVERIFY(zones[0].height() > 0);
        QVERIFY(zones[1].height() > 0);
    }

    void test_singleZone_noChange()
    {
        QVector<QRect> zones = {QRect(0, 0, 900, 900)};
        const QVector<QRect> original = zones;

        GeometryUtils::removeZoneOverlaps(zones);

        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], original[0]);
    }

    void test_emptyZones_noChange()
    {
        QVector<QRect> zones;

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY(zones.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Gap preservation tests
    // ═══════════════════════════════════════════════════════════════════════════

    void test_overlapResolution_preservesGap()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),
            QRect(400, 0, 500, 900),
        };

        GeometryUtils::removeZoneOverlaps(zones, {}, /*innerGap=*/8);

        QVERIFY2(
            !zones[0].intersects(zones[1]),
            qPrintable(
                QStringLiteral("Zones overlap: A.right=%1, B.left=%2").arg(zones[0].right()).arg(zones[1].left())));

        int gap = zones[1].left() - (zones[0].left() + zones[0].width());
        QVERIFY2(gap >= 8, qPrintable(QStringLiteral("Gap between zones = %1, expected >= 8").arg(gap)));
    }

    void test_crossRowOverlapPrevention()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 500, 450),
            QRect(500, 0, 500, 450),
            QRect(0, 450, 400, 450),
            QRect(400, 450, 600, 450),
        };
        QVector<QSize> minSizes = {QSize(700, 1), QSize(), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/5);

        QVERIFY2(!zones[0].intersects(zones[3]),
                 qPrintable(QStringLiteral("Zone A overlaps D: A=(%1,%2,%3,%4) D=(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[3].x())
                                .arg(zones[3].y())
                                .arg(zones[3].width())
                                .arg(zones[3].height())));
    }

    void test_gapPreservedAfterMinSizeEnforcement()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(308, 0, 292, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/10, /*innerGap=*/8);

        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("Zone[0] width=%1, expected >= 400").arg(zones[0].width())));

        int gap = zones[1].left() - (zones[0].left() + zones[0].width());
        QVERIFY2(gap >= 6 && gap <= 10, qPrintable(QStringLiteral("Gap=%1, expected ~8px (6-10)").arg(gap)));
    }
};

QTEST_MAIN(TestGeometryUtilsOverlaps)
#include "test_geometry_utils_overlaps.moc"
