// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_bsp.cpp
 * @brief BSP-specific regression tests for GeometryUtils
 */

#include <QTest>
#include <QRect>
#include <QVector>
#include <QSize>

#include "core/geometryutils.h"
#include "core/constants.h"

using namespace PlasmaZones;

class TestGeometryUtilsBsp : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void test_bspHierarchicalBoundaryShift()
    {
        // BSP 5-window layout: zones at different tree levels share a single
        // boundary (the root V-split at x=955). When a top-left zone steals
        // from a top-right zone, the boundary shift must propagate to bottom
        // zones too, or a gap appears between Discord and Kate.
        QVector<QRect> zones = {
            QRect(10, 10, 467, 525), // 0: App (top-left-left)
            QRect(487, 10, 468, 525), // 1: Steam (top-left-right)
            QRect(10, 545, 945, 525), // 2: Discord (bottom-left, full width)
            QRect(965, 10, 945, 525), // 3: Browser (top-right)
            QRect(965, 545, 945, 525), // 4: Kate (bottom-right)
        };
        QVector<QSize> minSizes = {
            QSize(), // App: no constraint
            QSize(500, 1), // Steam needs 500 (deficit of 32)
            QSize(), // Discord: no constraint
            QSize(), // Browser: no constraint
            QSize(), // Kate: no constraint
        };

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/22, /*innerGap=*/10);

        // Steam should satisfy its minimum
        QVERIFY2(zones[1].width() >= 500,
                 qPrintable(QStringLiteral("Steam width=%1, expected >= 500").arg(zones[1].width())));

        // The gap between Discord and Kate must stay consistent (== innerGap).
        int discordExclusiveRight = zones[2].left() + zones[2].width();
        int kateLeft = zones[4].left();
        int gap = kateLeft - discordExclusiveRight;
        QVERIFY2(gap == 10, qPrintable(QStringLiteral("Gap between Discord and Kate = %1, expected 10").arg(gap)));

        // Browser and Kate should have the same left edge (both in right half)
        QCOMPARE(zones[3].left(), zones[4].left());

        // Discord and Steam should have the same right edge (both in left half)
        QCOMPARE(zones[1].right(), zones[2].right());

        // No zone overlaps
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                QVERIFY2(!zones[i].intersects(zones[j]),
                         qPrintable(QStringLiteral("Zones %1 and %2 overlap").arg(i).arg(j)));
            }
        }
    }

    void test_bspLayout_perRowChainStealing()
    {
        // BSP layout: 3 windows in top row, 3 in bottom row with different
        // column boundaries.
        QVector<QRect> zones = {
            QRect(10, 10, 480, 520), // 0: Vesktop (top row)
            QRect(498, 10, 480, 520), // 1: Steam (top row)
            QRect(986, 10, 944, 520), // 2: Kate (top row)
            QRect(10, 538, 400, 520), // 3: Suno (bottom row)
            QRect(418, 538, 600, 520), // 4: Terminal (bottom row)
            QRect(1026, 538, 904, 520), // 5: Settings (bottom row)
        };
        QVector<QSize> minSizes = {
            QSize(940, 1), // Vesktop needs 940
            QSize(800, 1), // Steam needs 800
            QSize(), // Kate: no constraint
            QSize(), // Suno: no constraint
            QSize(), // Terminal: no constraint
            QSize(), // Settings: no constraint
        };

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/20, /*innerGap=*/8);

        // Steam can steal directly from Kate (adjacent, Kate has surplus)
        QVERIFY2(zones[1].width() >= 800,
                 qPrintable(QStringLiteral("Steam width=%1, expected >= 800").arg(zones[1].width())));

        // Vesktop: safety net can't chain-steal through Steam (Steam is at its
        // min after satisfying itself). Verify no corruption.
        QVERIFY2(zones[0].width() > 0,
                 qPrintable(QStringLiteral("Vesktop width=%1, expected > 0").arg(zones[0].width())));

        // No zone overlaps
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                QVERIFY2(!zones[i].intersects(zones[j]),
                         qPrintable(QStringLiteral("Zones %1 and %2 overlap: (%3,%4,%5,%6) vs (%7,%8,%9,%10)")
                                        .arg(i)
                                        .arg(j)
                                        .arg(zones[i].x())
                                        .arg(zones[i].y())
                                        .arg(zones[i].width())
                                        .arg(zones[i].height())
                                        .arg(zones[j].x())
                                        .arg(zones[j].y())
                                        .arg(zones[j].width())
                                        .arg(zones[j].height())));
            }
        }

        // Bottom row zones should be unchanged (different row, not affected)
        QCOMPARE(zones[3], QRect(10, 538, 400, 520));
        QCOMPARE(zones[4], QRect(418, 538, 600, 520));
        QCOMPARE(zones[5], QRect(1026, 538, 904, 520));
    }
};

QTEST_MAIN(TestGeometryUtilsBsp)
#include "test_geometry_utils_bsp.moc"
