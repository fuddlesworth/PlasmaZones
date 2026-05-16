// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestTilingAlgoBsp : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_landscapeScreen{0, 0, ScreenWidth, ScreenHeight};
    QRect m_portraitScreen{0, 0, 1080, 1920};
    ScriptedAlgoTestSetup m_scriptSetup;

    PhosphorTiles::TilingAlgorithm* bsp()
    {
        return m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
    }

    /**
     * @brief Convenience wrapper to run BSP with a given split ratio
     */
    QVector<QRect> runBsp(const QRect& screen, int windowCount, int gap, qreal splitRatio)
    {
        auto* algo = bsp();
        if (!algo)
            return {};
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(splitRatio);
        return algo->calculateZones(
            makeParams(windowCount, screen, &state, gap, ::PhosphorLayout::EdgeGaps::uniform(0)));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(bsp() != nullptr);
    }

    // =========================================================================
    // 1. Basic BSP zones — various window counts on 1920x1080 with gap=10
    // =========================================================================

    void testBasicBsp_twoWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 2, 10, 0.5);
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
    }

    void testBasicBsp_threeWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 3, 10, 0.5);
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
    }

    void testBasicBsp_fourWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 4, 10, 0.5);
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
    }

    void testBasicBsp_fiveWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 5, 10, 0.5);
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
    }

    // =========================================================================
    // 2. Split direction heuristic — landscape vs portrait first split
    // =========================================================================

    void testSplitDirection_landscapeFirstSplitVertical()
    {
        // On a landscape screen (wider than tall), the first BSP split should
        // be vertical (left/right), producing two zones side by side.
        auto zones = runBsp(m_landscapeScreen, 2, 0, 0.5);
        QCOMPARE(zones.size(), 2);

        // Both zones should span the full height (vertical split = left/right)
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[1].y(), 0);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].height(), ScreenHeight);
    }

    void testSplitDirection_portraitFirstSplitHorizontal()
    {
        // On a portrait screen (taller than wide), the first BSP split should
        // be horizontal (top/bottom), producing two zones stacked vertically.
        auto zones = runBsp(m_portraitScreen, 2, 0, 0.5);
        QCOMPARE(zones.size(), 2);

        // Both zones should span the full width (horizontal split = top/bottom)
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[0].width(), 1080);
        QCOMPARE(zones[1].width(), 1080);
    }

    // =========================================================================
    // 3. Fallback / degenerate input — tiny screen with large gaps
    // =========================================================================

    void testFallback_tinyScreenLargeGap()
    {
        // Very small screen with large gap and many windows should still
        // produce valid zones (tests graceful degradation / fallback paths).
        QRect tinyScreen(0, 0, 50, 50);
        auto zones = runBsp(tinyScreen, 5, 40, 0.5);

        // Must produce exactly 5 zones regardless of constraints
        QCOMPARE(zones.size(), 5);

        // All zones must have positive dimensions
        for (const QRect& zone : zones) {
            QVERIFY2(zone.width() > 0, qPrintable(QStringLiteral("Zone width was %1").arg(zone.width())));
            QVERIFY2(zone.height() > 0, qPrintable(QStringLiteral("Zone height was %1").arg(zone.height())));
        }

        QVERIFY(allWithinBounds(zones, tinyScreen));
    }

    // =========================================================================
    // 4. MaxBSPDepth — 50 windows must still terminate and produce zones
    // =========================================================================

    void testMaxBspDepth_fiftyWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 50, 0, 0.5);
        QCOMPARE(zones.size(), 50);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
    }

    // =========================================================================
    // 5. splitRatio variation — asymmetric ratios
    // =========================================================================

    void testSplitRatio_030()
    {
        auto zones = runBsp(m_landscapeScreen, 4, 0, 0.3);
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
        QVERIFY(zonesFillScreen(zones, m_landscapeScreen));

        // With ratio 0.3, first partition's left half should be narrower
        // than 50% of screen width (split at ~30%)
        // Find the zone(s) in the left partition — at least one zone's right
        // edge should be less than half the screen width.
        bool hasNarrowLeft = false;
        for (const QRect& zone : zones) {
            if (zone.x() == 0 && zone.width() < ScreenWidth / 2) {
                hasNarrowLeft = true;
                break;
            }
        }
        QVERIFY2(hasNarrowLeft, "splitRatio=0.3 should produce a narrow left partition");
    }

    void testSplitRatio_070()
    {
        auto zones = runBsp(m_landscapeScreen, 4, 0, 0.7);
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_landscapeScreen));
        QVERIFY(zonesFillScreen(zones, m_landscapeScreen));

        // With ratio 0.7 and 4 windows (gap=0), the root splits at ~70%.
        // BSP then subdivides the larger left partition, so no single zone
        // at x=0 spans 70%. Instead, verify that some zone starts beyond
        // the 50% midpoint (the right partition from the asymmetric split).
        bool hasRightPartition = false;
        for (const QRect& zone : zones) {
            if (zone.x() >= ScreenWidth / 2) {
                hasRightPartition = true;
                break;
            }
        }
        QVERIFY2(hasRightPartition, "splitRatio=0.7 should produce an asymmetric split with right partition");
    }

    void testSplitRatio_030vs070_asymmetric()
    {
        // Verify that 0.3 and 0.7 produce different layouts
        auto zones30 = runBsp(m_landscapeScreen, 4, 0, 0.3);
        auto zones70 = runBsp(m_landscapeScreen, 4, 0, 0.7);
        QCOMPARE(zones30.size(), 4);
        QCOMPARE(zones70.size(), 4);

        // The layouts should differ
        QVERIFY2(zones30 != zones70, "splitRatio 0.3 and 0.7 should produce different BSP layouts");
    }

    // =========================================================================
    // 6. PhosphorZones::Zone dimensions positive — comprehensive check
    // =========================================================================

    void testAllZonesPositiveDimensions_variousCounts()
    {
        const int counts[] = {1, 2, 3, 4, 5, 8, 10, 16, 20};
        for (int count : counts) {
            auto zones = runBsp(m_landscapeScreen, count, 5, 0.5);
            QCOMPARE(zones.size(), count);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(
                    zones[i].width() > 0,
                    qPrintable(
                        QStringLiteral("windowCount=%1, zone %2: width=%3").arg(count).arg(i).arg(zones[i].width())));
                QVERIFY2(
                    zones[i].height() > 0,
                    qPrintable(
                        QStringLiteral("windowCount=%1, zone %2: height=%3").arg(count).arg(i).arg(zones[i].height())));
            }
        }
    }

    // =========================================================================
    // Additional BSP-specific tests
    // =========================================================================

    void testBsp_deterministic()
    {
        // Same inputs should always produce the same output
        auto zones1 = runBsp(m_landscapeScreen, 6, 10, 0.5);
        auto zones2 = runBsp(m_landscapeScreen, 6, 10, 0.5);
        QCOMPARE(zones1, zones2);

        // Running a different config in between should not affect determinism
        runBsp(m_landscapeScreen, 3, 5, 0.3);
        auto zones3 = runBsp(m_landscapeScreen, 6, 10, 0.5);
        QCOMPARE(zones1, zones3);
    }

    void testBsp_noGapsFillScreen()
    {
        // With zero gaps, BSP zones should perfectly tile the screen
        auto zones = runBsp(m_landscapeScreen, 4, 0, 0.5);
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_landscapeScreen));
    }

    void testBsp_offsetScreen()
    {
        // BSP should respect screen origin offset
        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = runBsp(offsetScreen, 4, 0, 0.5);
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    void testBsp_singleWindow()
    {
        auto zones = runBsp(m_landscapeScreen, 1, 10, 0.5);
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_landscapeScreen);
    }

    void testBsp_zeroWindows()
    {
        auto zones = runBsp(m_landscapeScreen, 0, 10, 0.5);
        QVERIFY(zones.isEmpty());
    }
};

QTEST_MAIN(TestTilingAlgoBsp)
#include "test_tiling_algo_bsp.moc"
