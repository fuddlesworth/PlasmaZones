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

/**
 * @brief Unit tests for grid, three-column, columns, rows, BSP, dwindle,
 *        and master-stack tiling algorithms.
 *
 * Despite the file name (test_tiling_algo_grid_threecolumn), this file covers
 * a broader set of algorithms:
 *   - grid:          layout, gaps, metadata, zero/single/many windows
 *   - three-column:  center-master layout, interleaved filling, gaps, offsets
 *   - columns:       zero-window, single-zone, gap-aware tests
 *   - rows:          zero-window, gap-aware tests
 *   - bsp:           negative-content-width edge case
 *   - dwindle:       gap-exceeds-remaining edge case
 *   - master-stack:  gap-aware layout, unsatisfiable minWidths
 *
 * The file name is kept for backwards compatibility with existing CI
 * configurations and CMakeLists.txt references.
 */
class TestTilingAlgoGridThreeColumn : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* threeCol()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("three-column"));
    }
    TilingAlgorithm* grid()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("grid"));
    }
    TilingAlgorithm* masterStack()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
    }
    TilingAlgorithm* dw()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(threeCol() != nullptr);
        QVERIFY(grid() != nullptr);
        QVERIFY(masterStack() != nullptr);
        QVERIFY(dw() != nullptr);
    }

    void testThreeColumn_metadata()
    {
        auto* algo = threeCol();
        QCOMPARE(algo->name(), QStringLiteral("Three Column"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
        QCOMPARE(algo->defaultSplitRatio(), 0.5);
    }

    void testThreeColumn_zeroWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(threeCol()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testGrid_zeroWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(grid()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testColumns_zeroWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(QLatin1String("columns"))
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testRows_zeroWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(QLatin1String("rows"))
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testThreeColumn_zeroAndOneWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = threeCol()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testThreeColumn_twoWindows_usesSplitRatio()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = threeCol()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = threeCol()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);
        int rightWidth = ScreenWidth - sideWidth - centerWidth;
        QCOMPARE(zones[0].x(), sideWidth);
        QCOMPARE(zones[0].width(), centerWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[1].width(), sideWidth);
        QCOMPARE(zones[1].height(), ScreenHeight);
        QCOMPARE(zones[2].x(), sideWidth + centerWidth);
        QCOMPARE(zones[2].width(), rightWidth);
        QCOMPARE(zones[2].height(), ScreenHeight);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_fourWindows_interleavedFilling()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = threeCol()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);
        QCOMPARE(zones[0].x(), sideWidth);
        QCOMPARE(zones[0].width(), centerWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].x(), 0);
        QCOMPARE(zones[1].width(), sideWidth);
        QCOMPARE(zones[2].x(), sideWidth + centerWidth);
        QCOMPARE(zones[2].height(), ScreenHeight);
        QCOMPARE(zones[3].x(), 0);
        QCOMPARE(zones[3].width(), sideWidth);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_fiveWindows_distribution()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = threeCol()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        int sideWidth = static_cast<int>(ScreenWidth * 0.25);
        int centerWidth = static_cast<int>(ScreenWidth * 0.5);
        QCOMPARE(zones[0].x(), sideWidth);
        QCOMPARE(zones[0].width(), centerWidth);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testThreeColumn_manyWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = threeCol()->calculateZones(
            makeParams(11, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 11);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testThreeColumn_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones =
            threeCol()->calculateZones(makeParams(5, offsetScreen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    void testThreeColumn_withGaps()
    {
        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6); // Explicit ratio for deterministic geometry
        auto zones =
            threeCol()->calculateZones(makeParams(3, screen, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(20)));
        QCOMPARE(zones.size(), 3);
        QCOMPARE(zones[0].x(), 402);
        QCOMPARE(zones[0].width(), 1116);
        QCOMPARE(zones[0].y(), 20);
        QCOMPARE(zones[0].height(), 1040);
        QCOMPARE(zones[1].x(), 20);
        QCOMPARE(zones[1].width(), 372);
        QCOMPARE(zones[2].x(), 1528);
        QCOMPARE(zones[2].width(), 372);
    }

    void testGapAware_singleZoneOuterGap()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            AlgorithmRegistry::instance()
                ->algorithm(QLatin1String("columns"))
                ->calculateZones(makeParams(1, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(20)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[0].width(), ScreenWidth - 40);
        QCOMPARE(zones[0].height(), ScreenHeight - 40);
    }

    void testGapAware_twoColumnsWithGaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            AlgorithmRegistry::instance()
                ->algorithm(QLatin1String("columns"))
                ->calculateZones(makeParams(2, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(20)));
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[1].right(), ScreenWidth - 20 - 1);
        const int gap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap, 10);
        QVERIFY(!zones[0].intersects(zones[1]));
    }

    void testGapAware_masterStackWithGaps()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("w1"));
        state.addWindow(QStringLiteral("w2"));
        state.addWindow(QStringLiteral("w3"));
        state.setSplitRatio(0.6);
        auto zones = masterStack()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 8, ::PhosphorLayout::EdgeGaps::uniform(8)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].left(), 8);
        QCOMPARE(zones[0].top(), 8);
        const int hGap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(hGap, 8);
        const int vGap = zones[2].top() - zones[1].bottom() - 1;
        QCOMPARE(vGap, 8);
    }

    void testGapAware_rowsWithGaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            AlgorithmRegistry::instance()
                ->algorithm(QLatin1String("rows"))
                ->calculateZones(makeParams(3, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(15)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        for (const QRect& zone : zones) {
            QCOMPARE(zone.left(), 15);
            QCOMPARE(zone.width(), ScreenWidth - 30);
        }
        const int gap01 = zones[1].top() - zones[0].bottom() - 1;
        QCOMPARE(gap01, 10);
    }

    void testGrid_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            grid()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testGrid_fourWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            grid()->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QCOMPARE(zones[0].width(), 960);
        QCOMPARE(zones[0].height(), 540);
        QCOMPARE(zones[1].width(), 960);
        QCOMPARE(zones[3].x(), 960);
        QCOMPARE(zones[3].y(), 540);
    }

    void testGrid_fiveWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            grid()->calculateZones(makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[1].y(), 0);
        QCOMPARE(zones[2].y(), 0);
        QCOMPARE(zones[3].y(), zones[0].height());
        QCOMPARE(zones[4].y(), zones[0].height());
        QCOMPARE(zones[3].width() + zones[4].width(), ScreenWidth);
    }

    void testGrid_nineWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones =
            grid()->calculateZones(makeParams(9, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 9);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QCOMPARE(zones[0].width(), 640);
        QCOMPARE(zones[0].height(), 360);
    }

    void testGrid_withGaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = grid()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(20)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QRect area(20, 20, 1880, 1040);
        QVERIFY(allWithinBounds(zones, area));
        QVERIFY(zones[1].x() > zones[0].right());
        QVERIFY(zones[2].y() > zones[0].bottom());
    }

    void testGrid_metadata()
    {
        auto* algo = grid();
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
        QCOMPARE(algo->defaultMaxWindows(), 9);
    }

    void test_bspNegativeContentWidth()
    {
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));
        auto zones = AlgorithmRegistry::instance()
                         ->algorithm(QLatin1String("bsp"))
                         ->calculateZones(makeParams(3, screen, &state, 200, ::PhosphorLayout::EdgeGaps::uniform(10)));
        QCOMPARE(zones.size(), 3);
        for (const auto& z : zones) {
            QVERIFY(z.width() > 0 && z.height() > 0);
        }
    }

    void test_dwindleGapExceedsRemaining()
    {
        QRect screen(0, 0, 200, 200);
        TilingState state(QStringLiteral("test"));
        auto zones = dw()->calculateZones(makeParams(5, screen, &state, 80, ::PhosphorLayout::EdgeGaps::uniform(10)));
        QCOMPARE(zones.size(), 5);
        for (const auto& z : zones) {
            QVERIFY(z.width() > 0 && z.height() > 0);
        }
    }

    void test_masterStackUnsatisfiableMinWidths()
    {
        QRect screen(0, 0, 400, 400);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        QVector<QSize> minSizes = {QSize(300, 0), QSize(300, 0)};
        auto zones = masterStack()->calculateZones(
            makeParams(2, screen, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0), minSizes));
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[1].width() > 0);
        QCOMPARE(zones[0].width() + 10 + zones[1].width(), 400);
    }

    void test_threeColumnUnsatisfiableMinWidths()
    {
        QRect screen(0, 0, 300, 300);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        QVector<QSize> minSizes = {QSize(200, 0), QSize(200, 0), QSize(200, 0)};
        auto zones = threeCol()->calculateZones(
            makeParams(3, screen, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0), minSizes));
        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(zones[i].width() > 0);
        }
    }
};

QTEST_MAIN(TestTilingAlgoGridThreeColumn)
#include "test_tiling_algo_grid_threecolumn.moc"
