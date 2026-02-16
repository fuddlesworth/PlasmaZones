// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

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
    bool zonesFillScreen(const QVector<QRect> &zones, const QRect &screen) const
    {
        // Simple check: total area should equal screen area
        // Note: This doesn't catch overlaps, just ensures coverage
        int totalArea = 0;
        for (const QRect &zone : zones) {
            totalArea += zone.width() * zone.height();
        }
        return totalArea == screen.width() * screen.height();
    }

    // Helper to verify no zone overlaps
    bool noOverlaps(const QVector<QRect> &zones) const
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
    bool allWithinBounds(const QVector<QRect> &zones, const QRect &screen) const
    {
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(7, m_screenGeometry, state);
        QCOMPARE(zones.size(), 7);

        // Verify sum equals screen width exactly
        int totalWidth = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(8, m_screenGeometry, state); // 1 master + 7 stack
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testMasterStack_oneWindow()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMasterStack_twoWindows_defaultRatio()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(4, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(5, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
        QCOMPARE(zones.size(), 3);

        // All should be full width (no stack since masterCount >= windowCount)
        for (const QRect &zone : zones) {
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
        auto zones = algo.calculateZones(3, invalidRect, state);
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testColumns_oneWindow()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testColumns_twoWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
        QCOMPARE(zones.size(), 3);

        // 1920 / 3 = 640, remainder 0 - actually divides evenly
        // All columns should be 640
        int currentX = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(10, m_screenGeometry, state);
        QCOMPARE(zones.size(), 10);

        // Verify contiguous and fill screen
        int currentX = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testBSP_oneWindow()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testBSP_twoWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(4, m_screenGeometry, state);
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testBSP_oddWindowCount()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(5, m_screenGeometry, state);
        QCOMPARE(zones.size(), 5);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_manyWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(16, m_screenGeometry, state);
        QCOMPARE(zones.size(), 16);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));

        // All zones should have reasonable minimum size
        for (const QRect &zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testBSP_squareScreen()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect squareScreen(0, 0, 1000, 1000);
        auto zones = algo.calculateZones(4, squareScreen, state);
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, squareScreen));
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testFibonacci_oneWindow()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testFibonacci_twoWindows_spiralSplit()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(5, m_screenGeometry, state);
        QCOMPARE(zones.size(), 5);

        // First window should have the largest area
        int firstArea = zones[0].width() * zones[0].height();
        for (int i = 1; i < zones.size(); ++i) {
            int area = zones[i].width() * zones[i].height();
            QVERIFY2(firstArea >= area,
                      qPrintable(QStringLiteral("Zone 0 area (%1) should be >= zone %2 area (%3)")
                                     .arg(firstArea).arg(i).arg(area)));
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_manyWindows()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones(12, m_screenGeometry, state);
        QCOMPARE(zones.size(), 12);

        // All zones should have positive dimensions
        for (const QRect &zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFibonacci_minimumSizeEnforcement()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        // Very small screen with many windows: should degrade gracefully
        QRect tinyScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones(20, tinyScreen, state);
        QCOMPARE(zones.size(), 20);

        // When the remaining area is too small to split (< MinZoneSizePx),
        // remaining windows get the same zone (graceful degradation)
        for (const QRect &zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, tinyScreen));
    }

    void testFibonacci_invalidGeometry()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones(3, invalidRect, state);
        QVERIFY(zones.isEmpty());
    }

    void testFibonacci_offsetScreen()
    {
        FibonacciAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones(5, offsetScreen, state);
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_oneWindow()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMonocle_twoWindows_allFullScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
        QCOMPARE(zones.size(), 2);

        // Both zones should be the full screen (all windows overlap)
        QCOMPARE(zones[0], m_screenGeometry);
        QCOMPARE(zones[1], m_screenGeometry);
    }

    void testMonocle_manyWindows_allIdentical()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(10, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(zones.size(), 50);

        for (const QRect &zone : zones) {
            QCOMPARE(zone, m_screenGeometry);
        }
    }

    void testMonocle_invalidGeometry()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones(3, invalidRect, state);
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_offsetScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(200, 100, 1920, 1080);
        auto zones = algo.calculateZones(5, offsetScreen, state);
        QCOMPARE(zones.size(), 5);

        for (const QRect &zone : zones) {
            QCOMPARE(zone, offsetScreen);
        }
    }

    void testMonocle_smallScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect smallScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones(8, smallScreen, state);
        QCOMPARE(zones.size(), 8);

        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testRows_oneWindow()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testRows_twoWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
        QCOMPARE(zones.size(), 3);

        // 1080 / 3 = 360, remainder 0 -- divides evenly
        int currentY = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(7, m_screenGeometry, state);
        QCOMPARE(zones.size(), 7);

        int totalHeight = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(5, m_screenGeometry, state);
        QCOMPARE(zones.size(), 5);

        // Verify each row starts exactly where the previous one ends
        int currentY = 0;
        for (const QRect &zone : zones) {
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

        auto zones = algo.calculateZones(10, m_screenGeometry, state);
        QCOMPARE(zones.size(), 10);

        int currentY = 0;
        for (const QRect &zone : zones) {
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
        auto zones = algo.calculateZones(3, invalidRect, state);
        QVERIFY(zones.isEmpty());
    }

    void testRows_offsetScreen()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones(4, offsetScreen, state);
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

        auto zones = algo.calculateZones(0, m_screenGeometry, state);
        QVERIFY(zones.isEmpty());
    }

    void testThreeColumn_oneWindow()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(1, m_screenGeometry, state);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testThreeColumn_twoWindows_usesSplitRatio()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);

        auto zones = algo.calculateZones(2, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(4, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(5, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(11, m_screenGeometry, state);
        QCOMPARE(zones.size(), 11);

        // All zones should have positive dimensions
        for (const QRect &zone : zones) {
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
        auto zones = algo.calculateZones(3, invalidRect, state);
        QVERIFY(zones.isEmpty());
    }

    void testThreeColumn_offsetScreen()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones(5, offsetScreen, state);
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // applyGaps() tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testApplyGaps_singleZone()
    {
        QVector<QRect> zones = {m_screenGeometry};

        TilingAlgorithm::applyGaps(zones, m_screenGeometry, 10, 20);

        QCOMPARE(zones.size(), 1);
        // Should have outer gap on all sides
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[0].right(), ScreenWidth - 1 - 20);
        QCOMPARE(zones[0].bottom(), ScreenHeight - 1 - 20);
    }

    void testApplyGaps_twoZonesHorizontal()
    {
        QVector<QRect> zones = {
            QRect(0, 0, ScreenWidth / 2, ScreenHeight),
            QRect(ScreenWidth / 2, 0, ScreenWidth / 2, ScreenHeight)
        };

        TilingAlgorithm::applyGaps(zones, m_screenGeometry, 10, 20);

        QCOMPARE(zones.size(), 2);

        // Left zone: outer gap on left/top/bottom, inner gap on right
        QCOMPARE(zones[0].left(), 20); // Outer gap

        // Right zone: inner gap on left, outer gap on right/top/bottom
        QCOMPARE(zones[1].right(), ScreenWidth - 1 - 20); // Outer gap

        // Zones shouldn't overlap
        QVERIFY(!zones[0].intersects(zones[1]));
    }

    void testApplyGaps_zeroGaps()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones(3, m_screenGeometry, state);
        auto originalZones = zones;

        TilingAlgorithm::applyGaps(zones, m_screenGeometry, 0, 0);

        // With zero gaps, zones should be unchanged
        QCOMPARE(zones, originalZones);
    }

    void testApplyGaps_clampsToMax()
    {
        QVector<QRect> zones = {m_screenGeometry};

        // Gap of 100 should be clamped to MaxGap (50)
        TilingAlgorithm::applyGaps(zones, m_screenGeometry, 100, 100);

        QCOMPARE(zones[0].left(), AutotileDefaults::MaxGap);
        QCOMPARE(zones[0].top(), AutotileDefaults::MaxGap);
    }

    void testApplyGaps_oddInnerGap()
    {
        QVector<QRect> zones = {
            QRect(0, 0, ScreenWidth / 2, ScreenHeight),
            QRect(ScreenWidth / 2, 0, ScreenWidth / 2, ScreenHeight)
        };

        // Odd gap of 9: should alternate 4/5 pixels
        TilingAlgorithm::applyGaps(zones, m_screenGeometry, 9, 0);

        // The gap between zones should total 9 pixels
        int gap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap, 9);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Edge case tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testAllAlgorithms_negativeWindowCount()
    {
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        QVERIFY(masterStack.calculateZones(-1, m_screenGeometry, state).isEmpty());

        ColumnsAlgorithm columns;
        QVERIFY(columns.calculateZones(-5, m_screenGeometry, state).isEmpty());

        BSPAlgorithm bsp;
        QVERIFY(bsp.calculateZones(-10, m_screenGeometry, state).isEmpty());

        FibonacciAlgorithm fibonacci;
        QVERIFY(fibonacci.calculateZones(-3, m_screenGeometry, state).isEmpty());

        MonocleAlgorithm monocle;
        QVERIFY(monocle.calculateZones(-1, m_screenGeometry, state).isEmpty());

        RowsAlgorithm rows;
        QVERIFY(rows.calculateZones(-7, m_screenGeometry, state).isEmpty());

        ThreeColumnAlgorithm threeCol;
        QVERIFY(threeCol.calculateZones(-2, m_screenGeometry, state).isEmpty());
    }

    void testAllAlgorithms_largeWindowCount()
    {
        TilingState state(QStringLiteral("test"));

        // Test with 50 windows - should still work without crashes
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(msZones.size(), 50);
        QVERIFY(noOverlaps(msZones));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(colZones.size(), 50);
        QVERIFY(noOverlaps(colZones));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(bspZones.size(), 50);
        QVERIFY(noOverlaps(bspZones));

        FibonacciAlgorithm fibonacci;
        auto fibZones = fibonacci.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(fibZones.size(), 50);

        MonocleAlgorithm monocle;
        auto monZones = monocle.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(monZones.size(), 50);

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(rowZones.size(), 50);
        QVERIFY(noOverlaps(rowZones));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones(50, m_screenGeometry, state);
        QCOMPARE(tcZones.size(), 50);
        QVERIFY(noOverlaps(tcZones));
    }

    void testAllAlgorithms_offsetScreen()
    {
        // Test with screen that doesn't start at (0,0)
        QRect offsetScreen(100, 50, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones(3, offsetScreen, state);
        QCOMPARE(msZones.size(), 3);
        QVERIFY(allWithinBounds(msZones, offsetScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones(3, offsetScreen, state);
        QCOMPARE(colZones.size(), 3);
        QVERIFY(allWithinBounds(colZones, offsetScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones(3, offsetScreen, state);
        QCOMPARE(bspZones.size(), 3);
        QVERIFY(allWithinBounds(bspZones, offsetScreen));

        FibonacciAlgorithm fibonacci;
        auto fibZones = fibonacci.calculateZones(3, offsetScreen, state);
        QCOMPARE(fibZones.size(), 3);
        QVERIFY(allWithinBounds(fibZones, offsetScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones(3, offsetScreen, state);
        QCOMPARE(rowZones.size(), 3);
        QVERIFY(allWithinBounds(rowZones, offsetScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones(3, offsetScreen, state);
        QCOMPARE(tcZones.size(), 3);
        QVERIFY(allWithinBounds(tcZones, offsetScreen));
    }

    void testAllAlgorithms_smallScreen()
    {
        // Very small screen (200x150)
        QRect smallScreen(0, 0, 200, 150);
        TilingState state(QStringLiteral("test"));

        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones(4, smallScreen, state);
        QCOMPARE(msZones.size(), 4);
        QVERIFY(zonesFillScreen(msZones, smallScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones(4, smallScreen, state);
        QCOMPARE(colZones.size(), 4);
        QVERIFY(zonesFillScreen(colZones, smallScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones(4, smallScreen, state);
        QCOMPARE(bspZones.size(), 4);
        QVERIFY(zonesFillScreen(bspZones, smallScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones(4, smallScreen, state);
        QCOMPARE(rowZones.size(), 4);
        QVERIFY(zonesFillScreen(rowZones, smallScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones(4, smallScreen, state);
        QCOMPARE(tcZones.size(), 4);
        QVERIFY(zonesFillScreen(tcZones, smallScreen));
    }
};

QTEST_MAIN(TestTilingAlgorithms)
#include "test_tiling_algorithms.moc"