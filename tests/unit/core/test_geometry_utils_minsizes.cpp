// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_minsizes.cpp
 * @brief Unit tests for GeometryUtils::enforceWindowMinSizes()
 *
 * Tests cover:
 * - No-op when minimums are empty or already satisfied
 * - Single-zone stealing from a neighbor
 * - Chain stealing across multiple columns (critical bug scenario)
 * - Height (vertical) chain stealing
 * - MasterStack-like layouts with multiple zones per column
 * - Unsatisfiable constraints (proportional fallback)
 * - Size mismatch early-return guard
 * - Gap threshold adjacency detection
 */

#include <QTest>
#include <QRect>
#include <QVector>
#include <QSize>

#include "core/geometryutils.h"
#include "core/constants.h"

using namespace PlasmaZones;

class TestGeometryUtilsMinSizes : public QObject
{
    Q_OBJECT

private:
    static bool allPositiveDimensions(const QVector<QRect>& zones)
    {
        for (const QRect& z : zones) {
            if (z.width() <= 0 || z.height() <= 0) {
                return false;
            }
        }
        return true;
    }

private Q_SLOTS:

    void test_noMinSizes_noChange()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_singleZoneBelowMin_stealsFromNeighbor()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("PhosphorZones::Zone[0] width %1 should be >= 400").arg(zones[0].width())));
        QVERIFY2(zones[1].width() > 0,
                 qPrintable(QStringLiteral("PhosphorZones::Zone[1] width %1 should be > 0").arg(zones[1].width())));
        QCOMPARE(zones[0].width() + zones[1].width(), 600);
    }

    void test_chainStealing_threeColumns()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize(350, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral(
                                "CHAIN STEAL FAILURE: PhosphorZones::Zone A width = %1, expected >= 400. "
                                "PhosphorZones::Zone B = %2, PhosphorZones::Zone C = %3. A cannot reach C through B.")
                                .arg(zones[0].width())
                                .arg(zones[1].width())
                                .arg(zones[2].width())));

        QVERIFY2(zones[1].width() >= 350,
                 qPrintable(QStringLiteral("PhosphorZones::Zone B width = %1, expected >= 350. "
                                           "PhosphorZones::Zone A = %2, PhosphorZones::Zone C = %3.")
                                .arg(zones[1].width())
                                .arg(zones[0].width())
                                .arg(zones[2].width())));

        QVERIFY2(zones[2].width() > 0,
                 qPrintable(QStringLiteral("PhosphorZones::Zone C width = %1, must be > 0").arg(zones[2].width())));

        const int totalWidth = zones[0].width() + zones[1].width() + zones[2].width();
        QVERIFY2(totalWidth == 900,
                 qPrintable(QStringLiteral("Total width %1 != 900 (A=%2, B=%3, C=%4)")
                                .arg(totalWidth)
                                .arg(zones[0].width())
                                .arg(zones[1].width())
                                .arg(zones[2].width())));
    }

    void test_chainStealing_fourColumns()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 250, 900),
            QRect(250, 0, 250, 900),
            QRect(500, 0, 250, 900),
            QRect(750, 0, 250, 900),
        };
        QVector<QSize> minSizes = {QSize(350, 1), QSize(300, 1), QSize(250, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        QVERIFY2(zones[0].width() >= 350,
                 qPrintable(QStringLiteral("PhosphorZones::Zone A width = %1, expected >= 350").arg(zones[0].width())));
        QVERIFY2(zones[1].width() >= 300,
                 qPrintable(QStringLiteral("PhosphorZones::Zone B width = %1, expected >= 300").arg(zones[1].width())));
        QVERIFY2(zones[2].width() >= 250,
                 qPrintable(QStringLiteral("PhosphorZones::Zone C width = %1, expected >= 250").arg(zones[2].width())));
        QVERIFY2(zones[3].width() > 0,
                 qPrintable(QStringLiteral("PhosphorZones::Zone D width = %1, must be > 0").arg(zones[3].width())));
    }

    void test_heightChainStealing_threeRows()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 900, 300),
            QRect(0, 300, 900, 300),
            QRect(0, 600, 900, 300),
        };
        QVector<QSize> minSizes = {QSize(1, 400), QSize(1, 350), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        QVERIFY2(zones[0].height() >= 400,
                 qPrintable(QStringLiteral("Row A height = %1, expected >= 400. "
                                           "Row B = %2, Row C = %3.")
                                .arg(zones[0].height())
                                .arg(zones[1].height())
                                .arg(zones[2].height())));

        QVERIFY2(zones[1].height() >= 350,
                 qPrintable(QStringLiteral("Row B height = %1, expected >= 350").arg(zones[1].height())));

        QVERIFY2(zones[2].height() > 0,
                 qPrintable(QStringLiteral("Row C height = %1, must be > 0").arg(zones[2].height())));
    }

    void test_masterStackLayout()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 600, 1080),
            QRect(600, 0, 300, 540),
            QRect(600, 540, 300, 540),
        };
        QVector<QSize> minSizes = {QSize(700, 1), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(zones[0].width() >= 700,
                 qPrintable(QStringLiteral("Master width = %1, expected >= 700").arg(zones[0].width())));

        QCOMPARE(zones[1].left(), zones[2].left());
        QCOMPARE(zones[1].width(), zones[2].width());

        QVERIFY2(zones[1].width() > 0,
                 qPrintable(QStringLiteral("Stack width = %1, must be > 0").arg(zones[1].width())));

        QCOMPARE(zones[0].width() + zones[1].width(), 900);
    }

    void test_unsatisfiableConstraints_proportional()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        QVector<QSize> minSizes = {QSize(500, 1), QSize(500, 1), QSize(500, 1)};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones),
                 "All zones must have positive dimensions even with unsatisfiable constraints");

        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(
                zones[i].width() > 0,
                qPrintable(
                    QStringLiteral("PhosphorZones::Zone[%1] width = %2, must be > 0").arg(i).arg(zones[i].width())));
        }
    }

    void test_noDeficit_noChange()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),
            QRect(500, 0, 400, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1), QSize(300, 1)};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_sizesMismatch_earlyReturn()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1)};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QCOMPARE(zones.size(), 2);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_gapThreshold_adjacencyDetection()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(308, 0, 292, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 10);

        QVERIFY2(
            zones[0].width() >= 400,
            qPrintable(QStringLiteral(
                           "PhosphorZones::Zone[0] width = %1, expected >= 400 (gap threshold should allow stealing)")
                           .arg(zones[0].width())));
    }

    void test_gapThreshold_tooFar()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(320, 0, 280, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 10);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_multipleZonesSameColumn()
    {
        QVector<QRect> zones = {
            QRect(0, 0, 400, 540),
            QRect(0, 540, 400, 540),
            QRect(400, 0, 400, 540),
            QRect(400, 540, 400, 540),
        };
        QVector<QSize> minSizes = {QSize(500, 1), QSize(500, 1), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(zones[0].width() >= 500,
                 qPrintable(QStringLiteral("Master top width = %1, expected >= 500").arg(zones[0].width())));
        QVERIFY2(zones[1].width() >= 500,
                 qPrintable(QStringLiteral("Master bottom width = %1, expected >= 500").arg(zones[1].width())));

        QCOMPARE(zones[0].left(), zones[1].left());
        QCOMPARE(zones[0].right(), zones[1].right());

        QCOMPARE(zones[2].left(), zones[3].left());
        QCOMPARE(zones[2].right(), zones[3].right());

        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(
                zones[i].width() > 0,
                qPrintable(
                    QStringLiteral("PhosphorZones::Zone[%1] width = %2, must be > 0").arg(i).arg(zones[i].width())));
        }
    }
};

QTEST_MAIN(TestGeometryUtilsMinSizes)
#include "test_geometry_utils_minsizes.moc"
