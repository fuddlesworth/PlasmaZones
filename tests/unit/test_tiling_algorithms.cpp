// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_algorithms.cpp
 * @brief Unit tests for all built-in tiling algorithms
 *
 * Tests BSP, Columns, Fibonacci, Master-Stack, Monocle, and Three Column
 * algorithms plus generic validation (valid rects, no overlap, total area = 1.0).
 */

#include <QTest>
#include <QRectF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "core/tilingalgorithm.h"
#include "core/algorithms/bspalgorithm.h"
#include "core/algorithms/fibonaccialgorithm.h"
#include "core/algorithms/masterstackalgorithm.h"
#include "core/algorithms/monoclealgorithm.h"
#include "core/algorithms/threecolumnalgorithm.h"

using namespace PlasmaZones;

static constexpr qreal EPSILON = 1e-9;

class TestTilingAlgorithms : public QObject
{
    Q_OBJECT

private:
    static void verifyValidRects(const QVector<QRectF>& zones, const QString& ctx = {})
    {
        for (int i = 0; i < zones.size(); ++i) {
            const auto& z = zones[i];
            QVERIFY2(z.x() >= -EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 x < 0: %3").arg(ctx).arg(i).arg(z.x())));
            QVERIFY2(z.y() >= -EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 y < 0: %3").arg(ctx).arg(i).arg(z.y())));
            QVERIFY2(z.right() <= 1.0 + EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 right > 1: %3").arg(ctx).arg(i).arg(z.right())));
            QVERIFY2(z.bottom() <= 1.0 + EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 bottom > 1: %3").arg(ctx).arg(i).arg(z.bottom())));
            QVERIFY2(z.width() > EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 width too small: %3").arg(ctx).arg(i).arg(z.width())));
            QVERIFY2(z.height() > EPSILON,
                     qPrintable(QStringLiteral("%1 zone %2 height too small: %3").arg(ctx).arg(i).arg(z.height())));
        }
    }

    static void verifyTotalArea(const QVector<QRectF>& zones, const QString& ctx = {})
    {
        qreal totalArea = 0.0;
        for (const auto& z : zones) {
            totalArea += z.width() * z.height();
        }
        QVERIFY2(std::abs(totalArea - 1.0) < EPSILON,
                 qPrintable(QStringLiteral("%1 total area = %2, expected 1.0").arg(ctx).arg(totalArea, 0, 'g', 15)));
    }

    static void verifyNoOverlap(const QVector<QRectF>& zones, const QString& ctx = {})
    {
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                const QRectF overlap = zones[i].intersected(zones[j]);
                if (overlap.isValid() && overlap.width() > EPSILON && overlap.height() > EPSILON) {
                    QFAIL(qPrintable(QStringLiteral("%1 zones %2 and %3 overlap (area=%4)")
                                         .arg(ctx).arg(i).arg(j).arg(overlap.width() * overlap.height())));
                }
            }
        }
    }

    static void verifyAll(const QVector<QRectF>& zones, const QString& ctx = {})
    {
        verifyValidRects(zones, ctx);
        verifyTotalArea(zones, ctx);
        verifyNoOverlap(zones, ctx);
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // Registry
    // ═══════════════════════════════════════════════════════════════════════════

    void testRegistryContainsAllAlgorithms()
    {
        auto* reg = TilingAlgorithmRegistry::instance();
        auto ids = reg->algorithmIds();
        QVERIFY(ids.contains(QStringLiteral("bsp")));
        QVERIFY(ids.contains(QStringLiteral("columns")));
        QVERIFY(ids.contains(QStringLiteral("fibonacci")));
        QVERIFY(ids.contains(QStringLiteral("master-stack")));
        QVERIFY(ids.contains(QStringLiteral("monocle")));
        QVERIFY(ids.contains(QStringLiteral("three-column")));
        QCOMPARE(ids.size(), 6);
    }

    void testRegistryUniqueIds()
    {
        auto* reg = TilingAlgorithmRegistry::instance();
        auto ids = reg->algorithmIds();
        QSet<QString> unique(ids.begin(), ids.end());
        QCOMPARE(unique.size(), ids.size());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Monocle
    // ═══════════════════════════════════════════════════════════════════════════

    void testMonocleEmpty()
    {
        MonocleTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testMonocleSingle()
    {
        MonocleTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testMonocleMany()
    {
        MonocleTilingAlgorithm algo;
        for (int n : {2, 5, 10, 100}) {
            auto zones = algo.generateZones(n, {0.6, 3});
            QCOMPARE(zones.size(), 1);
            QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
        }
    }

    void testMonocleId()
    {
        MonocleTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("monocle"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Master-Stack
    // ═══════════════════════════════════════════════════════════════════════════

    void testMasterStackEmpty()
    {
        MasterStackTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testMasterStackSingle()
    {
        MasterStackTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testMasterStackStandard()
    {
        MasterStackTilingAlgorithm algo;
        TilingParams params{0.6, 1};
        auto zones = algo.generateZones(4, params);

        QCOMPARE(zones.size(), 4);
        verifyAll(zones, QStringLiteral("MasterStack(4,0.6)"));

        // Master zone: left 60%
        QCOMPARE(zones[0].x(), 0.0);
        QVERIFY(std::abs(zones[0].width() - 0.6) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 1.0) < EPSILON);

        // Stack zones: right 40%, 3 equal rows
        for (int i = 1; i <= 3; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.6) < EPSILON);
            QVERIFY(std::abs(zones[i].width() - 0.4) < EPSILON);
        }
    }

    void testMasterStackMultipleMasters()
    {
        MasterStackTilingAlgorithm algo;
        TilingParams params{0.55, 2};
        auto zones = algo.generateZones(5, params);

        QCOMPARE(zones.size(), 5);
        verifyAll(zones, QStringLiteral("MasterStack(5,0.55,2)"));

        // 2 master zones on left
        QCOMPARE(zones[0].x(), 0.0);
        QCOMPARE(zones[1].x(), 0.0);
        QVERIFY(std::abs(zones[0].width() - 0.55) < EPSILON);
        QVERIFY(std::abs(zones[1].width() - 0.55) < EPSILON);

        // 3 stack zones on right
        for (int i = 2; i < 5; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.55) < EPSILON);
        }
    }

    void testMasterStackAllMasters()
    {
        MasterStackTilingAlgorithm algo;
        TilingParams params{0.5, 5};
        auto zones = algo.generateZones(3, params);

        // masterCount clamped to windowCount=3, stackCount=0, full width
        QCOMPARE(zones.size(), 3);
        verifyAll(zones, QStringLiteral("MasterStack(3,allMasters)"));
        for (const auto& z : zones) {
            QVERIFY(std::abs(z.width() - 1.0) < EPSILON);
        }
    }

    void testMasterStackMasterCountZero()
    {
        MasterStackTilingAlgorithm algo;
        TilingParams params{0.5, 0};
        auto zones = algo.generateZones(3, params);

        // masterCount=0 clamped to 1 → 1 master + 2 stack
        QCOMPARE(zones.size(), 3);
        verifyAll(zones, QStringLiteral("MasterStack(3,mc=0)"));
        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 1.0) < EPSILON);
    }

    void testMasterStackMasterRatioBoundaries()
    {
        MasterStackTilingAlgorithm algo;

        // masterRatio at documented min
        {
            TilingParams params{0.1, 1};
            auto zones = algo.generateZones(3, params);
            QCOMPARE(zones.size(), 3);
            verifyAll(zones, QStringLiteral("MasterStack(3,mr=0.1)"));
            QVERIFY(std::abs(zones[0].width() - 0.1) < EPSILON);
        }

        // masterRatio at documented max
        {
            TilingParams params{0.9, 1};
            auto zones = algo.generateZones(3, params);
            QCOMPARE(zones.size(), 3);
            verifyAll(zones, QStringLiteral("MasterStack(3,mr=0.9)"));
            QVERIFY(std::abs(zones[0].width() - 0.9) < EPSILON);
        }

        // masterRatio=0.0 clamped to 0.1
        {
            TilingParams params{0.0, 1};
            auto zones = algo.generateZones(3, params);
            QCOMPARE(zones.size(), 3);
            verifyAll(zones, QStringLiteral("MasterStack(3,mr=0.0)"));
            QVERIFY(std::abs(zones[0].width() - 0.1) < EPSILON);
        }

        // masterRatio=1.0 clamped to 0.9
        {
            TilingParams params{1.0, 1};
            auto zones = algo.generateZones(3, params);
            QCOMPARE(zones.size(), 3);
            verifyAll(zones, QStringLiteral("MasterStack(3,mr=1.0)"));
            QVERIFY(std::abs(zones[0].width() - 0.9) < EPSILON);
        }
    }

    void testMasterStackId()
    {
        MasterStackTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("master-stack"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Three Column
    // ═══════════════════════════════════════════════════════════════════════════

    void testThreeColumnEmpty()
    {
        ThreeColumnTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testThreeColumnSingle()
    {
        ThreeColumnTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testThreeColumnTwo()
    {
        ThreeColumnTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(2, params);

        QCOMPARE(zones.size(), 2);
        verifyAll(zones, QStringLiteral("ThreeColumn(2)"));

        // No left column → center starts at x=0
        QVERIFY(std::abs(zones[0].x() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);

        // Right stack fills remaining space
        QVERIFY(std::abs(zones[1].x() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[1].width() - 0.5) < EPSILON);
    }

    void testThreeColumnStandard()
    {
        ThreeColumnTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(6, params);

        QCOMPARE(zones.size(), 6);
        verifyAll(zones, QStringLiteral("ThreeColumn(6)"));

        // Zone 0: center master (50% width)
        QVERIFY(std::abs(zones[0].x() - 0.25) < EPSILON);
        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 1.0) < EPSILON);

        // stackCount=5, rightCount=3, leftCount=2
        // Zones 1-3: right column
        for (int i = 1; i <= 3; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.75) < EPSILON);
            QVERIFY(std::abs(zones[i].width() - 0.25) < EPSILON);
        }

        // Zones 4-5: left column
        for (int i = 4; i <= 5; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.0) < EPSILON);
            QVERIFY(std::abs(zones[i].width() - 0.25) < EPSILON);
        }
    }

    void testThreeColumnEvenStack()
    {
        ThreeColumnTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(5, params);

        // stackCount=4, rightCount=2, leftCount=2 (symmetric)
        QCOMPARE(zones.size(), 5);
        verifyAll(zones, QStringLiteral("ThreeColumn(5,even)"));

        // Center master
        QVERIFY(std::abs(zones[0].x() - 0.25) < EPSILON);
        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);

        // Right column: 2 zones
        for (int i = 1; i <= 2; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.75) < EPSILON);
            QVERIFY(std::abs(zones[i].width() - 0.25) < EPSILON);
        }

        // Left column: 2 zones (symmetric with right)
        for (int i = 3; i <= 4; ++i) {
            QVERIFY(std::abs(zones[i].x() - 0.0) < EPSILON);
            QVERIFY(std::abs(zones[i].width() - 0.25) < EPSILON);
        }
    }

    void testThreeColumnAllMasters()
    {
        ThreeColumnTilingAlgorithm algo;
        TilingParams params{0.5, 10};
        auto zones = algo.generateZones(3, params);

        // masterCount clamped to 3, stackCount=0, full width rows
        QCOMPARE(zones.size(), 3);
        verifyAll(zones, QStringLiteral("ThreeColumn(3,allMasters)"));
        for (const auto& z : zones) {
            QVERIFY(std::abs(z.width() - 1.0) < EPSILON);
        }
    }

    void testThreeColumnMasterCountZero()
    {
        ThreeColumnTilingAlgorithm algo;
        TilingParams params{0.5, 0};
        auto zones = algo.generateZones(4, params);

        // masterCount=0 clamped to 1
        QCOMPARE(zones.size(), 4);
        verifyAll(zones, QStringLiteral("ThreeColumn(4,mc=0)"));
    }

    void testThreeColumnId()
    {
        ThreeColumnTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("three-column"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BSP
    // ═══════════════════════════════════════════════════════════════════════════

    void testBspEmpty()
    {
        BSPTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testBspSingle()
    {
        BSPTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testBspTwo()
    {
        BSPTilingAlgorithm algo;
        TilingParams params{0.55, 1};
        auto zones = algo.generateZones(2, params);

        QCOMPARE(zones.size(), 2);
        verifyAll(zones, QStringLiteral("BSP(2)"));

        // First split at masterRatio=0.55
        QVERIFY(std::abs(zones[0].width() - 0.55) < EPSILON);
        QVERIFY(std::abs(zones[1].width() - 0.45) < EPSILON);
    }

    void testBspFiveStructure()
    {
        BSPTilingAlgorithm algo;
        TilingParams params{0.55, 1};
        auto zones = algo.generateZones(5, params);

        QCOMPARE(zones.size(), 5);
        verifyAll(zones, QStringLiteral("BSP(5)"));

        // BFS order:
        // zone[0] = top-left     (0, 0, 0.55, 0.5)
        // zone[1] = bottom-left  (0, 0.5, 0.55, 0.5)
        // zone[2] = top-right    (0.55, 0, 0.45, 0.5)
        // zone[3] = bottom-mid   (0.55, 0.5, 0.225, 0.5)
        // zone[4] = bottom-right (0.775, 0.5, 0.225, 0.5)
        QVERIFY(std::abs(zones[0].x() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[0].y() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[0].width() - 0.55) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 0.5) < EPSILON);

        QVERIFY(std::abs(zones[1].x() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[1].y() - 0.5) < EPSILON);

        QVERIFY(std::abs(zones[2].x() - 0.55) < EPSILON);
        QVERIFY(std::abs(zones[2].y() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[2].width() - 0.45) < EPSILON);

        QVERIFY(std::abs(zones[3].x() - 0.55) < EPSILON);
        QVERIFY(std::abs(zones[3].y() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[3].width() - 0.225) < EPSILON);

        QVERIFY(std::abs(zones[4].x() - 0.775) < EPSILON);
        QVERIFY(std::abs(zones[4].y() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[4].width() - 0.225) < EPSILON);
    }

    void testBspBalanced()
    {
        BSPTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(4, params);

        QCOMPARE(zones.size(), 4);
        verifyAll(zones, QStringLiteral("BSP(4)"));

        // With 50/50 ratio, 4 zones should each be 0.25 area
        for (const auto& z : zones) {
            QVERIFY(std::abs(z.width() * z.height() - 0.25) < 0.01);
        }
    }

    void testBspDeeperLevelsUseEqualSplit()
    {
        BSPTilingAlgorithm algo;
        TilingParams params{0.7, 1};
        auto zones = algo.generateZones(4, params);

        QCOMPARE(zones.size(), 4);
        verifyAll(zones, QStringLiteral("BSP(4,mr=0.7)"));

        // Depth 0: vertical split at 0.7 → left 70% (2), right 30% (2)
        // Depth 1: horizontal split at 0.5 for each
        // Left zones: 0.7 * 0.5 = 0.35 area each
        // Right zones: 0.3 * 0.5 = 0.15 area each
        QVERIFY(std::abs(zones[0].width() * zones[0].height() - 0.35) < 0.01);
        QVERIFY(std::abs(zones[1].width() * zones[1].height() - 0.35) < 0.01);
        QVERIFY(std::abs(zones[2].width() * zones[2].height() - 0.15) < 0.01);
        QVERIFY(std::abs(zones[3].width() * zones[3].height() - 0.15) < 0.01);
    }

    void testBspMasterRatioBoundaries()
    {
        BSPTilingAlgorithm algo;

        // masterRatio=0.0 → clamped to 0.1
        {
            TilingParams params{0.0, 1};
            auto zones = algo.generateZones(2, params);
            QCOMPARE(zones.size(), 2);
            verifyAll(zones, QStringLiteral("BSP(2,mr=0.0)"));
            QVERIFY(std::abs(zones[0].width() - 0.1) < EPSILON);
        }

        // masterRatio=1.0 → clamped to 0.9
        {
            TilingParams params{1.0, 1};
            auto zones = algo.generateZones(2, params);
            QCOMPARE(zones.size(), 2);
            verifyAll(zones, QStringLiteral("BSP(2,mr=1.0)"));
            QVERIFY(std::abs(zones[0].width() - 0.9) < EPSILON);
        }
    }

    void testBspId()
    {
        BSPTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("bsp"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Fibonacci
    // ═══════════════════════════════════════════════════════════════════════════

    void testFibonacciEmpty()
    {
        FibonacciTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testFibonacciSingle()
    {
        FibonacciTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testFibonacciTwo()
    {
        FibonacciTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(2, params);

        QCOMPARE(zones.size(), 2);
        verifyAll(zones, QStringLiteral("Fibonacci(2)"));

        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 1.0) < EPSILON);
        QVERIFY(std::abs(zones[1].x() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[1].width() - 0.5) < EPSILON);
    }

    void testFibonacciFiveSpiralStructure()
    {
        FibonacciTilingAlgorithm algo;
        TilingParams params{0.5, 1};
        auto zones = algo.generateZones(5, params);

        QCOMPARE(zones.size(), 5);
        verifyAll(zones, QStringLiteral("Fibonacci(5)"));

        // Spiral: left → top → left → top → remaining
        // zone[0]: master left half  (0, 0, 0.5, 1.0)
        // zone[1]: top-right         (0.5, 0, 0.5, 0.5)
        // zone[2]: bottom-mid-left   (0.5, 0.5, 0.25, 0.5)
        // zone[3]: bottom-right-top  (0.75, 0.5, 0.25, 0.25)
        // zone[4]: bottom-right-bot  (0.75, 0.75, 0.25, 0.25)
        QVERIFY(std::abs(zones[0].x() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[0].y() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[0].width() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[0].height() - 1.0) < EPSILON);

        QVERIFY(std::abs(zones[1].x() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[1].y() - 0.0) < EPSILON);
        QVERIFY(std::abs(zones[1].width() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[1].height() - 0.5) < EPSILON);

        QVERIFY(std::abs(zones[2].x() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[2].y() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[2].width() - 0.25) < EPSILON);
        QVERIFY(std::abs(zones[2].height() - 0.5) < EPSILON);

        QVERIFY(std::abs(zones[3].x() - 0.75) < EPSILON);
        QVERIFY(std::abs(zones[3].y() - 0.5) < EPSILON);
        QVERIFY(std::abs(zones[3].width() - 0.25) < EPSILON);
        QVERIFY(std::abs(zones[3].height() - 0.25) < EPSILON);

        QVERIFY(std::abs(zones[4].x() - 0.75) < EPSILON);
        QVERIFY(std::abs(zones[4].y() - 0.75) < EPSILON);

        // Monotonically decreasing areas (including zone[0] vs zone[1])
        for (int i = 1; i < zones.size(); ++i) {
            qreal prevArea = zones[i - 1].width() * zones[i - 1].height();
            qreal currArea = zones[i].width() * zones[i].height();
            QVERIFY2(currArea <= prevArea + EPSILON,
                     qPrintable(QStringLiteral("Fibonacci zone %1 area (%2) > zone %3 area (%4)")
                                    .arg(i).arg(currArea).arg(i - 1).arg(prevArea)));
        }
    }

    void testFibonacciMasterRatio()
    {
        FibonacciTilingAlgorithm algo;
        TilingParams params{0.7, 1};
        auto zones = algo.generateZones(3, params);

        QCOMPARE(zones.size(), 3);
        verifyAll(zones, QStringLiteral("Fibonacci(3,mr=0.7)"));
        QVERIFY(std::abs(zones[0].width() - 0.7) < EPSILON);
    }

    void testFibonacciId()
    {
        FibonacciTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("fibonacci"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Columns
    // ═══════════════════════════════════════════════════════════════════════════

    void testColumnsEmpty()
    {
        ColumnsTilingAlgorithm algo;
        QCOMPARE(algo.generateZones(0, {}), QVector<QRectF>());
    }

    void testColumnsSingle()
    {
        ColumnsTilingAlgorithm algo;
        auto zones = algo.generateZones(1, {});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], QRectF(0.0, 0.0, 1.0, 1.0));
    }

    void testColumnsStandard()
    {
        ColumnsTilingAlgorithm algo;
        auto zones = algo.generateZones(4, {});

        QCOMPARE(zones.size(), 4);
        verifyAll(zones, QStringLiteral("Columns(4)"));

        for (int i = 0; i < 4; ++i) {
            QVERIFY(std::abs(zones[i].width() - 0.25) < EPSILON);
            QVERIFY(std::abs(zones[i].height() - 1.0) < EPSILON);
            QVERIFY(std::abs(zones[i].x() - i * 0.25) < EPSILON);
        }
    }

    void testColumnsId()
    {
        ColumnsTilingAlgorithm algo;
        QCOMPARE(algo.id(), QStringLiteral("columns"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Generic validation across all algorithms
    // ═══════════════════════════════════════════════════════════════════════════

    void testAllAlgorithmsGenericValidation()
    {
        std::vector<std::unique_ptr<TilingAlgorithm>> algos;
        algos.push_back(std::make_unique<BSPTilingAlgorithm>());
        algos.push_back(std::make_unique<ColumnsTilingAlgorithm>());
        algos.push_back(std::make_unique<FibonacciTilingAlgorithm>());
        algos.push_back(std::make_unique<MasterStackTilingAlgorithm>());
        algos.push_back(std::make_unique<MonocleTilingAlgorithm>());
        algos.push_back(std::make_unique<ThreeColumnTilingAlgorithm>());

        TilingParams params{0.55, 1};

        for (const auto& algo : algos) {
            const QString id = algo->id();

            // Empty
            auto empty = algo->generateZones(0, params);
            QVERIFY2(empty.isEmpty(),
                     qPrintable(QStringLiteral("%1: expected empty for windowCount=0").arg(id)));

            // Single
            auto single = algo->generateZones(1, params);
            QVERIFY2(single.size() == 1,
                     qPrintable(QStringLiteral("%1: expected 1 zone for windowCount=1, got %2")
                                    .arg(id).arg(single.size())));
            QVERIFY2(single[0] == QRectF(0.0, 0.0, 1.0, 1.0),
                     qPrintable(QStringLiteral("%1: single zone should be fullscreen").arg(id)));

            // Standard cases
            for (int n : {2, 3, 4, 5, 8}) {
                const QString ctx = QStringLiteral("%1(%2)").arg(id).arg(n);
                auto zones = algo->generateZones(n, params);
                if (id != QStringLiteral("monocle")) {
                    QVERIFY2(zones.size() == n,
                             qPrintable(QStringLiteral("%1: expected %2 zones, got %3")
                                            .arg(ctx).arg(n).arg(zones.size())));
                } else {
                    QVERIFY2(zones.size() == 1,
                             qPrintable(QStringLiteral("%1: monocle should return 1 zone").arg(ctx)));
                }
                verifyAll(zones, ctx);
            }
        }
    }

    void testAllAlgorithmsMasterIndex()
    {
        std::vector<std::unique_ptr<TilingAlgorithm>> algos;
        algos.push_back(std::make_unique<BSPTilingAlgorithm>());
        algos.push_back(std::make_unique<FibonacciTilingAlgorithm>());
        algos.push_back(std::make_unique<MasterStackTilingAlgorithm>());
        algos.push_back(std::make_unique<MonocleTilingAlgorithm>());
        algos.push_back(std::make_unique<ThreeColumnTilingAlgorithm>());

        for (const auto& algo : algos) {
            QCOMPARE(algo->masterIndex(), 0);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Comparative: BSP (balanced) vs Fibonacci (greedy)
    // ═══════════════════════════════════════════════════════════════════════════

    void testBspVsFibonacciDistinction()
    {
        BSPTilingAlgorithm bsp;
        FibonacciTilingAlgorithm fib;
        TilingParams params{0.5, 1};

        auto bspZones = bsp.generateZones(5, params);
        auto fibZones = fib.generateZones(5, params);

        QCOMPARE(bspZones.size(), 5);
        QCOMPARE(fibZones.size(), 5);

        // BSP (balanced) should produce more uniform zone areas than Fibonacci (greedy)
        auto areaRatio = [](const QVector<QRectF>& zones) {
            qreal minA = 1.0, maxA = 0.0;
            for (const auto& z : zones) {
                qreal a = z.width() * z.height();
                minA = std::min(minA, a);
                maxA = std::max(maxA, a);
            }
            return maxA / minA;
        };

        qreal bspRatio = areaRatio(bspZones);   // ~2.0
        qreal fibRatio = areaRatio(fibZones);    // ~8.0

        QVERIFY2(bspRatio < fibRatio,
                 qPrintable(QStringLiteral("BSP area ratio (%1) should be less than Fibonacci (%2)")
                                .arg(bspRatio).arg(fibRatio)));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Large window counts
    // ═══════════════════════════════════════════════════════════════════════════

    void testLargeWindowCount()
    {
        TilingParams params{0.5, 1};
        BSPTilingAlgorithm bsp;
        FibonacciTilingAlgorithm fib;
        MasterStackTilingAlgorithm ms;
        ThreeColumnTilingAlgorithm tc;
        ColumnsTilingAlgorithm col;

        for (int n : {20, 50}) {
            verifyAll(bsp.generateZones(n, params), QStringLiteral("BSP(%1)").arg(n));
            verifyAll(fib.generateZones(n, params), QStringLiteral("Fibonacci(%1)").arg(n));
            verifyAll(ms.generateZones(n, params), QStringLiteral("MasterStack(%1)").arg(n));
            verifyAll(tc.generateZones(n, params), QStringLiteral("ThreeColumn(%1)").arg(n));
            verifyAll(col.generateZones(n, params), QStringLiteral("Columns(%1)").arg(n));
        }
    }
};

QTEST_MAIN(TestTilingAlgorithms)
#include "test_tiling_algorithms.moc"
