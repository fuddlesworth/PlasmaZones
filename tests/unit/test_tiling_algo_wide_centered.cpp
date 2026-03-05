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
#include "autotile/algorithms/DwindleAlgorithm.h"
#include "autotile/algorithms/SpiralAlgorithm.h"
#include "autotile/algorithms/MonocleAlgorithm.h"
#include "autotile/algorithms/RowsAlgorithm.h"
#include "autotile/algorithms/ThreeColumnAlgorithm.h"
#include "autotile/algorithms/GridAlgorithm.h"
#include "autotile/algorithms/WideAlgorithm.h"
#include "autotile/algorithms/CenteredMasterAlgorithm.h"
#include "core/constants.h"

#include "helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestTilingAlgoWideCentered : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
private Q_SLOTS:
    void testWide_singleWindow()
    {
        WideAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testWide_twoWindows()
    {
        WideAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth);
        QCOMPARE(zones[1].width(), ScreenWidth);
        QCOMPARE(zones[0].height() + zones[1].height(), ScreenHeight);
    }

    void testWide_withMasterCountTwo()
    {
        WideAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(2);
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[1].y(), 0);
        QCOMPARE(zones[0].width() + zones[1].width(), ScreenWidth);
        QCOMPARE(zones[2].width(), ScreenWidth);
    }

    void testWide_withSplitRatio()
    {
        WideAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].height() > zones[1].height());
    }

    void testWide_allMasters()
    {
        WideAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(3);
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(zones[i].y(), 0);
            QCOMPARE(zones[i].height(), ScreenHeight);
        }
        int totalWidth = 0;
        for (const auto& z : zones)
            totalWidth += z.width();
        QCOMPARE(totalWidth, ScreenWidth);
    }

    void testWide_metadata()
    {
        WideAlgorithm algo;
        QVERIFY(algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.defaultMaxWindows(), 5);
    }

    void testCenteredMaster_singleWindow()
    {
        CenteredMasterAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testCenteredMaster_twoWindows()
    {
        CenteredMasterAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testCenteredMaster_threeWindows()
    {
        CenteredMasterAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zones[0].x() > zones[1].x());
        QVERIFY(zones[0].x() < zones[2].x());
    }

    void testCenteredMaster_withMasterCountTwo()
    {
        CenteredMasterAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(2);
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].x(), zones[1].x());
        QCOMPARE(zones[0].width(), zones[1].width());
        QVERIFY(zones[1].y() > zones[0].y());
    }

    void testCenteredMaster_fiveWindows()
    {
        CenteredMasterAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].height(), ScreenHeight);
        QVERIFY(zones[1].x() < zones[0].x());
        QVERIFY(zones[3].x() < zones[0].x());
        QVERIFY(zones[2].x() > zones[0].x());
        QVERIFY(zones[4].x() > zones[0].x());
    }

    void testCenteredMaster_metadata()
    {
        CenteredMasterAlgorithm algo;
        QVERIFY(algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.defaultMaxWindows(), 7);
    }

    void testAllAlgorithms_negativeWindowCount()
    {
        TilingState state(QStringLiteral("test"));
        MasterStackAlgorithm masterStack;
        ColumnsAlgorithm columns;
        BSPAlgorithm bsp;
        DwindleAlgorithm dwindle;
        MonocleAlgorithm monocle;
        RowsAlgorithm rows;
        ThreeColumnAlgorithm threeCol;
        GridAlgorithm grid;
        WideAlgorithm wide;
        CenteredMasterAlgorithm centeredMaster;
        QVERIFY(masterStack.calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(columns.calculateZones({-5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(bsp.calculateZones({-10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(dwindle.calculateZones({-3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(monocle.calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(rows.calculateZones({-7, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(threeCol.calculateZones({-2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(grid.calculateZones({-3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(wide.calculateZones({-4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(centeredMaster.calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testAllAlgorithms_largeWindowCount()
    {
        TilingState state(QStringLiteral("test"));
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(msZones.size(), 50);
        QVERIFY(noOverlaps(msZones));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(colZones.size(), 50);
        QVERIFY(noOverlaps(colZones));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(bspZones.size(), 50);
        QVERIFY(noOverlaps(bspZones));

        DwindleAlgorithm dwindle;
        auto dwindleZones = dwindle.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(dwindleZones.size(), 50);

        MonocleAlgorithm monocle;
        auto monZones = monocle.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(monZones.size(), 50);

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(rowZones.size(), 50);
        QVERIFY(noOverlaps(rowZones));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(tcZones.size(), 50);
        QVERIFY(noOverlaps(tcZones));

        GridAlgorithm grid;
        auto gridZones = grid.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(gridZones.size(), 50);
        QVERIFY(noOverlaps(gridZones));

        WideAlgorithm wide;
        auto wideZones = wide.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(wideZones.size(), 50);
        QVERIFY(noOverlaps(wideZones));

        CenteredMasterAlgorithm centeredMaster;
        auto cmZones = centeredMaster.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(cmZones.size(), 50);
        QVERIFY(noOverlaps(cmZones));
    }

    void testAllAlgorithms_offsetScreen()
    {
        QRect offsetScreen(100, 50, 1920, 1080);
        TilingState state(QStringLiteral("test"));
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(msZones.size(), 3);
        QVERIFY(allWithinBounds(msZones, offsetScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(colZones.size(), 3);
        QVERIFY(allWithinBounds(colZones, offsetScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(bspZones.size(), 3);
        QVERIFY(allWithinBounds(bspZones, offsetScreen));

        DwindleAlgorithm dwindle;
        auto dwindleZones = dwindle.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(dwindleZones.size(), 3);
        QVERIFY(allWithinBounds(dwindleZones, offsetScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(rowZones.size(), 3);
        QVERIFY(allWithinBounds(rowZones, offsetScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(tcZones.size(), 3);
        QVERIFY(allWithinBounds(tcZones, offsetScreen));

        GridAlgorithm grid;
        auto gridZones = grid.calculateZones({4, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(gridZones.size(), 4);
        QVERIFY(allWithinBounds(gridZones, offsetScreen));

        WideAlgorithm wide;
        auto wideZones = wide.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(wideZones.size(), 3);
        QVERIFY(allWithinBounds(wideZones, offsetScreen));

        CenteredMasterAlgorithm centeredMaster;
        auto cmZones = centeredMaster.calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(cmZones.size(), 3);
        QVERIFY(allWithinBounds(cmZones, offsetScreen));
    }

    void testAllAlgorithms_smallScreen()
    {
        QRect smallScreen(0, 0, 200, 150);
        TilingState state(QStringLiteral("test"));
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(msZones.size(), 4);
        QVERIFY(zonesFillScreen(msZones, smallScreen));

        ColumnsAlgorithm columns;
        auto colZones = columns.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(colZones.size(), 4);
        QVERIFY(zonesFillScreen(colZones, smallScreen));

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(bspZones.size(), 4);
        QVERIFY(zonesFillScreen(bspZones, smallScreen));

        RowsAlgorithm rows;
        auto rowZones = rows.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(rowZones.size(), 4);
        QVERIFY(zonesFillScreen(rowZones, smallScreen));

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(tcZones.size(), 4);
        QVERIFY(zonesFillScreen(tcZones, smallScreen));

        GridAlgorithm grid;
        auto gridZones = grid.calculateZones({4, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(gridZones.size(), 4);
        QVERIFY(zonesFillScreen(gridZones, smallScreen));
    }

    void test_negativeScreenCoordinates()
    {
        QRect screen(-1920, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));
        MasterStackAlgorithm masterStack;
        auto msZones = masterStack.calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(msZones.size(), 3);
        for (int i = 0; i < msZones.size(); ++i) {
            QVERIFY2(msZones[i].left() >= screen.left(),
                     qPrintable(QStringLiteral("MasterStack zone %1 left %2 < screen left %3")
                                    .arg(i)
                                    .arg(msZones[i].left())
                                    .arg(screen.left())));
            QVERIFY2(msZones[i].right() <= screen.right(),
                     qPrintable(QStringLiteral("MasterStack zone %1 extends past screen right").arg(i)));
            QVERIFY2(msZones[i].width() > 0 && msZones[i].height() > 0,
                     qPrintable(QStringLiteral("MasterStack zone %1 has non-positive dimensions").arg(i)));
        }

        BSPAlgorithm bsp;
        auto bspZones = bsp.calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(bspZones.size(), 4);
        for (int i = 0; i < bspZones.size(); ++i) {
            QVERIFY2(bspZones[i].left() >= screen.left(),
                     qPrintable(QStringLiteral("BSP zone %1 left %2 < screen left %3")
                                    .arg(i)
                                    .arg(bspZones[i].left())
                                    .arg(screen.left())));
        }

        ThreeColumnAlgorithm threeCol;
        auto tcZones = threeCol.calculateZones({5, screen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(tcZones.size(), 5);
        for (int i = 0; i < tcZones.size(); ++i) {
            QVERIFY2(tcZones[i].left() >= screen.left(),
                     qPrintable(QStringLiteral("ThreeColumn zone %1 left %2 < screen left %3")
                                    .arg(i)
                                    .arg(tcZones[i].left())
                                    .arg(screen.left())));
        }

        QRect topScreen(0, -1080, 1920, 1080);
        ColumnsAlgorithm cols;
        auto colZones = cols.calculateZones({3, topScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(colZones.size(), 3);
        for (int i = 0; i < colZones.size(); ++i) {
            QVERIFY2(colZones[i].top() >= topScreen.top(),
                     qPrintable(QStringLiteral("Columns zone %1 top %2 < screen top %3")
                                    .arg(i)
                                    .arg(colZones[i].top())
                                    .arg(topScreen.top())));
        }
    }

    void test_splitRatioBoundaryValues()
    {
        QRect screen(0, 0, 1920, 1080);
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);

            MasterStackAlgorithm algo;
            auto zones = algo.calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 3);
            QVERIFY2(zones[0].width() > 0,
                     qPrintable(QStringLiteral("Master width %1 should be > 0 with ratio 0.1").arg(zones[0].width())));
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                         qPrintable(QStringLiteral("Zone %1 has non-positive dimension at ratio 0.1").arg(i)));
            }
        }

        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);

            MasterStackAlgorithm algo;
            auto zones = algo.calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 3);
            for (int i = 1; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                         qPrintable(QStringLiteral("Stack zone %1 has non-positive dimension at ratio 0.9").arg(i)));
            }
        }

        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);

            DwindleAlgorithm algo;
            auto zones = algo.calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 4);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                         qPrintable(QStringLiteral("Dwindle zone %1 non-positive at ratio 0.1").arg(i)));
            }
        }

        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);

            ThreeColumnAlgorithm algo;
            auto zones = algo.calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 4);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                         qPrintable(QStringLiteral("ThreeColumn zone %1 non-positive at ratio 0.9").arg(i)));
            }
        }
    }

    void test_nullStatePointer()
    {
        QRect screen(0, 0, 1920, 1080);
        // Algorithms that require state return empty
        QCOMPARE(BSPAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        QCOMPARE(DwindleAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        QCOMPARE(MasterStackAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        QCOMPARE(ThreeColumnAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        QCOMPARE(WideAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        QCOMPARE(CenteredMasterAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 0);
        // Algorithms that work without state return zones
        QCOMPARE(GridAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(ColumnsAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(RowsAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(MonocleAlgorithm().calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
    }

    void test_innerRectHugeOuterGap()
    {
        ColumnsAlgorithm algo;
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, screen, &state, 0, EdgeGaps::uniform(500)});
        QCOMPARE(zones.size(), 1);
        QVERIFY2(zones[0].width() >= 1, "Zone width must be at least 1");
        QVERIFY2(zones[0].height() >= 1, "Zone height must be at least 1");
        QVERIFY2(zones[0].left() >= screen.left(), "Zone must not extend left of screen");
        QVERIFY2(zones[0].top() >= screen.top(), "Zone must not extend above screen");
        QVERIFY2(zones[0].right() <= screen.right(), "Zone must not extend right of screen");
        QVERIFY2(zones[0].bottom() <= screen.bottom(), "Zone must not extend below screen");
    }
};

QTEST_MAIN(TestTilingAlgoWideCentered)
#include "test_tiling_algo_wide_centered.moc"
