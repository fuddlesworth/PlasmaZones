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

/**
 * @brief Tests for Spiral, Monocle, and Rows tiling algorithms (all JS-based via registry)
 */
class TestTilingAlgoSpiralMonocle : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* spiral()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("spiral"));
    }
    TilingAlgorithm* dwindleAlgo()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle"));
    }
    TilingAlgorithm* monocle()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("monocle"));
    }
    TilingAlgorithm* rows()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("rows"));
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(spiral() != nullptr);
        QVERIFY(dwindleAlgo() != nullptr);
        QVERIFY(monocle() != nullptr);
        QVERIFY(rows() != nullptr);
    }

    // =========================================================================
    // SpiralAlgorithm tests
    // =========================================================================

    void testSpiral_metadata()
    {
        auto* algo = spiral();
        QCOMPARE(algo->name(), QStringLiteral("Spiral"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), -1);
        QCOMPARE(algo->defaultSplitRatio(), 0.5);
    }

    void testSpiral_oneWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = spiral()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testSpiral_twoWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = spiral()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);

        int expectedWidth = static_cast<int>(ScreenWidth * 0.5);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), expectedWidth);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].x(), expectedWidth);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testSpiral_fiveWindows_fourDirectionRotation()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = spiral()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);

        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].width(), ScreenWidth / 2);
        QCOMPARE(zones[0].height(), ScreenHeight);

        QCOMPARE(zones[1].x(), ScreenWidth / 2);
        QCOMPARE(zones[1].height(), ScreenHeight / 2);

        QVERIFY(zones[2].x() > zones[0].x());
        QVERIFY(zones[2].y() >= ScreenHeight / 2);

        QVERIFY(zones[3].y() > zones[1].y());

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testSpiral_differenceFromDwindle()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto spiralZones = spiral()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto dwindleZones = dwindleAlgo()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        QCOMPARE(spiralZones.size(), 5);
        QCOMPARE(dwindleZones.size(), 5);

        QCOMPARE(spiralZones[0], dwindleZones[0]);
        QCOMPARE(spiralZones[1], dwindleZones[1]);

        QVERIFY(spiralZones[2] != dwindleZones[2]);
    }

    void testSpiral_manyWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = spiral()->calculateZones({12, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 12);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // MonocleAlgorithm tests (JS-based via registry)
    // =========================================================================

    void testMonocle_metadata()
    {
        auto* algo = monocle();
        QCOMPARE(algo->name(), QStringLiteral("Monocle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), -1);
    }

    void testMonocle_oneWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = monocle()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMonocle_twoWindows_allFullScreen()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = monocle()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones[0], m_screenGeometry);
        QCOMPARE(zones[1], m_screenGeometry);
    }

    void testMonocle_manyWindows_allIdentical()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = monocle()->calculateZones({10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 10);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], m_screenGeometry);
        }
    }

    void testMonocle_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(200, 100, 1920, 1080);
        auto zones = monocle()->calculateZones({5, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        for (const QRect& zone : zones) {
            QCOMPARE(zone, offsetScreen);
        }
    }

    // =========================================================================
    // RowsAlgorithm tests (JS-based via registry)
    // =========================================================================

    void testRows_metadata()
    {
        auto* algo = rows();
        QCOMPARE(algo->name(), QStringLiteral("Rows"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), -1);
    }

    void testRows_oneWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testRows_twoWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        int currentY = 0;
        for (const QRect& zone : zones) {
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
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({7, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 7);
        int totalHeight = 0;
        for (const QRect& zone : zones) {
            totalHeight += zone.height();
            QCOMPARE(zone.width(), ScreenWidth);
            QVERIFY(zone.height() == 154 || zone.height() == 155);
        }
        QCOMPARE(totalHeight, ScreenHeight);
        QCOMPARE(zones[0].height(), 155);
        QCOMPARE(zones[1].height(), 155);
        QCOMPARE(zones[2].height(), 154);
    }

    void testRows_contiguousRows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);
        int currentY = 0;
        for (const QRect& zone : zones) {
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
        TilingState state(QStringLiteral("test"));
        auto zones = rows()->calculateZones({10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 10);
        int currentY = 0;
        for (const QRect& zone : zones) {
            QCOMPARE(zone.y(), currentY);
            QCOMPARE(zone.width(), ScreenWidth);
            currentY += zone.height();
        }
        QCOMPARE(currentY, ScreenHeight);
        QVERIFY(noOverlaps(zones));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testRows_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = rows()->calculateZones({4, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));
        QCOMPARE(zones[0].x(), 100);
        QCOMPARE(zones[0].y(), 50);
    }
};

QTEST_MAIN(TestTilingAlgoSpiralMonocle)
#include "test_tiling_algo_spiral_monocle.moc"
