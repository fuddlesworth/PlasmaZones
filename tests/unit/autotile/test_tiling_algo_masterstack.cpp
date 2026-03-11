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
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestTilingAlgoMasterStack : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
private Q_SLOTS:
    void testPixelPerfect_columnsRemainderDistribution()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({7, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 7);
        int totalWidth = 0;
        for (const QRect& zone : zones) {
            totalWidth += zone.width();
            QVERIFY(zone.width() == 274 || zone.width() == 275);
        }
        QCOMPARE(totalWidth, ScreenWidth);
        QCOMPARE(zones[0].width(), 275);
        QCOMPARE(zones[1].width(), 275);
        QCOMPARE(zones[2].width(), 274);
    }

    void testPixelPerfect_masterStackHeightDistribution()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(1);
        auto zones = algo.calculateZones({8, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 8);
        int totalStackHeight = 0;
        for (int i = 1; i < 8; ++i) {
            totalStackHeight += zones[i].height();
            QVERIFY(zones[i].height() == 154 || zones[i].height() == 155);
        }
        QCOMPARE(totalStackHeight, ScreenHeight);
    }

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
        QVERIFY(algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testMasterStack_oneWindow()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMasterStack_twoWindows_defaultRatio()
    {
        MasterStackAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), static_cast<int>(ScreenWidth * 0.6));
        QCOMPARE(zones[0].height(), ScreenHeight);

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
        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);
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
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(2);
        state.setSplitRatio(0.6);
        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        int masterWidth = static_cast<int>(ScreenWidth * 0.6);
        QCOMPARE(zones[0].width(), masterWidth);
        QCOMPARE(zones[1].width(), masterWidth);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[1].x(), 0);

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
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(5);
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
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
        QVERIFY(algo.calculateZones({3, QRect(), &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testColumns_metadata()
    {
        ColumnsAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Columns"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
    }

    void testColumns_zeroAndOneWindow()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QVERIFY(algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testColumns_twoWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);

        int currentX = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.x(), currentX);
            QCOMPARE(zone.height(), ScreenHeight);
            currentX += zone.width();
        }
        QCOMPARE(currentX, ScreenWidth);
        QVERIFY(noOverlaps(zones));
    }

    void testColumns_manyWindows()
    {
        ColumnsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 10);

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

    void testBSP_metadata()
    {
        BSPAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Binary Split"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testBSP_zeroAndOneWindow()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QVERIFY(algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testBSP_twoWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);

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

        auto zones = algo.calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testBSP_oddWindowCount()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_manyWindows()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({16, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 16);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));

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
        auto zones = algo.calculateZones({4, squareScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, squareScreen));
    }

    void testBSP_deterministic()
    {
        BSPAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones1 = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto zones2 = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones1, zones2);
        algo.calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto zones3 = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones1, zones3);
    }

    void testDwindle_metadata()
    {
        DwindleAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Dwindle"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testDwindle_zeroAndOneWindow()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QVERIFY(algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testDwindle_twoWindows_dwindleSplit()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);

        int expectedWidth = static_cast<int>(ScreenWidth * 0.5);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), expectedWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);

        QCOMPARE(zones[1].x(), expectedWidth);
        QCOMPARE(zones[1].width(), ScreenWidth - expectedWidth);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testDwindle_threeWindows_dwindlePattern()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);

        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);

        QCOMPARE(zones[1].x(), ScreenWidth / 2);
        QCOMPARE(zones[1].height(), ScreenHeight / 2);

        QCOMPARE(zones[2].x(), ScreenWidth / 2);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testDwindle_customRatio_firstWindowLargest()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);

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

    void testDwindle_manyWindows()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = algo.calculateZones({12, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 12);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testDwindle_minimumSizeEnforcement()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);
        QRect tinyScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones({20, tinyScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 20);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        QVERIFY(allWithinBounds(zones, tinyScreen));
    }

    void testDwindle_invalidGeometry()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QVERIFY(algo.calculateZones({3, QRect(), &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testDwindle_offsetScreen()
    {
        DwindleAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }
};

QTEST_MAIN(TestTilingAlgoMasterStack)
#include "test_tiling_algo_masterstack.moc"
