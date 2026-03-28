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

class TestTilingAlgoWideCentered : public QObject
{
    Q_OBJECT
private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* wide()
    {
        return AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::Wide);
    }
    TilingAlgorithm* masterStack()
    {
        return AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::MasterStack);
    }
    TilingAlgorithm* dw()
    {
        return AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::Dwindle);
    }
    TilingAlgorithm* threeCol()
    {
        return AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::ThreeColumn);
    }
    TilingAlgorithm* grid()
    {
        return AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::Grid);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(wide() != nullptr);
        QVERIFY(masterStack() != nullptr);
    }

    void testWide_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = wide()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testWide_twoWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones = wide()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(2);
        state.setSplitRatio(0.5);
        auto zones = wide()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[1].y(), 0);
        QCOMPARE(zones[0].width() + zones[1].width(), ScreenWidth);
        QCOMPARE(zones[2].width(), ScreenWidth);
    }

    void testWide_withSplitRatio()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = wide()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].height() > zones[1].height());
    }

    void testWide_allMasters()
    {
        TilingState state(QStringLiteral("test"));
        state.setMasterCount(3);
        auto zones = wide()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        auto* algo = wide();
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->defaultMaxWindows(), 5);
    }

    void testCenteredMaster_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = AlgorithmRegistry::instance()
                         ->algorithm(DBus::AutotileAlgorithm::CenteredMaster)
                         ->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testCenteredMaster_metadata()
    {
        auto* cm = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::CenteredMaster);
        QVERIFY(cm->supportsMasterCount());
        QVERIFY(cm->supportsSplitRatio());
        QCOMPARE(cm->defaultMaxWindows(), 7);
    }

    void testAllAlgorithms_negativeWindowCount()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(masterStack()->calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(dw()->calculateZones({-3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(threeCol()->calculateZones({-2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(grid()->calculateZones({-3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(wide()->calculateZones({-4, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).isEmpty());
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(DBus::AutotileAlgorithm::Columns)
                    ->calculateZones({-5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                    .isEmpty());
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(DBus::AutotileAlgorithm::BSP)
                    ->calculateZones({-10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                    .isEmpty());
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(DBus::AutotileAlgorithm::Monocle)
                    ->calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                    .isEmpty());
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(DBus::AutotileAlgorithm::Rows)
                    ->calculateZones({-7, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                    .isEmpty());
        QVERIFY(AlgorithmRegistry::instance()
                    ->algorithm(DBus::AutotileAlgorithm::CenteredMaster)
                    ->calculateZones({-1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                    .isEmpty());
    }

    void testAllAlgorithms_largeWindowCount()
    {
        TilingState state(QStringLiteral("test"));
        QCOMPARE(masterStack()->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).size(), 50);
        QCOMPARE(dw()->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).size(), 50);
        QCOMPARE(threeCol()->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).size(), 50);
        QCOMPARE(grid()->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).size(), 50);
        QCOMPARE(wide()->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)}).size(), 50);
        QCOMPARE(AlgorithmRegistry::instance()
                     ->algorithm(DBus::AutotileAlgorithm::Columns)
                     ->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                     .size(),
                 50);
        QCOMPARE(AlgorithmRegistry::instance()
                     ->algorithm(DBus::AutotileAlgorithm::BSP)
                     ->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                     .size(),
                 50);
        QCOMPARE(AlgorithmRegistry::instance()
                     ->algorithm(DBus::AutotileAlgorithm::Rows)
                     ->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                     .size(),
                 50);
        QCOMPARE(AlgorithmRegistry::instance()
                     ->algorithm(DBus::AutotileAlgorithm::CenteredMaster)
                     ->calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)})
                     .size(),
                 50);
    }

    void test_splitRatioBoundaryValues()
    {
        QRect screen(0, 0, 1920, 1080);
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);
            auto zones = masterStack()->calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 3);
            for (const auto& z : zones)
                QVERIFY(z.width() > 0 && z.height() > 0);
        }
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);
            auto zones = masterStack()->calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 3);
            for (int i = 1; i < zones.size(); ++i)
                QVERIFY(zones[i].width() > 0 && zones[i].height() > 0);
        }
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.1);
            auto zones = dw()->calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 4);
            for (const auto& z : zones)
                QVERIFY(z.width() > 0 && z.height() > 0);
        }
        {
            TilingState state(QStringLiteral("test"));
            state.setSplitRatio(0.9);
            auto zones = threeCol()->calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 4);
            for (const auto& z : zones)
                QVERIFY(z.width() > 0 && z.height() > 0);
        }
    }

    void test_nullStatePointer()
    {
        // Scripted algorithms use default state values (splitRatio, masterCount) when
        // state is null, so they produce valid zones rather than returning empty.
        QRect screen(0, 0, 1920, 1080);
        QCOMPARE(masterStack()->calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(wide()->calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(dw()->calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(threeCol()->calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
        QCOMPARE(grid()->calculateZones({3, screen, nullptr, 0, EdgeGaps::uniform(0)}).size(), 3);
    }

    void test_innerRectHugeOuterGap()
    {
        QRect screen(0, 0, 100, 100);
        TilingState state(QStringLiteral("test"));
        auto zones = AlgorithmRegistry::instance()
                         ->algorithm(DBus::AutotileAlgorithm::Columns)
                         ->calculateZones({1, screen, &state, 0, EdgeGaps::uniform(500)});
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[0].height() >= 1);
    }
};

QTEST_MAIN(TestTilingAlgoWideCentered)
#include "test_tiling_algo_wide_centered.moc"
