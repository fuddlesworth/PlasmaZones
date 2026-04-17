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
 * @brief Tests for Corner Master and Quadrant Priority tiling algorithms
 *
 * Both are L-shaped layouts: master in top-left corner, remaining windows
 * fill the right column and bottom row.
 */
class TestTilingAlgoLShape : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    PhosphorTiles::TilingAlgorithm* cornerMaster()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("corner-master"));
    }
    PhosphorTiles::TilingAlgorithm* quadrantPriority()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("quadrant-priority"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(cornerMaster() != nullptr);
        QVERIFY(quadrantPriority() != nullptr);
    }

    // =========================================================================
    // Corner Master — zero and single window
    // =========================================================================

    void testCornerMaster_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(cornerMaster()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testCornerMaster_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = cornerMaster()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    // =========================================================================
    // Corner Master — multi-window layouts
    // =========================================================================

    void testCornerMaster_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Master (index 0) should be in top-left
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].y(), 0);
        // Master should be smaller than full screen
        QVERIFY(zones[0].width() < ScreenWidth);
    }

    void testCornerMaster_threeWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Master in top-left corner
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].y(), 0);
        // Right column window: x > master width
        QVERIFY2(zones[1].x() >= zones[0].width(),
                 qPrintable(QStringLiteral("Right column zone x (%1) should be >= master width (%2)")
                                .arg(zones[1].x())
                                .arg(zones[0].width())));
        // Bottom row window: y > master height
        QVERIFY2(zones[2].y() >= zones[0].height(),
                 qPrintable(QStringLiteral("Bottom row zone y (%1) should be >= master height (%2)")
                                .arg(zones[2].y())
                                .arg(zones[0].height())));
    }

    void testCornerMaster_fourWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testCornerMaster_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Corner Master — master is in top-left
    // =========================================================================

    void testCornerMaster_masterTopLeft()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].y(), 0);
    }

    // =========================================================================
    // Corner Master — splitRatio affects master size
    // =========================================================================

    void testCornerMaster_splitRatioAffectsMasterSize()
    {
        PhosphorTiles::TilingState smallRatioState(QStringLiteral("test"));
        smallRatioState.setSplitRatio(0.3);
        auto smallZones = cornerMaster()->calculateZones(
            makeParams(3, m_screenGeometry, &smallRatioState, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));

        PhosphorTiles::TilingState largeRatioState(QStringLiteral("test"));
        largeRatioState.setSplitRatio(0.7);
        auto largeZones = cornerMaster()->calculateZones(
            makeParams(3, m_screenGeometry, &largeRatioState, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));

        // Larger splitRatio should produce a wider and taller master
        QVERIFY2(largeZones[0].width() > smallZones[0].width(),
                 qPrintable(QStringLiteral("Large ratio master width (%1) should be > small ratio (%2)")
                                .arg(largeZones[0].width())
                                .arg(smallZones[0].width())));
        QVERIFY2(largeZones[0].height() > smallZones[0].height(),
                 qPrintable(QStringLiteral("Large ratio master height (%1) should be > small ratio (%2)")
                                .arg(largeZones[0].height())
                                .arg(smallZones[0].height())));
    }

    // =========================================================================
    // Corner Master — gaps
    // =========================================================================

    void testCornerMaster_gapsNoOverlap()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        auto zones = cornerMaster()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Corner Master — tiny screen
    // =========================================================================

    void testCornerMaster_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.55);
        QRect tiny(0, 0, 50, 50);
        auto zones =
            cornerMaster()->calculateZones(makeParams(3, tiny, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    // =========================================================================
    // Corner Master — metadata
    // =========================================================================

    void testCornerMaster_metadata()
    {
        auto* algo = cornerMaster();
        QCOMPARE(algo->name(), QStringLiteral("Corner Master"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
    }

    // =========================================================================
    // Quadrant Priority — zero and single window
    // =========================================================================

    void testQuadrantPriority_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(quadrantPriority()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testQuadrantPriority_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = quadrantPriority()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    // =========================================================================
    // Quadrant Priority — multi-window layouts
    // =========================================================================

    void testQuadrantPriority_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = quadrantPriority()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Master in top-left
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].y(), 0);
    }

    void testQuadrantPriority_threeWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = quadrantPriority()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testQuadrantPriority_fourWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = quadrantPriority()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testQuadrantPriority_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = quadrantPriority()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Quadrant Priority — splitRatio affects master size
    // =========================================================================

    void testQuadrantPriority_splitRatioAffectsMasterSize()
    {
        PhosphorTiles::TilingState smallState(QStringLiteral("test"));
        smallState.setSplitRatio(0.3);
        auto smallZones = quadrantPriority()->calculateZones(
            makeParams(3, m_screenGeometry, &smallState, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));

        PhosphorTiles::TilingState largeState(QStringLiteral("test"));
        largeState.setSplitRatio(0.7);
        auto largeZones = quadrantPriority()->calculateZones(
            makeParams(3, m_screenGeometry, &largeState, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));

        QVERIFY2(largeZones[0].width() > smallZones[0].width(),
                 qPrintable(QStringLiteral("Large ratio master width (%1) should be > small ratio (%2)")
                                .arg(largeZones[0].width())
                                .arg(smallZones[0].width())));
    }

    // =========================================================================
    // Quadrant Priority — gaps
    // =========================================================================

    void testQuadrantPriority_gapsNoOverlap()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones = quadrantPriority()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Quadrant Priority — tiny screen
    // =========================================================================

    void testQuadrantPriority_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        QRect tiny(0, 0, 50, 50);
        auto zones =
            quadrantPriority()->calculateZones(makeParams(3, tiny, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    // =========================================================================
    // Quadrant Priority — metadata
    // =========================================================================

    void testQuadrantPriority_metadata()
    {
        auto* algo = quadrantPriority();
        QCOMPARE(algo->name(), QStringLiteral("Quadrant Priority"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
    }
};

QTEST_MAIN(TestTilingAlgoLShape)
#include "test_tiling_algo_lshape.moc"
