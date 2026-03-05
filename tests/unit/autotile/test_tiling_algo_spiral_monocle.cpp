// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/SpiralAlgorithm.h"
#include "autotile/algorithms/DwindleAlgorithm.h"
#include "autotile/algorithms/MonocleAlgorithm.h"
#include "autotile/algorithms/RowsAlgorithm.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Tests for Spiral, Monocle, and Rows tiling algorithms
 */
class TestTilingAlgoSpiralMonocle : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};

private Q_SLOTS:

    // =========================================================================
    // SpiralAlgorithm tests
    // =========================================================================

    void testSpiral_metadata()
    {
        SpiralAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Spiral"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
        QCOMPARE(algo.defaultSplitRatio(), 0.5);
    }

    void testSpiral_zeroWindows()
    {
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testSpiral_oneWindow()
    {
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testSpiral_twoWindows()
    {
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        SpiralAlgorithm spiral;
        DwindleAlgorithm dwindle;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto spiralZones = spiral.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        auto dwindleZones = dwindle.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        QCOMPARE(spiralZones.size(), 5);
        QCOMPARE(dwindleZones.size(), 5);

        QCOMPARE(spiralZones[0], dwindleZones[0]);
        QCOMPARE(spiralZones[1], dwindleZones[1]);

        QVERIFY(spiralZones[2] != dwindleZones[2]);
    }

    void testSpiral_manyWindows()
    {
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);

        auto zones = algo.calculateZones({12, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 12);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testSpiral_invalidGeometry()
    {
        SpiralAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    // =========================================================================
    // MonocleAlgorithm tests
    // =========================================================================

    void testMonocle_metadata()
    {
        MonocleAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Monocle"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
    }

    void testMonocle_zeroWindows()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_oneWindow()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testMonocle_twoWindows_allFullScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 2);

        QCOMPARE(zones[0], m_screenGeometry);
        QCOMPARE(zones[1], m_screenGeometry);
    }

    void testMonocle_manyWindows_allIdentical()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 10);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], m_screenGeometry);
        }
    }

    void testMonocle_fiftyWindows()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({50, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 50);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, m_screenGeometry);
        }
    }

    void testMonocle_invalidGeometry()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testMonocle_offsetScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(200, 100, 1920, 1080);
        auto zones = algo.calculateZones({5, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 5);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, offsetScreen);
        }
    }

    void testMonocle_smallScreen()
    {
        MonocleAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect smallScreen(0, 0, 200, 150);
        auto zones = algo.calculateZones({8, smallScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 8);

        for (const QRect& zone : zones) {
            QCOMPARE(zone, smallScreen);
        }
    }

    // =========================================================================
    // RowsAlgorithm tests
    // =========================================================================

    void testRows_metadata()
    {
        RowsAlgorithm algo;
        QCOMPARE(algo.name(), QStringLiteral("Rows"));
        QVERIFY(!algo.icon().isEmpty());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.masterZoneIndex(), -1);
    }

    void testRows_zeroWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testRows_oneWindow()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testRows_twoWindows()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({7, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        auto zones = algo.calculateZones({10, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
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

    void testRows_invalidGeometry()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect invalidRect;
        auto zones = algo.calculateZones({3, invalidRect, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testRows_offsetScreen()
    {
        RowsAlgorithm algo;
        TilingState state(QStringLiteral("test"));

        QRect offsetScreen(100, 50, 1920, 1080);
        auto zones = algo.calculateZones({4, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, offsetScreen));
        QVERIFY(noOverlaps(zones));

        QCOMPARE(zones[0].x(), 100);
        QCOMPARE(zones[0].y(), 50);
    }
};

QTEST_MAIN(TestTilingAlgoSpiralMonocle)
#include "test_tiling_algo_spiral_monocle.moc"
