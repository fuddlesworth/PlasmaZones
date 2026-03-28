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
 * @brief Tests for Cascade, Stair, and Spread tiling algorithms (all JS-based via registry)
 *
 * These are overlapping layout algorithms where zones intentionally overlap,
 * so noOverlaps() and zonesFillScreen() are NOT expected to pass.
 */
class TestTilingAlgoCascadeStairSpread : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* cascade()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("cascade"));
    }
    TilingAlgorithm* stair()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("stair"));
    }
    TilingAlgorithm* spread()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("spread"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(cascade() != nullptr);
        QVERIFY(stair() != nullptr);
        QVERIFY(spread() != nullptr);
    }

    // =========================================================================
    // Cascade algorithm tests
    // =========================================================================

    void testCascade_metadata()
    {
        auto* algo = cascade();
        QCOMPARE(algo->name(), QStringLiteral("Cascade"));
        QVERIFY(algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testCascade_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testCascade_multipleWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testCascade_gaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones({3, m_screenGeometry, &state, 10, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testCascade_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones = cascade()->calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY2(zone.x() >= 100, qPrintable(QStringLiteral("Zone x (%1) should be >= 100").arg(zone.x())));
            QVERIFY2(zone.y() >= 200, qPrintable(QStringLiteral("Zone y (%1) should be >= 200").arg(zone.y())));
        }
    }

    // =========================================================================
    // Stair algorithm tests
    // =========================================================================

    void testStair_metadata()
    {
        auto* algo = stair();
        QCOMPARE(algo->name(), QStringLiteral("Stair"));
        QVERIFY(algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testStair_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = stair()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testStair_multipleWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = stair()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testStair_gaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = stair()->calculateZones({3, m_screenGeometry, &state, 10, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testStair_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones = stair()->calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY2(zone.x() >= 100, qPrintable(QStringLiteral("Zone x (%1) should be >= 100").arg(zone.x())));
            QVERIFY2(zone.y() >= 200, qPrintable(QStringLiteral("Zone y (%1) should be >= 200").arg(zone.y())));
        }
    }

    // =========================================================================
    // Spread algorithm tests
    // =========================================================================

    void testSpread_metadata()
    {
        auto* algo = spread();
        QCOMPARE(algo->name(), QStringLiteral("Spread"));
        QVERIFY(algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testSpread_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[0].height() > 0);
    }

    void testSpread_multipleWindows()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testSpread_gaps()
    {
        TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones({3, m_screenGeometry, &state, 10, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testSpread_offsetScreen()
    {
        TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones = spread()->calculateZones({3, offsetScreen, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY2(zone.x() >= 100, qPrintable(QStringLiteral("Zone x (%1) should be >= 100").arg(zone.x())));
            QVERIFY2(zone.y() >= 200, qPrintable(QStringLiteral("Zone y (%1) should be >= 200").arg(zone.y())));
        }
    }
};

QTEST_MAIN(TestTilingAlgoCascadeStairSpread)
#include "test_tiling_algo_cascade_stair_spread.moc"
