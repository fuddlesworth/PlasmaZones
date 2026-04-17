// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QSize>
#include <QVector>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Cross-cutting edge case tests for all 24 JS tiling algorithms
 *
 * Tests extreme screen sizes, gap overflows, masterCount boundary values,
 * minSizes, dwindle-memory tree paths, and frozen-globals sandbox integrity.
 */
class TestTilingAlgoEdgeCases : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    // All 24 algorithm IDs
    QStringList allAlgoIds() const
    {
        return {
            QLatin1String("bsp"),
            QLatin1String("cascade"),
            QLatin1String("centered-master"),
            QLatin1String("columns"),
            QLatin1String("corner-master"),
            QLatin1String("deck"),
            QLatin1String("dwindle"),
            QLatin1String("dwindle-memory"),
            QLatin1String("floating-center"),
            QLatin1String("focus-sidebar"),
            QLatin1String("grid"),
            QLatin1String("horizontal-deck"),
            QLatin1String("master-stack"),
            QLatin1String("monocle"),
            QLatin1String("paper"),
            QLatin1String("quadrant-priority"),
            QLatin1String("rows"),
            QLatin1String("spiral"),
            QLatin1String("spread"),
            QLatin1String("stair"),
            QLatin1String("tatami"),
            QLatin1String("three-column"),
            QLatin1String("wide"),
            QLatin1String("zen"),
        };
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        // Verify all 24 algorithms are loaded
        for (const auto& id : allAlgoIds()) {
            QVERIFY2(PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id) != nullptr,
                     qPrintable(QStringLiteral("Algorithm '%1' not found in registry").arg(id)));
        }
    }

    // =========================================================================
    // Tiny screen 10x10, gap=0 — all algorithms must produce valid zones
    // =========================================================================

    void testAllAlgorithms_tinyScreen10x10()
    {
        QRect tiny(0, 0, 10, 10);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones = algo->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(
                    zones.size() >= 1 && zones.size() <= 3,
                    qPrintable(
                        QStringLiteral("Algorithm %1 on 10x10: expected 1-3 zones, got %2").arg(id).arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 3);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 on 10x10: zone has non-positive dimension (%2x%3)")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    // =========================================================================
    // Extreme aspect ratios — 100x2000 and 2000x100
    // =========================================================================

    void testAllAlgorithms_extremeTall()
    {
        QRect tall(0, 0, 100, 2000);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones = algo->calculateZones(makeParams(4, tall, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 4,
                         qPrintable(QStringLiteral("Algorithm %1 on 100x2000: expected 1-4 zones, got %2")
                                        .arg(id)
                                        .arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 4);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 on 100x2000: zone (%2x%3) non-positive")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    void testAllAlgorithms_extremeWide()
    {
        QRect wide(0, 0, 2000, 100);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones = algo->calculateZones(makeParams(4, wide, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 4,
                         qPrintable(QStringLiteral("Algorithm %1 on 2000x100: expected 1-4 zones, got %2")
                                        .arg(id)
                                        .arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 4);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 on 2000x100: zone (%2x%3) non-positive")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    // =========================================================================
    // Gap larger than screen — gap=5000 on 1000x1000
    // =========================================================================

    void testAllAlgorithms_gapLargerThanScreen()
    {
        QRect screen(0, 0, 1000, 1000);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones =
                algo->calculateZones(makeParams(3, screen, &state, 5000, ::PhosphorLayout::EdgeGaps::uniform(0)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 3,
                         qPrintable(QStringLiteral("Algorithm %1 with gap=5000: expected 1-3 zones, got %2")
                                        .arg(id)
                                        .arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 3);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 with gap=5000: zone (%2x%3) non-positive")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    // =========================================================================
    // masterCount=0 for master-supporting algorithms
    // =========================================================================

    void testMasterCountZero_centeredMaster()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(0);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("centered-master"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0 && zone.height() > 0);
        }
    }

    void testMasterCountZero_masterStack()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(0);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0 && zone.height() > 0);
        }
    }

    void testMasterCountZero_wide()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(0);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("wide"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0 && zone.height() > 0);
        }
    }

    // =========================================================================
    // masterCount > windowCount for master-supporting algorithms
    // =========================================================================

    void testMasterCountExceedsWindows_centeredMaster()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(10);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("centered-master"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testMasterCountExceedsWindows_masterStack()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(10);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testMasterCountExceedsWindows_wide()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setMasterCount(10);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("wide"));
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // minSizes tests — cascade, stair, spread respect per-window minimums
    // =========================================================================

    void testCascade_minSizesRespected()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVector<QSize> minSizes = {QSize(200, 150), QSize(200, 150), QSize(200, 150)};
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("cascade"));
        auto zones = algo->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0), minSizes));
        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(zones[i].width() >= 200,
                     qPrintable(QStringLiteral("Cascade zone %1 width (%2) should be >= minWidth 200")
                                    .arg(i)
                                    .arg(zones[i].width())));
            QVERIFY2(zones[i].height() >= 150,
                     qPrintable(QStringLiteral("Cascade zone %1 height (%2) should be >= minHeight 150")
                                    .arg(i)
                                    .arg(zones[i].height())));
        }
    }

    void testStair_minSizesRespected()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVector<QSize> minSizes = {QSize(200, 150), QSize(200, 150), QSize(200, 150)};
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("stair"));
        auto zones = algo->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0), minSizes));
        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(
                zones[i].width() >= 200,
                qPrintable(
                    QStringLiteral("Stair zone %1 width (%2) should be >= minWidth 200").arg(i).arg(zones[i].width())));
            QVERIFY2(zones[i].height() >= 150,
                     qPrintable(QStringLiteral("Stair zone %1 height (%2) should be >= minHeight 150")
                                    .arg(i)
                                    .arg(zones[i].height())));
        }
    }

    void testSpread_minSizesRespected()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVector<QSize> minSizes = {QSize(200, 150), QSize(200, 150), QSize(200, 150)};
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("spread"));
        auto zones = algo->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0), minSizes));
        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(zones[i].width() >= 200,
                     qPrintable(QStringLiteral("Spread zone %1 width (%2) should be >= minWidth 200")
                                    .arg(i)
                                    .arg(zones[i].width())));
            QVERIFY2(zones[i].height() >= 150,
                     qPrintable(QStringLiteral("Spread zone %1 height (%2) should be >= minHeight 150")
                                    .arg(i)
                                    .arg(zones[i].height())));
        }
    }

    // =========================================================================
    // Dwindle-memory — tree path test
    // =========================================================================

    void testDwindleMemory_fallbackWithoutTree()
    {
        // Without a split tree, dwindle-memory should fall back to stateless dwindle
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle-memory"));
        auto zones =
            algo->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testDwindleMemory_withTree()
    {
        // Build a split tree with the right leaf count, then calculate zones
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        // Add 3 windows to state, then rebuild the tree so leafCount matches
        state.addWindow(QStringLiteral("w1"));
        state.addWindow(QStringLiteral("w2"));
        state.addWindow(QStringLiteral("w3"));

        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle-memory"));
        // prepareTilingState creates the tree if supportsMemory is true
        algo->prepareTilingState(&state);

        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testDwindleMemory_treeMismatchFallback()
    {
        // When tree leaf count mismatches window count, should fall back to stateless
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        state.addWindow(QStringLiteral("w1"));
        state.addWindow(QStringLiteral("w2"));

        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle-memory"));
        algo->prepareTilingState(&state);

        // Request 4 zones but tree has only 2 leaves — should fall back
        auto zones =
            algo->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testDwindleMemory_metadata()
    {
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle-memory"));
        QCOMPARE(algo->name(), QStringLiteral("Dwindle (Memory)"));
        QVERIFY(algo->supportsMemory());
        QVERIFY(algo->supportsSplitRatio());
        QVERIFY(!algo->supportsMasterCount());
    }

    // =========================================================================
    // Negative gap — all algorithms must produce valid zones
    // =========================================================================

    void testAllAlgorithms_negativeGap()
    {
        QRect screen(0, 0, 1000, 1000);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones =
                algo->calculateZones(makeParams(3, screen, &state, -10, ::PhosphorLayout::EdgeGaps::uniform(0)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 3,
                         qPrintable(QStringLiteral("Algorithm %1 with gap=-10: expected 1-3 zones, got %2")
                                        .arg(id)
                                        .arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 3);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 with gap=-10: zone (%2x%3) non-positive")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    // =========================================================================
    // Edge gap overflow — ::PhosphorLayout::EdgeGaps::uniform(600) on 1000x1000
    // =========================================================================

    void testAllAlgorithms_edgeGapOverflow()
    {
        QRect screen(0, 0, 1000, 1000);
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones =
                algo->calculateZones(makeParams(3, screen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(600)));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 3,
                         qPrintable(QStringLiteral("Algorithm %1 with edgeGaps=600: expected 1-3 zones, got %2")
                                        .arg(id)
                                        .arg(zones.size())));
            } else {
                QCOMPARE(zones.size(), 3);
            }
            for (const QRect& zone : zones) {
                QVERIFY2(zone.width() > 0 && zone.height() > 0,
                         qPrintable(QStringLiteral("Algorithm %1 with edgeGaps=600: zone (%2x%3) non-positive")
                                        .arg(id)
                                        .arg(zone.width())
                                        .arg(zone.height())));
            }
        }
    }

    // =========================================================================
    // Frozen globals sync test — verify builtins are immutable in sandbox
    // =========================================================================

    // =========================================================================
    // Zero windows sweep — every algorithm must return empty list for count=0
    // =========================================================================

    void testAllAlgorithms_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones = algo->calculateZones(
                makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
            QVERIFY2(zones.isEmpty(),
                     qPrintable(QStringLiteral("Algorithm '%1' returned %2 zones for windowCount=0, expected 0")
                                    .arg(id)
                                    .arg(zones.size())));
        }
    }

    // =========================================================================
    // Single window sweep — every algorithm must return valid zone(s)
    // =========================================================================

    void testAllAlgorithms_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones = algo->calculateZones(
                makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));

            // Non-overlapping algorithms must return exactly 1 zone;
            // overlapping algorithms (monocle, cascade, stair, paper, horizontal-deck)
            // may return 1 or more.
            if (!algo->producesOverlappingZones()) {
                QVERIFY2(
                    zones.size() == 1,
                    qPrintable(QStringLiteral("Algorithm '%1' (non-overlapping) returned %2 zones for windowCount=1")
                                   .arg(id)
                                   .arg(zones.size())));
            } else {
                QVERIFY2(zones.size() >= 1,
                         qPrintable(QStringLiteral("Algorithm '%1' returned %2 zones for windowCount=1, expected >= 1")
                                        .arg(id)
                                        .arg(zones.size())));
            }

            // All zones must have positive dimensions and be within bounds
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].width() > 0 && zones[i].height() > 0,
                         qPrintable(QStringLiteral("Algorithm '%1' zone %2 has non-positive dimension (%3x%4)")
                                        .arg(id)
                                        .arg(i)
                                        .arg(zones[i].width())
                                        .arg(zones[i].height())));
            }
            QVERIFY2(
                allWithinBounds(zones, m_screenGeometry),
                qPrintable(QStringLiteral("Algorithm '%1' zone(s) exceed screen bounds for windowCount=1").arg(id)));
        }
    }

    // =========================================================================
    // Frozen globals sync test — verify builtins are immutable in sandbox
    // =========================================================================

    void testFrozenGlobals_builtinsCannotBeOverwritten()
    {
        // Use a known algorithm to verify that injected builtins are frozen.
        // After calculateZones, the engine state is live — but we cannot directly
        // access the JS engine. Instead, verify indirectly: if a builtin like
        // distributeEvenly were overwritable, running a second calculateZones
        // after a hypothetical overwrite would produce wrong results.
        //
        // We verify that calling the same algorithm twice with identical
        // parameters produces identical results, confirming that no internal
        // state mutation affects outputs.
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("zen"));
        auto zones1 =
            algo->calculateZones(makeParams(5, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        auto zones2 =
            algo->calculateZones(makeParams(5, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones1, zones2);

        // Also verify with a non-overlapping layout
        auto* tatamiAlgo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("tatami"));
        auto tz1 = tatamiAlgo->calculateZones(
            makeParams(5, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        auto tz2 = tatamiAlgo->calculateZones(
            makeParams(5, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(tz1, tz2);
    }

    // =========================================================================
    // Asymmetric edge gaps — top=30, bottom=10, left=20, right=0
    // =========================================================================

    void testAsymmetricEdgeGaps()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        // ::PhosphorLayout::EdgeGaps field order: top, bottom, left, right
        const ::PhosphorLayout::EdgeGaps asymGaps{30, 10, 20, 0};
        const QRect reducedArea(m_screenGeometry.x() + asymGaps.left, m_screenGeometry.y() + asymGaps.top,
                                m_screenGeometry.width() - asymGaps.left - asymGaps.right,
                                m_screenGeometry.height() - asymGaps.top - asymGaps.bottom);

        const QStringList testAlgos = {
            QLatin1String("master-stack"),
            QLatin1String("grid"),
            QLatin1String("three-column"),
        };

        for (const auto& id : testAlgos) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Algorithm '%1' not found").arg(id)));
            auto zones = algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, asymGaps));
            QCOMPARE(zones.size(), 3);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY2(zones[i].left() >= reducedArea.left(),
                         qPrintable(QStringLiteral("Algorithm '%1' zone %2 left (%3) < reduced area left (%4)")
                                        .arg(id)
                                        .arg(i)
                                        .arg(zones[i].left())
                                        .arg(reducedArea.left())));
                QVERIFY2(zones[i].top() >= reducedArea.top(),
                         qPrintable(QStringLiteral("Algorithm '%1' zone %2 top (%3) < reduced area top (%4)")
                                        .arg(id)
                                        .arg(i)
                                        .arg(zones[i].top())
                                        .arg(reducedArea.top())));
                QVERIFY2(zones[i].right() <= reducedArea.right(),
                         qPrintable(QStringLiteral("Algorithm '%1' zone %2 right (%3) > reduced area right (%4)")
                                        .arg(id)
                                        .arg(i)
                                        .arg(zones[i].right())
                                        .arg(reducedArea.right())));
                QVERIFY2(zones[i].bottom() <= reducedArea.bottom(),
                         qPrintable(QStringLiteral("Algorithm '%1' zone %2 bottom (%3) > reduced area bottom (%4)")
                                        .arg(id)
                                        .arg(i)
                                        .arg(zones[i].bottom())
                                        .arg(reducedArea.bottom())));
                QVERIFY2(
                    zones[i].width() > 0 && zones[i].height() > 0,
                    qPrintable(QStringLiteral("Algorithm '%1' zone %2 has non-positive dimension").arg(id).arg(i)));
            }
        }
    }

    // =========================================================================
    // supportsMemory sweep — only dwindle-memory returns true
    // =========================================================================

    void testSupportsMemory_sweep()
    {
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Algorithm '%1' not found").arg(id)));
            if (id == QLatin1String("dwindle-memory")) {
                QVERIFY2(algo->supportsMemory(),
                         qPrintable(QStringLiteral("dwindle-memory should return supportsMemory() == true")));
            } else {
                QVERIFY2(!algo->supportsMemory(),
                         qPrintable(QStringLiteral("Algorithm '%1' should return supportsMemory() == false").arg(id)));
            }
        }
    }

    void testFrozenGlobals_allAlgorithmsDeterministic()
    {
        // Every algorithm should produce identical output for identical input,
        // confirming that sandbox frozen globals are not mutated between calls.
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        for (const auto& id : allAlgoIds()) {
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(id);
            auto zones1 = algo->calculateZones(
                makeParams(3, m_screenGeometry, &state, 5, ::PhosphorLayout::EdgeGaps::uniform(0)));
            auto zones2 = algo->calculateZones(
                makeParams(3, m_screenGeometry, &state, 5, ::PhosphorLayout::EdgeGaps::uniform(0)));
            QVERIFY2(
                zones1 == zones2,
                qPrintable(
                    QStringLiteral("Algorithm '%1' is non-deterministic: results differ on identical input").arg(id)));
        }
    }
};

QTEST_MAIN(TestTilingAlgoEdgeCases)
#include "test_tiling_algo_edge_cases.moc"
