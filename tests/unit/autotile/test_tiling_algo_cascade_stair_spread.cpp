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

    PhosphorTiles::TilingAlgorithm* cascade()
    {
        return m_scriptSetup.registry()->algorithm(QLatin1String("cascade"));
    }
    PhosphorTiles::TilingAlgorithm* stair()
    {
        return m_scriptSetup.registry()->algorithm(QLatin1String("stair"));
    }
    PhosphorTiles::TilingAlgorithm* spread()
    {
        return m_scriptSetup.registry()->algorithm(QLatin1String("spread"));
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
    // Zero window count tests
    // =========================================================================

    void testCascade_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(cascade()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testStair_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(stair()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testSpread_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(spread()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testCascade_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        // Cascade should step diagonally: each successive zone offset right and down
        QVERIFY2(zones[1].x() > zones[0].x(),
                 qPrintable(QStringLiteral("Cascade zone[1].x (%1) should be > zone[0].x (%2)")
                                .arg(zones[1].x())
                                .arg(zones[0].x())));
        QVERIFY2(zones[1].y() > zones[0].y(),
                 qPrintable(QStringLiteral("Cascade zone[1].y (%1) should be > zone[0].y (%2)")
                                .arg(zones[1].y())
                                .arg(zones[0].y())));
    }

    void testCascade_gaps()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testCascade_offsetScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones =
            cascade()->calculateZones(makeParams(3, offsetScreen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            stair()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testStair_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            stair()->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        // Stair should step in the x direction: each successive zone shifts horizontally
        QVERIFY2(zones[1].x() != zones[0].x(),
                 qPrintable(QStringLiteral("Stair zone[1].x (%1) should differ from zone[0].x (%2)")
                                .arg(zones[1].x())
                                .arg(zones[0].x())));
    }

    void testStair_gaps()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = stair()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testStair_offsetScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones =
            stair()->calculateZones(makeParams(3, offsetScreen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[0].height() > 0);
    }

    void testSpread_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        // Spread should distribute zones across the area width
        int minX = zones[0].x();
        int maxRight = zones[0].right();
        for (int i = 1; i < zones.size(); ++i) {
            minX = qMin(minX, zones[i].x());
            maxRight = qMax(maxRight, zones[i].right());
        }
        int totalSpan = maxRight - minX;
        QVERIFY2(totalSpan > ScreenWidth / 2,
                 qPrintable(QStringLiteral("Spread zones should span a significant portion of the screen width, got %1")
                                .arg(totalSpan)));
    }

    void testSpread_gaps()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testSpread_offsetScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QRect offsetScreen(100, 200, 1920, 1080);
        auto zones =
            spread()->calculateZones(makeParams(3, offsetScreen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY2(zone.x() >= 100, qPrintable(QStringLiteral("Zone x (%1) should be >= 100").arg(zone.x())));
            QVERIFY2(zone.y() >= 200, qPrintable(QStringLiteral("Zone y (%1) should be >= 200").arg(zone.y())));
        }
    }
    // =========================================================================
    // Large window count tests
    // =========================================================================

    void testCascade_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = cascade()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testStair_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = stair()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testSpread_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = spread()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }
};

QTEST_MAIN(TestTilingAlgoCascadeStairSpread)
#include "test_tiling_algo_cascade_stair_spread.moc"
