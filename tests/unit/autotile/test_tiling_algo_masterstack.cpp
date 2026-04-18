// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/configdefaults.h"

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

    PhosphorTiles::TilingAlgorithm* ms()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(ms() != nullptr);
    }

    void testPixelPerfect_masterStackHeightDistribution()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(1);
        auto zones =
            ms()->calculateZones(makeParams(8, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        QCOMPARE(algo->defaultSplitRatio(), 0.6); // master-stack declares @defaultSplitRatio 0.6
    }

    void testMasterStack_oneWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            ms()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMasterStack_twoWindows_defaultRatio()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones =
            ms()->calculateZones(makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto zones =
            ms()->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(2);
        state.setSplitRatio(0.6);
        auto zones =
            ms()->calculateZones(makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("window%1").arg(i));
        }
        state.setMasterCount(5);
        auto zones =
            ms()->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QCOMPARE(zone.width(), ScreenWidth);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testMasterStack_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(ms()->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }
};

QTEST_MAIN(TestTilingAlgoMasterStack)
#include "test_tiling_algo_masterstack.moc"
