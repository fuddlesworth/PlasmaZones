// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/MasterStackAlgorithm.h"
#include "autotile/algorithms/ColumnsAlgorithm.h"
#include "autotile/algorithms/BSPAlgorithm.h"
#include "autotile/algorithms/FibonacciAlgorithm.h"
#include "autotile/algorithms/MonocleAlgorithm.h"
#include "autotile/algorithms/RowsAlgorithm.h"
#include "autotile/algorithms/ThreeColumnAlgorithm.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for tiling algorithms
 *
 * Tests cover:
 * - Basic zone calculation for various window counts
 * - Edge cases (0 windows, 1 window, many windows)
 * - Pixel-perfect geometry (no gaps between zones, zones fill screen)
 * - Algorithm-specific features (master count, split ratio)
 * - Gap application
 * - Helper function (distributeEvenly)
 */
class TestTilingAlgorithms : public QObject
{
    Q_OBJECT

private:
    // Standard test screen geometry
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};

    // Helper to verify zones fill screen exactly
    bool zonesFillScreen(const QVector<QRect>& zones, const QRect& screen) const
    {
        // Simple check: total area should equal screen area
        // Note: This doesn't catch overlaps, just ensures coverage
        int totalArea = 0;
        for (const QRect& zone : zones) {
            totalArea += zone.width() * zone.height();
        }
        return totalArea == screen.width() * screen.height();
    }

    // Helper to verify no zone overlaps
    bool noOverlaps(const QVector<QRect>& zones) const
    {
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                if (zones[i].intersects(zones[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    // Helper to verify all zones are within screen bounds
    bool allWithinBounds(const QVector<QRect>& zones, const QRect& screen) const
    {
        for (const QRect& zone : zones) {
            if (!screen.contains(zone)) {
                return false;
            }
        }
        return true;
    }

private Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Pixel-perfect distribution tests (via algorithm behavior)
    // The distributeEvenly() helper is protected, so we test it indirectly
    // ═══════════════════════════════════════════════════════════════════════════

    void testPixelPerfect_columnsRemainderDistribution()
    {
        // Test that 1920px / 7 columns distributes remainder correctly
        // 1920 / 7 = 274 with remainder 2
        // First 2 columns should be 275px, rest 274px
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({7, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 7);

        // Verify sum equals screen width exactly
        int totalWidth = 0;
        for (const QRect& zone : zones) {
            totalWidth += zone.width();
            // Each zone should be either 274 or 275
            QVERIFY(zone.width() == 274 || zone.width() == 275);
        }
        QCOMPARE(totalWidth, ScreenWidth);

        // First zones get extra pixels
        QCOMPARE(zones[0].width(), 275);
        QCOMPARE(zones[1].width(), 275);
        QCOMPARE(zones[2].width(), 274);
    }

    void testPixelPerfect_masterStackHeightDistribution()
    {
        // Test that 1080px / 7 stack windows distributes remainder correctly
        // 1080 / 7 = 154 with remainder 2
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(1);

        auto zones = algo.calculateZones({8, m_screenGeometry, &state}); // 1 master + 7 stack
        QCOMPARE(zones.size(), 8);

        // Stack zones (indices 1-7) should have pixel-perfect distribution
        int totalStackHeight = 0;
        for (int i = 1; i < 8; ++i) {
            totalStackHeight += zones[i].height();
            QVERIFY(zones[i].height() == 154 || zones[i].height() == 155);
        }
        QCOMPARE(totalStackHeight, ScreenHeight);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // MasterStackAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testMasterStack_metadata()
    {
        MasterStackAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Master + Stack"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), 0);
        QCOMPARE(algo.defaultSplitRatio(), AutotileDefaults::DefaultSplitRatio);
    }

    void testMasterStack_zeroWindows()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testMasterStack_oneWindow()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMasterStack_twoWindows_defaultRatio()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        // Master should be 60% width
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), static_cast<int>(ScreenWidth * 0.6));
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Stack should fill remainder
        QCOMPARE(zones[1].x(), zones[0].width());
        QCOMPARE(zones[1].width(), ScreenWidth - zones[0].width());
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testMasterStack_multipleStack()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({4, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 4);

        // Master takes left half
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Stack has 3 windows, should divide height evenly
        for (int i = 1; i < 4; ++i) {
            QCOMPARE(zones[i].x(), ScreenWidth / 2);
            QCOMPARE(zones[i].width(), ScreenWidth / 2);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testMasterStack_multipleMasters()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        // Add windows to state so masterCount isn't over-clamped
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(2);
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 5);

        // First 2 zones are masters (stacked vertically on left)
        int masterWidth = static_cast<int>(ScreenWidth * 0.6);
        QCOMPARE(zones[0].width(), masterWidth);
        QCOMPARE(zones[1].width(), masterWidth);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[1].x(), 0);

        // Stack has 3 windows on right
        for (int i = 2; i < 5; ++i) {
            QCOMPARE(zones[i].x(), masterWidth);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testMasterStack_allMasters()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        // Add windows to state so masterCount can be set high enough
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(5); // More than windows we'll tile

        auto zones = algo.calculateZones({3, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 3);

        // All should be full width (no stack since masterCount >= windowCount)
        for (const QRect& zone : zones) {
            QCOMPARE(zone.width(), ScreenWidth);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testMasterStack_invalidGeometry()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state});
        QVERIFY(zones.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // ColumnsAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testColumns_metadata()
    {
        ColumnsAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Columns"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1); // No master concept
    }

    void testColumns_zeroWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testColumns_oneWindow()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testColumns_twoWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);

        QCOMPARE(zones[1].x(), ScreenWidth / 2);
        QCOMPARE(zones[1].width(), ScreenWidth / 2);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testColumns_threeWindows_remainder()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 3);

        // 1920 / 3 = 640, remainder 0 - actually divides evenly
        // All columns should be 640
        int currentX = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.x(), currentX);
            QCOMPARE(zone.height(), ScreenHeight);
            currentX += zone.width();
        }

        // Should fill exactly
        QCOMPARE(currentX, ScreenWidth);
        QVERIFY(noOverlaps(zones));
    }

    void testColumns_manyWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 10);

        // Verify contiguous and fill screen
        int currentX = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.x(), currentX);
            QCOMPARE(zone.y(), 0);
            QCOMPARE(zone.height(), ScreenHeight);
            currentX += zone.width();
        }
        QCOMPARE(currentX, ScreenWidth);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BSPAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testBSP_metadata()
    {
        BSPAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("BSP"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1); // No master concept
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testBSP_zeroWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testBSP_oneWindow()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testBSP_twoWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        // Screen is wider than tall, so should split left/right
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[1].y(), 0);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_fourWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({4, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testBSP_oddWindowCount()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 5);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_manyWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({16, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 16);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));

        // All zones should have reasonable minimum size
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testBSP_squareScreen()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect squareScreen(0, 0, 1000, 1000);
        auto zones = algo.calculateZones({4, squareScreen, &state});
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, squareScreen));
    }

    void testBSP_persistentTreeStability()
    {
        auto* algo = AlgorithmRegistry::instance()->algorithm(QStringLiteral("bsp"));
        QVERIFY(algo);
        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        // Calculate zones for 4 windows
        QVector<QRect> zones4 = algo->calculateZones({4, screen, &state});
        QCOMPARE(zones4.size(), 4);

        // Calculate zones for 5 windows (incremental grow)
        QVector<QRect> zones5 = algo->calculateZones({5, screen, &state});
        QCOMPARE(zones5.size(), 5);

        // BSP grows by splitting the largest leaf into two children. The
        // unsplit leaves retain their geometry, but their DFS index may shift.
        // Check that most 4-window geometries appear somewhere in the 5-window set.
        int preservedCount = 0;
        for (const QRect& z4 : zones4) {
            if (zones5.contains(z4)) {
                preservedCount++;
            }
        }
        QVERIFY2(preservedCount >= 3,
                 qPrintable(QStringLiteral("Only %1/4 zone geometries preserved after grow").arg(preservedCount)));

        // Shrink back to 4
        QVector<QRect> zones4again = algo->calculateZones({4, screen, &state});
        QCOMPARE(zones4again.size(), 4);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // FibonacciAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testFibonacci_metadata()
    {
        FibonacciAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Fibonacci"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1); // No master concept
        QCOMPARE(algo.defaultSplitRatio(), 0.5); // Dwindle default
    }

    void testFibonacci_zeroWindows()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testFibonacci_oneWindow()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testFibonacci_twoWindows_spiralSplit()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        // Dwindle: first split is vertical — window 1 on left
        int expectedWidth = static_cast<int>(ScreenWidth * 0.618);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), expectedWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Window 2 gets the remaining right portion
        QCOMPARE(zones[1].x(), expectedWidth);
        QCOMPARE(zones[1].width(), ScreenWidth - expectedWidth);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_threeWindows_spiralPattern()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({3, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 3);

        // With 0.5 ratio: first split vertical (left half), second split horizontal (top of right)
        // Zone 0: left half of screen
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Zone 1: top-right quarter (horizontal split on remaining right half)
        QCOMPARE(zones[1].x(), ScreenWidth / 2);
        QCOMPARE(zones[1].height(), ScreenHeight / 2);

        // Zone 2: bottom-right quarter (remaining area)
        QCOMPARE(zones[2].x(), ScreenWidth / 2);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_goldenRatio_firstWindowLargest()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 5);

        // First window should have the largest area
        int firstArea = zones[0].width() * zones[0].height();
        for (int i = 1; i < zones.size(); ++i) {
            int area = zones[i].width() * zones[i].height();
            QVERIFY2(
                firstArea >= area,
                qPrintable(
                    QStringLiteral("Zone 0 area (%1) should be >= zone %2 area (%3)").arg(firstArea).arg(i).arg(area)));
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_manyWindows()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones({12, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 12);

        // All zones should have positive dimensions
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        // NOTE: noOverlaps is intentionally NOT checked here. Fibonacci produces overlapping
        // zones when the remaining area becomes too small to split, duplicating the last zone
        // for surplus windows (similar to Monocle stacking). This is expected behavior.

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_minimumSizeEnforcement()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        // Very small screen with many windows: should degrade gracefully
        QRect tinyScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones({20, tinyScreen, &state});
        QCOMPARE(zones.size(), 20);

        // When the remaining area is too small to split (< MinZoneSizePx),
        // remaining windows get the same zone (graceful degradation)
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        // NOTE: noOverlaps is intentionally NOT checked here. Fibonacci produces overlapping
        // zones when the remaining area becomes too small to split, duplicating the last zone
        // for surplus windows (similar to Monocle stacking). This is expected behavior.

        QVERIFY(allWithinBounds(zones, tinyScreen));
    }

    void testFibonacci_invalidGeometry()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state});
        QVERIFY(zones.isEmpty());
    }

    void testFibonacci_offsetScreen()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state});
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // MonocleAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testMonocle_metadata()
    {
        MonocleAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Monocle"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1); // No master concept
    }

    void testMonocle_zeroWindows()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_oneWindow()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMonocle_twoWindows_allFullScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        // Both zones should be the full screen (all windows overlap)
        QCOMPARE(zones[0], m_screenGeometry);
        QCOMPARE(zones[1], m_screenGeometry);
    }

    void testMonocle_manyWindows_allIdentical()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 10);

        // Every zone must equal the full screen geometry
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], m_screenGeometry);
        }
    }

    void testMonocle_fiftyWindows()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 50);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, m_screenGeometry);
        }
    }

    void testMonocle_invalidGeometry()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state});
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_offsetScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(200, 100, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state});
        QCOMPARE(zones.size(), 5);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, offsetScreen);
        }
    }

    void testMonocle_smallScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect smallScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones({8, smallScreen, &state});
        QCOMPARE(zones.size(), 8);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, smallScreen);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // RowsAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testRows_metadata()
    {
        RowsAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Rows"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1); // No master concept
    }

    void testRows_zeroWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testRows_oneWindow()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testRows_twoWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth);
        QCOMPARE(zones[0].height(), ScreenHeight / 2);

        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[1].y(), ScreenHeight / 2);
        QCOMPARE(zones[1].width(), ScreenWidth);
        QCOMPARE(zones[1].height(), ScreenHeight / 2);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testRows_threeWindows_heightDistribution()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 3);

        // 1080 / 3 = 360, remainder 0 -- divides evenly
        int currentY = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.x(), 0);
            QCOMPARE(zone.y(), currentY);
            QCOMPARE(zone.width(), ScreenWidth);
            QCOMPARE(zone.height(), 360);
            currentY += zone.height();
        }
        QCOMPARE(currentY, ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testRows_remainderHandling()
    {
        // Test that 1080px / 7 rows distributes remainder correctly
        // 1080 / 7 = 154 with remainder 2
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({7, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 7);

        int totalHeight = 0;
        for (const QRect& zone : zones) {
            totalHeight += zone.height();
            QCOMPARE(zone.width(), ScreenWidth);
            // Each row should be either 154 or 155
            QVERIFY(zone.height() == 154 || zone.height() == 155);
        }
        QCOMPARE(totalHeight, ScreenHeight);

        // First rows get extra pixels
        QCOMPARE(zones[0].height(), 155);
        QCOMPARE(zones[1].height(), 155);
        QCOMPARE(zones[2].height(), 154);
    }

    void testRows_contiguousRows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 5);

        // Verify each row starts exactly where the previous one ends
        int currentY = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.y(), currentY);
            QCOMPARE(zone.x(), 0);
            QCOMPARE(zone.width(), ScreenWidth);
            currentY += zone.height();
        }
        QCOMPARE(currentY, ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testRows_manyWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 10);

        int currentY = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.y(), currentY);
            QCOMPARE(zone.width(), ScreenWidth);
            currentY += zone.height();
        }
        QCOMPARE(currentY, ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testRows_invalidGeometry()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state});
        QVERIFY(zones.isEmpty());
    }

    void testRows_offsetScreen()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({4, offsetScreen, &state});
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));

        // First row starts at the offset Y
        QCOMPARE(zones[0].x(), 100);
        QCOMPARE(zones[0].y(), 50);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // ThreeColumnAlgorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testThreeColumn_metadata()
    {
        ThreeColumnAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Three Column"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), 0); // Center master
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testThreeColumn_zeroWindows()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state});
        QVERIFY(zones.isEmpty());
    }

    void testThreeColumn_oneWindow()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testThreeColumn_twoWindows_usesSplitRatio()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 2);

        // Two-window case: master gets splitRatio portion, second gets remainder
        int masterWidth = static_cast<int>(ScreenWidth * 0.6);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), masterWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);

        QCOMPARE(zones[1].x(), masterWidth);
        QCOMPARE(zones[1].width(), ScreenWidth - masterWidth);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_threeWindows_centerMaster()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({3, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 3);

        // Center column (master) gets 50% width
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);
        int rightWidth = ScreenWidth - sideWidth - centerWidth;

        // Zone 0 is center/master
        QCOMPARE(zones[0].x(), sideWidth);
        QCOMPARE(zones[0].width(), centerWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Zone 1 is left column (first stack window goes left)
        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[1].width(), sideWidth);
        QCOMPARE(zones[1].height(), ScreenHeight);

        // Zone 2 is right column
        QCOMPARE(zones[2].x(), sideWidth + centerWidth);
        QCOMPARE(zones[2].width(), rightWidth);
        QCOMPARE(zones[2].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_fourWindows_interleavedFilling()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({4, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 4);

        // 4 windows: master (center) + 3 stack
        // stackCount = 3, leftCount = (3+1)/2 = 2, rightCount = 1
        // Interleaved order: left1, right1, left2
        // Zone 0: center/master
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);

        QCOMPARE(zones[0].x(), sideWidth); // Center master
        QCOMPARE(zones[0].width(), centerWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);

        // Zone 1: left column, first entry (left gets 2 windows, stacked vertically)
        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[1].width(), sideWidth);

        // Zone 2: right column, first entry (right gets 1 window, full height)
        QCOMPARE(zones[2].x(), sideWidth + centerWidth);
        QCOMPARE(zones[2].height(), ScreenHeight);

        // Zone 3: left column, second entry
        QCOMPARE(zones[3].x(), 0);
        QCOMPARE(zones[3].width(), sideWidth);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_fiveWindows_distribution()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 5);

        // 5 windows: master + 4 stack
        // leftCount = (4+1)/2 = 2 (extra goes to left), rightCount = 2
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);

        // Master is center
        QCOMPARE(zones[0].x(), sideWidth);
        QCOMPARE(zones[0].width(), centerWidth);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_manyWindows()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({11, m_screenGeometry, &state});
        QCOMPARE(zones.size(), 11);

        // All zones should have positive dimensions
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testThreeColumn_invalidGeometry()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state});
        QVERIFY(zones.isEmpty());
    }

    void testThreeColumn_offsetScreen()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state});
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Gap-aware calculateZones() tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testGapAware_singleZoneOuterGap()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 10, 20});

        QCOMPARE(zones.size(), 1);
        // Should have outer gap on all sides
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[0].width(), ScreenWidth - 40);
        QCOMPARE(zones[0].height(), ScreenHeight - 40);
    }

    void testGapAware_twoColumnsWithGaps()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 10, 20});

        QCOMPARE(zones.size(), 2);
        // Left zone starts at outer gap
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        // Right zone ends at screen right minus outer gap
        QCOMPARE(zones[1].right(), ScreenWidth - 20 - 1);
        // Gap between zones should be innerGap
        const int gap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap, 10);
        // Zones shouldn't overlap
        QVERIFY(!zones[0].intersects(zones[1]));
    }

    void testGapAware_zeroGapsUnchanged()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zonesNoGap = algo.calculateZones({3, m_screenGeometry, &state, 0, 0});
        auto zonesDefault = algo.calculateZones({3, m_screenGeometry, &state});

        // With zero gaps, should match the default (no-gap) calculation
        QCOMPARE(zonesNoGap, zonesDefault);
    }

    void testGapAware_innerGapBetweenColumns()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 9, 0});

        QCOMPARE(zones.size(), 3);
        // Gap between zone 0 and zone 1
        const int gap01 = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap01, 9);
        // Gap between zone 1 and zone 2
        const int gap12 = zones[2].left() - zones[1].right() - 1;
        QCOMPARE(gap12, 9);
    }

    void testGapAware_zonesWithinInsetBounds()
    {
        const int outerGap = 20;
        const QRect insetScreen(m_screenGeometry.x() + outerGap, m_screenGeometry.y() + outerGap,
                                m_screenGeometry.width() - 2 * outerGap, m_screenGeometry.height() - 2 * outerGap);

        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 8, outerGap});

        for (const QRect& zone : zones) {
            QVERIFY2(insetScreen.contains(zone),
                     qPrintable(QStringLiteral("Zone %1,%2 %3x%4 outside inset bounds %5,%6 %7x%8")
                                    .arg(zone.x())
                                    .arg(zone.y())
                                    .arg(zone.width())
                                    .arg(zone.height())
                                    .arg(insetScreen.x())
                                    .arg(insetScreen.y())
                                    .arg(insetScreen.width())
                                    .arg(insetScreen.height())));
        }
    }

    void testGapAware_noOverlaps()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 8, 8});

        QVERIFY2(noOverlaps(zones), "Gap-aware zones must not overlap");
        const QRect inner(8, 8, ScreenWidth - 16, ScreenHeight - 16);
        for (const QRect& zone : zones) {
            QVERIFY2(inner.contains(zone), qPrintable(QStringLiteral("Zone extends outside gap-inset area")));
        }
    }

    void testGapAware_masterStackWithGaps()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("w1"));
        state.addWindow(QStringLiteral("w2"));
        state.addWindow(QStringLiteral("w3"));
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 8, 8});

        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        // Master zone starts after outer gap
        QCOMPARE(zones[0].left(), 8);
        QCOMPARE(zones[0].top(), 8);
        // Gap between master and first stack zone
        const int hGap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(hGap, 8);
        // Gap between stacked zones
        const int vGap = zones[2].top() - zones[1].bottom() - 1;
        QCOMPARE(vGap, 8);
    }

    void testGapAware_rowsWithGaps()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 10, 15});

        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        // All rows should start at left outer gap
        for (const QRect& zone : zones) {
            QCOMPARE(zone.left(), 15);
            QCOMPARE(zone.width(), ScreenWidth - 30);
        }
        // Gap between rows
        const int gap01 = zones[1].top() - zones[0].bottom() - 1;
        QCOMPARE(gap01, 10);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Edge case tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testAllAlgorithms_negativeWindowCount()
    {
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        QVERIFY(masterStack.calculateZones({-1, m_screenGeometry, &state}).isEmpty());

        ColumnsAlgorithm columns;
        QVERIFY(columns.calculateZones({-5, m_screenGeometry, &state}).isEmpty());

        BSPAlgorithm bsp;
        QVERIFY(bsp.calculateZones({-10, m_screenGeometry, &state}).isEmpty());

        FibonacciAlgorithm fibonacci;
        QVERIFY(fibonacci.calculateZones({-3, m_screenGeometry, &state}).isEmpty());

        MonocleAlgorithm monocle;
        QVERIFY(monocle.calculateZones({-1, m_screenGeometry, &state}).isEmpty());

        RowsAlgorithm rows;
        QVERIFY(rows.calculateZones({-7, m_screenGeometry, &state}).isEmpty());

        ThreeColumnAlgorithm threeCol;
        QVERIFY(threeCol.calculateZones({-2, m_screenGeometry, &state}).isEmpty());
    }

    void testAllAlgorithms_largeWindowCount()
    {
        TilingState state(QStringLiteral("test"));

        // Test with 50 windows - should still work without crashes
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(msZones.size(), 50);
        QVERIFY(noOverlaps(msZones));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(colZones.size(), 50);
        QVERIFY(noOverlaps(colZones));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(bspZones.size(), 50);
        QVERIFY(noOverlaps(bspZones));

        FibonacciAlgorithm fibonacci;
        auto fibZones = fibonacci.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(fibZones.size(), 50);

        MonocleAlgorithm monocle;
        auto monZones = monocle.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(monZones.size(), 50);

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(rowZones.size(), 50);
        QVERIFY(noOverlaps(rowZones));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({50, m_screenGeometry, &state});
        QCOMPARE(tcZones.size(), 50);
        QVERIFY(noOverlaps(tcZones));
    }

    void testAllAlgorithms_offsetScreen()
    {
        // Test with screen that doesn't start at (0,0)
        QRect offsetScreen(100, 50, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({3, offsetScreen, &state});
        QCOMPARE(msZones.size(), 3);
        QVERIFY(allWithinBounds(msZones, offsetScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({3, offsetScreen, &state});
        QCOMPARE(colZones.size(), 3);
        QVERIFY(allWithinBounds(colZones, offsetScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({3, offsetScreen, &state});
        QCOMPARE(bspZones.size(), 3);
        QVERIFY(allWithinBounds(bspZones, offsetScreen));

        FibonacciAlgorithm fibonacci;
        auto fibZones = fibonacci.calculateZones({3, offsetScreen, &state});
        QCOMPARE(fibZones.size(), 3);
        QVERIFY(allWithinBounds(fibZones, offsetScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({3, offsetScreen, &state});
        QCOMPARE(rowZones.size(), 3);
        QVERIFY(allWithinBounds(rowZones, offsetScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({3, offsetScreen, &state});
        QCOMPARE(tcZones.size(), 3);
        QVERIFY(allWithinBounds(tcZones, offsetScreen));
    }

    void testAllAlgorithms_smallScreen()
    {
        // Very small screen (200x150)
        QRect smallScreen(0, 0, 200, 150);
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({4, smallScreen, &state});
        QCOMPARE(msZones.size(), 4);
        QVERIFY(zonesFillScreen(msZones, smallScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({4, smallScreen, &state});
        QCOMPARE(colZones.size(), 4);
        QVERIFY(zonesFillScreen(colZones, smallScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({4, smallScreen, &state});
        QCOMPARE(bspZones.size(), 4);
        QVERIFY(zonesFillScreen(bspZones, smallScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({4, smallScreen, &state});
        QCOMPARE(rowZones.size(), 4);
        QVERIFY(zonesFillScreen(rowZones, smallScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({4, smallScreen, &state});
        QCOMPARE(tcZones.size(), 4);
        QVERIFY(zonesFillScreen(tcZones, smallScreen));
    }

    void testThreeColumn_withGaps()
    {
        // 3 windows, Center Master.
        // Inner Gap 10, Outer Gap 20.
        // Screen 1920x1080.
        // Available area for content:
        // Width = 1920 - 2*20 = 1880.
        // Height = 1080 - 2*20 = 1040.
        // 3 columns. 2 gaps between columns (Gap | Gap).
        // contentWidth = 1880 - 2*10 = 1860.

        // Ratios: DefaultSplitRatio is 0.6 (from constants.h).
        // Center Ratio = 0.6.
        // Side Ratio = (1.0 - 0.6) / 2 = 0.2.

        // Center width = 1860 * 0.6 = 1116.
        // Left width = 1860 * 0.2 = 372.
        // Right width = 1860 * 0.2 = 372.

        // Positions (Outer Gap 20 + offset):
        // Left X = 20. Width 372. Right 392.
        // Gap 10.
        // Center X = 392 + 10 = 402. Width 1116. Right 1518.
        // Gap 10.
        // Right X = 1518 + 10 = 1528. Width 372. Right 1900.
        // 1900 + 20 (Outer) = 1920. Correct.

        // Y = 20. Height 1040.

        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));
        // Default split ratio is 0.6 used by ThreeColumnAlgorithm logic.

        ThreeColumnAlgorithm algo;
        // calculateZones(count, screen, state, innerGap, outerGap)
        auto zones = algo.calculateZones({3, screen, &state, 10, 20});

        QCOMPARE(zones.size(), 3);

        // Verify Zone 0 (Center)
        QCOMPARE(zones[0].x(), 402);
        QCOMPARE(zones[0].width(), 1116);
        QCOMPARE(zones[0].y(), 20);
        QCOMPARE(zones[0].height(), 1040);

        // Verify Zone 1 (Left)
        QCOMPARE(zones[1].x(), 20);
        QCOMPARE(zones[1].width(), 372);

        // Verify Zone 2 (Right)
        QCOMPARE(zones[2].x(), 1528);
        QCOMPARE(zones[2].width(), 372);
    }

    // =============================================================================
    // Edge case: BSP with gap larger than available space
    // =============================================================================
    void test_bspNegativeContentWidth()
    {
        BSPAlgorithm algo;
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));

        // innerGap=200 exceeds screen width after outerGap — should not crash
        auto zones = algo.calculateZones({3, screen, &state, 200, 10});
        QCOMPARE(zones.size(), 3);
        for (const auto &z : zones) {
            QVERIFY2(z.width() > 0 && z.height() > 0,
                      qPrintable(QStringLiteral("Zone %1x%2 has non-positive dimension")
                                 .arg(z.width()).arg(z.height())));
        }
    }

    // =============================================================================
    // Edge case: Fibonacci with gap exceeding remaining area
    // =============================================================================
    void test_fibonacciGapExceedsRemaining()
    {
        FibonacciAlgorithm algo;
        QRect screen(0, 0, 200, 200);
        TilingState state(QStringLiteral("test"));

        // Large innerGap relative to screen — should degrade gracefully
        auto zones = algo.calculateZones({5, screen, &state, 80, 10});
        QCOMPARE(zones.size(), 5);
        for (const auto &z : zones) {
            QVERIFY2(z.width() > 0 && z.height() > 0,
                      qPrintable(QStringLiteral("Zone %1x%2 has non-positive dimension")
                                 .arg(z.width()).arg(z.height())));
        }
    }

    // =============================================================================
    // Edge case: MasterStack with unsatisfiable min widths
    // =============================================================================
    void test_masterStackUnsatisfiableMinWidths()
    {
        MasterStackAlgorithm algo;
        QRect screen(0, 0, 400, 400);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        // Both columns need 300px each in 400px screen (impossible)
        QVector<QSize> minSizes = {QSize(300, 0), QSize(300, 0)};
        auto zones = algo.calculateZones({2, screen, &state, 10, 0, minSizes});
        QCOMPARE(zones.size(), 2);

        // Both should get roughly proportional allocation (no negative widths)
        QVERIFY2(zones[0].width() > 0, "Master width must be positive");
        QVERIFY2(zones[1].width() > 0, "Stack width must be positive");
        QCOMPARE(zones[0].width() + 10 + zones[1].width(), 400);
    }

    // =============================================================================
    // Edge case: ThreeColumn with unsatisfiable min widths
    // =============================================================================
    void test_threeColumnUnsatisfiableMinWidths()
    {
        ThreeColumnAlgorithm algo;
        QRect screen(0, 0, 300, 300);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        // All three columns want 200px each in 300px screen (impossible)
        QVector<QSize> minSizes = {QSize(200, 0), QSize(200, 0), QSize(200, 0)};
        auto zones = algo.calculateZones({3, screen, &state, 10, 0, minSizes});
        QCOMPARE(zones.size(), 3);

        // All zones should have positive widths (proportional allocation)
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(zones[i].width() > 0,
                      qPrintable(QStringLiteral("Zone %1 width must be positive, got %2")
                                 .arg(i).arg(zones[i].width())));
        }
    }

    // =============================================================================
    // Edge case: negative screen coordinates (multi-monitor left-of-primary)
    // =============================================================================
    void test_negativeScreenCoordinates()
    {
        // Second monitor to the left of primary: x starts negative
        QRect screen(-1920, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        // All algorithms should handle negative coordinates correctly
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({3, screen, &state});
        QCOMPARE(msZones.size(), 3);
        for (int i = 0; i < msZones.size(); ++i) {
            QVERIFY2(msZones[i].left() >= screen.left(),
                      qPrintable(QStringLiteral("MasterStack zone %1 left %2 < screen left %3")
                          .arg(i).arg(msZones[i].left()).arg(screen.left())));
            QVERIFY2(msZones[i].right() <= screen.right(),
                      qPrintable(QStringLiteral("MasterStack zone %1 extends past screen right")
                          .arg(i)));
            QVERIFY2(msZones[i].width() > 0 && msZones[i].height() > 0,
                      qPrintable(QStringLiteral("MasterStack zone %1 has non-positive dimensions")
                          .arg(i)));
        }

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({4, screen, &state});
        QCOMPARE(bspZones.size(), 4);
        for (int i = 0; i < bspZones.size(); ++i) {
            QVERIFY2(bspZones[i].left() >= screen.left(),
                      qPrintable(QStringLiteral("BSP zone %1 left %2 < screen left %3")
                          .arg(i).arg(bspZones[i].left()).arg(screen.left())));
        }

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({5, screen, &state});
        QCOMPARE(tcZones.size(), 5);
        for (int i = 0; i < tcZones.size(); ++i) {
            QVERIFY2(tcZones[i].left() >= screen.left(),
                      qPrintable(QStringLiteral("ThreeColumn zone %1 left %2 < screen left %3")
                          .arg(i).arg(tcZones[i].left()).arg(screen.left())));
        }

        // Monitor above primary (negative Y)
        QRect topScreen(0, -1080, 1920, 1080);
        ColumnsAlgorithm cols;
        auto colZones = cols.calculateZones({3, topScreen, &state});
        QCOMPARE(colZones.size(), 3);
        for (int i = 0; i < colZones.size(); ++i) {
            QVERIFY2(colZones[i].top() >= topScreen.top(),
                      qPrintable(QStringLiteral("Columns zone %1 top %2 < screen top %3")
                          .arg(i).arg(colZones[i].top()).arg(topScreen.top())));
        }
    }

    // =============================================================================
    // Edge case: splitRatio boundary values (near 0 and near 1)
    // =============================================================================
    void test_splitRatioBoundaryValues()
    {
        QRect screen(0, 0, 1920, 1080);

        // Very small split ratio (0.1) — stack gets most space
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);

            MasterStackAlgorithm algo;
            auto zones = algo.calculateZones({3, screen, &state});
            QCOMPARE(zones.size(), 3);
            // Master should be narrow but still positive
            QVERIFY2(zones[0].width() > 0,
                      qPrintable(QStringLiteral("Master width %1 should be > 0 with ratio 0.1")
                          .arg(zones[0].width())));
            // All zones should have positive dimensions
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                          qPrintable(QStringLiteral("Zone %1 has non-positive dimension at ratio 0.1")
                              .arg(i)));
            }
        }

        // Very large split ratio (0.9) — master dominates
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);

            MasterStackAlgorithm algo;
            auto zones = algo.calculateZones({3, screen, &state});
            QCOMPARE(zones.size(), 3);
            // Stack windows should still be visible
            for (int i = 1; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                          qPrintable(QStringLiteral("Stack zone %1 has non-positive dimension at ratio 0.9")
                              .arg(i)));
            }
        }

        // Fibonacci with extreme ratios
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);

            FibonacciAlgorithm algo;
            auto zones = algo.calculateZones({4, screen, &state});
            QCOMPARE(zones.size(), 4);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                          qPrintable(QStringLiteral("Fibonacci zone %1 non-positive at ratio 0.1")
                              .arg(i)));
            }
        }

        // ThreeColumn with extreme split
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);

            ThreeColumnAlgorithm algo;
            auto zones = algo.calculateZones({4, screen, &state});
            QCOMPARE(zones.size(), 4);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                          qPrintable(QStringLiteral("ThreeColumn zone %1 non-positive at ratio 0.9")
                              .arg(i)));
            }
        }
    }

    // =============================================================================
    // Edge case: null state pointer — should return empty, not crash
    // =============================================================================
    void test_nullStatePointer()
    {
        QRect screen(0, 0, 1920, 1080);

        // All algorithms that dereference state must handle nullptr gracefully
        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({3, screen, nullptr});
        QCOMPARE(bspZones.size(), 0);

        FibonacciAlgorithm fib;
        auto fibZones = fib.calculateZones({3, screen, nullptr});
        QCOMPARE(fibZones.size(), 0);

        MasterStackAlgorithm ms;
        auto msZones = ms.calculateZones({3, screen, nullptr});
        QCOMPARE(msZones.size(), 0);

        ThreeColumnAlgorithm tc;
        auto tcZones = tc.calculateZones({3, screen, nullptr});
        QCOMPARE(tcZones.size(), 0);

        // Columns, Rows, Monocle don't dereference state but should still work
        ColumnsAlgorithm cols;
        auto colZones = cols.calculateZones({3, screen, nullptr});
        QCOMPARE(colZones.size(), 3);

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({3, screen, nullptr});
        QCOMPARE(rowZones.size(), 3);

        MonocleAlgorithm mono;
        auto monoZones = mono.calculateZones({3, screen, nullptr});
        QCOMPARE(monoZones.size(), 3);
    }

    // =============================================================================
    // Edge case: innerRect with huge outerGap
    // =============================================================================
    void test_innerRectHugeOuterGap()
    {
        ColumnsAlgorithm algo;
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));

        // outerGap=500 far exceeds screen — should produce valid centered zone
        auto zones = algo.calculateZones({1, screen, &state, 0, 500});
        QCOMPARE(zones.size(), 1);
        QVERIFY2(zones[0].width() >= 1, "Zone width must be at least 1");
        QVERIFY2(zones[0].height() >= 1, "Zone height must be at least 1");
        // Result should be within the screen bounds
        QVERIFY2(zones[0].left() >= screen.left(), "Zone must not extend left of screen");
        QVERIFY2(zones[0].top() >= screen.top(), "Zone must not extend above screen");
        QVERIFY2(zones[0].right() <= screen.right(), "Zone must not extend right of screen");
        QVERIFY2(zones[0].bottom() <= screen.bottom(), "Zone must not extend below screen");
    }
};

QTEST_MAIN(TestTilingAlgorithms)
#include "test_tiling_algorithms.moc"