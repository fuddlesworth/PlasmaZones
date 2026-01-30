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

        auto zones = algo.calculateZones(7, m_screenGeometry, state);
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

        auto zones = algo.calculateZones(10, m_screenGeometry, state);
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
        auto zones = algo.calculateZones(4, squareScreen, state);
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, squareScreen));
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
        QVector<QRect> zones = {QRect(0, 0, ScreenWidth / 2, ScreenHeight),
                                QRect(ScreenWidth / 2, 0, ScreenWidth / 2, ScreenHeight)};

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
        QVector<QRect> zones = {QRect(0, 0, ScreenWidth / 2, ScreenHeight),
                                QRect(ScreenWidth / 2, 0, ScreenWidth / 2, ScreenHeight)};

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
    }
};

QTEST_MAIN(TestTilingAlgorithms)
#include "test_tiling_algorithms.moc"
