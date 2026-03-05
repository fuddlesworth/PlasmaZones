// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/ThreeColumnAlgorithm.h"
#include "autotile/algorithms/GridAlgorithm.h"
#include "autotile/algorithms/ColumnsAlgorithm.h"
#include "autotile/algorithms/MasterStackAlgorithm.h"
#include "autotile/algorithms/RowsAlgorithm.h"
#include "autotile/algorithms/BSPAlgorithm.h"
#include "autotile/algorithms/DwindleAlgorithm.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestTilingAlgoGridThreeColumn : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
private Q_SLOTS:
    void testThreeColumn_metadata()
    {
        ThreeColumnAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Three Column"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), 0);
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testThreeColumn_zeroAndOneWindow()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QVERIFY(algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testThreeColumn_twoWindows_usesSplitRatio()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({11, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 11);

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
        QVERIFY(algo.calculateZones({3, QRect(), &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testThreeColumn_offsetScreen()
    {
        ThreeColumnAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }

    void testThreeColumn_withGaps()
    {
        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        ThreeColumnAlgorithm algo;
        auto zones = algo.calculateZones({3, screen, &state, 10, EdgeGaps::uniform(20)});

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
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 10, EdgeGaps::uniform(20)});

        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[0].width(), ScreenWidth - 40);
        QCOMPARE(zones[0].height(), ScreenHeight - 40);
    }

    void testGapAware_twoColumnsWithGaps()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 10, EdgeGaps::uniform(20)});

        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones[0].left(), 20);
        QCOMPARE(zones[0].top(), 20);
        QCOMPARE(zones[1].right(), ScreenWidth - 20 - 1);
        const int gap = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap, 10);
        QVERIFY(!zones[0].intersects(zones[1]));
    }

    void testGapAware_zeroGapsUnchanged()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zonesNoGap = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // Use different variable name to avoid self-comparison
        auto zonesWithZeroGap = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        // With zero gaps, both calculations produce identical results
        QCOMPARE(zonesNoGap, zonesWithZeroGap);

        // Verify that zones actually tile correctly (non-trivial assertion)
        QCOMPARE(zonesNoGap.size(), 3);
        QVERIFY(noOverlaps(zonesNoGap));
        QVERIFY(zonesFillScreen(zonesNoGap, m_screenGeometry));
    }

    void testGapAware_innerGapBetweenColumns()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 9, EdgeGaps::uniform(0)});

        QCOMPARE(zones.size(), 3);
        const int gap01 = zones[1].left() - zones[0].right() - 1;
        QCOMPARE(gap01, 9);
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
        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 8, EdgeGaps::uniform(outerGap)});

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
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 8, EdgeGaps::uniform(8)});

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

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 8, EdgeGaps::uniform(8)});

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
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 10, EdgeGaps::uniform(15)});

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
        GridAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testGrid_fourWindows()
    {
        GridAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        GridAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        GridAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({9, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 9);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));

        QCOMPARE(zones[0].width(), 640);
        QCOMPARE(zones[0].height(), 360);
    }

    void testGrid_withGaps()
    {
        GridAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 10, EdgeGaps::uniform(20)});
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));

        QRect area(20, 20, 1880, 1040);
        QVERIFY(allWithinBounds(zones, area));

        QVERIFY(zones[1].x() > zones[0].right());
        QVERIFY(zones[2].y() > zones[0].bottom());
    }

    void testGrid_metadata()
    {
        GridAlgorithm algo;
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.defaultMaxWindows(), 9);
    }

    void test_bspNegativeContentWidth()
    {
        BSPAlgorithm algo;
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, screen, &state, 200, EdgeGaps::uniform(10)});
        QCOMPARE(zones.size(), 3);
        for (const auto& z : zones) {
            QVERIFY2(
                z.width() > 0 && z.height() > 0,
                qPrintable(QStringLiteral("Zone %1x%2 has non-positive dimension").arg(z.width()).arg(z.height())));
        }
    }

    void test_dwindleGapExceedsRemaining()
    {
        DwindleAlgorithm algo;
        QRect screen(0, 0, 200, 200);
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, screen, &state, 80, EdgeGaps::uniform(10)});
        QCOMPARE(zones.size(), 5);
        for (const auto& z : zones) {
            QVERIFY2(
                z.width() > 0 && z.height() > 0,
                qPrintable(QStringLiteral("Zone %1x%2 has non-positive dimension").arg(z.width()).arg(z.height())));
        }
    }

    void test_masterStackUnsatisfiableMinWidths()
    {
        MasterStackAlgorithm algo;
        QRect screen(0, 0, 400, 400);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        QVector<QSize> minSizes = {QSize(300, 0), QSize(300, 0)};
        auto zones = algo.calculateZones({2, screen, &state, 10, EdgeGaps::uniform(0), minSizes});
        QCOMPARE(zones.size(), 2);

        QVERIFY2(zones[0].width() > 0, "Master width must be positive");
        QVERIFY2(zones[1].width() > 0, "Stack width must be positive");
        QCOMPARE(zones[0].width() + 10 + zones[1].width(), 400);
    }

    void test_threeColumnUnsatisfiableMinWidths()
    {
        ThreeColumnAlgorithm algo;
        QRect screen(0, 0, 300, 300);
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        QVector<QSize> minSizes = {QSize(200, 0), QSize(200, 0), QSize(200, 0)};
        auto zones = algo.calculateZones({3, screen, &state, 10, EdgeGaps::uniform(0), minSizes});
        QCOMPARE(zones.size(), 3);

        for (int i = 0; i < 3; ++i) {
            QVERIFY2(zones[i].width() > 0,
                     qPrintable(QStringLiteral("Zone %1 width must be positive, got %2").arg(i).arg(zones[i].width())));
        }
    }
};

QTEST_MAIN(TestTilingAlgoGridThreeColumn)
#include "test_tiling_algo_grid_threecolumn.moc"
