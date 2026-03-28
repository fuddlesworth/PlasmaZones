// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestTilingAlgoMasterStack : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* ms()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
    }
    TilingAlgorithm* dw()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle"));
    }
    TilingAlgorithm* bsp()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("bsp"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(ms() != nullptr);
        QVERIFY(dw() != nullptr);
        QVERIFY(bsp() != nullptr);
    }

    void testPixelPerfect_masterStackHeightDistribution()
    {
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(1);
        auto zones = ms()->calculateZones({8, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        auto* algo = ms();
        QCOMPARE(algo->name(), QStringLiteral("Master + Stack"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
        QCOMPARE(algo->defaultSplitRatio(), AutotileDefaults::DefaultSplitRatio);
    }

    void testMasterStack_oneWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = ms()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMasterStack_twoWindows_defaultRatio()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = ms()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = ms()->calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(2);
        state.setSplitRatio(0.6);
        auto zones = ms()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(5);
        auto zones = ms()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QCOMPARE(zone.width(), ScreenWidth);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_metadata()
    {
        auto* algo = bsp();
        QCOMPARE(algo->name(), QStringLiteral("Binary Split"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), -1);
        QCOMPARE(algo->defaultSplitRatio(), 0.5);
    }

    void testMasterStack_zeroWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(ms()->calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
    }

    void testBSP_zeroAndOneWindow()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(bsp()->calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        auto zones = bsp()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testBSP_twoWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = bsp()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = bsp()->calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testBSP_oddWindowCount()
    {
        TilingState state(QStringLiteral("test"));

        auto zones = bsp()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testBSP_manyWindows()
    {
        TilingState state(QStringLiteral("test"));

        auto zones = bsp()->calculateZones({16, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        QRect squareScreen(0, 0, 1000, 1000);
        auto zones = bsp()->calculateZones({4, squareScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, squareScreen));
    }

    void testBSP_deterministic()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones1 = bsp()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto zones2 = bsp()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones1, zones2);
        bsp()->calculateZones({4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto zones3 = bsp()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones1, zones3);
    }

    void testDwindle_metadata()
    {
        auto* algo = dw();
        QCOMPARE(algo->name(), QStringLiteral("Dwindle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), -1);
        QCOMPARE(algo->defaultSplitRatio(), 0.5);
    }

    void testDwindle_twoWindows_dwindleSplit()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = dw()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = dw()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = dw()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);

        auto zones = dw()->calculateZones({12, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 12);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testDwindle_minimumSizeEnforcement()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.618);
        QRect tinyScreen(0, 0, 200, 150);
        auto zones = dw()->calculateZones({20, tinyScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 20);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        QVERIFY(allWithinBounds(zones, tinyScreen));
    }

    void testDwindle_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = dw()->calculateZones({5, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
    }
};

QTEST_MAIN(TestTilingAlgoMasterStack)
#include "test_tiling_algo_masterstack.moc"
