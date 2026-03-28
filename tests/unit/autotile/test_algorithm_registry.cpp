// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QSignalSpy>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for AlgorithmRegistry: singleton, built-in algorithms,
 *        retrieval, and default algorithm behavior.
 */
class TestAlgorithmRegistry : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // Singleton tests
    // =========================================================================

    void testSingleton_sameInstance()
    {
        auto* instance1 = AlgorithmRegistry::instance();
        auto* instance2 = AlgorithmRegistry::instance();

        QVERIFY(instance1 != nullptr);
        QCOMPARE(instance1, instance2);
    }

    // =========================================================================
    // Built-in algorithm tests
    // =========================================================================

    void testBuiltIn_masterStackRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("master-stack"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Master + Stack"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_columnsRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("columns"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Columns"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_bspRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("bsp"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Binary Split"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_rowsRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("rows"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Rows"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_dwindleRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("dwindle"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Dwindle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_spiralRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("spiral"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Spiral"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_monocleRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("monocle"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Monocle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_threeColumnRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("three-column"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Three Column"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_gridRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("grid"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Grid"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_wideRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("wide"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Wide"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_centeredMasterRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QLatin1String("centered-master"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Centered Master"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_allRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();

        QVERIFY(available.contains(QLatin1String("master-stack")));
        QVERIFY(available.contains(QLatin1String("wide")));
        QVERIFY(available.contains(QLatin1String("columns")));
        QVERIFY(available.contains(QLatin1String("bsp")));
        QVERIFY(available.contains(QLatin1String("rows")));
        QVERIFY(available.contains(QLatin1String("dwindle")));
        QVERIFY(available.contains(QLatin1String("spiral")));
        QVERIFY(available.contains(QLatin1String("monocle")));
        QVERIFY(available.contains(QLatin1String("three-column")));
        QVERIFY(available.contains(QLatin1String("grid")));
        QVERIFY(available.contains(QLatin1String("centered-master")));
        QVERIFY(available.contains(QLatin1String("cascade")));
        QVERIFY(available.contains(QLatin1String("stair")));
        QVERIFY(available.contains(QLatin1String("spread")));
        QVERIFY(available.contains(QLatin1String("dwindle-memory")));
        // At least 15 built-in algorithms are registered before any scripted loader runs
        QVERIFY(available.size() >= 15);
    }

    // =========================================================================
    // Backwards compatibility & sandbox tests
    // =========================================================================

    void testBuiltIn_builtinIdBackwardsCompatibility()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QStringList builtinIds = {
            // 15 original C++-to-JS converted algorithms
            QLatin1String("bsp"),
            QLatin1String("cascade"),
            QLatin1String("centered-master"),
            QLatin1String("columns"),
            QLatin1String("dwindle"),
            QLatin1String("dwindle-memory"),
            QLatin1String("grid"),
            QLatin1String("master-stack"),
            QLatin1String("monocle"),
            QLatin1String("rows"),
            QLatin1String("spiral"),
            QLatin1String("spread"),
            QLatin1String("stair"),
            QLatin1String("three-column"),
            QLatin1String("wide"),
            // 9 JS-native algorithms (shipped with PR #256)
            QLatin1String("corner-master"),
            QLatin1String("quadrant-priority"),
            QLatin1String("deck"),
            QLatin1String("horizontal-deck"),
            QLatin1String("zen"),
            QLatin1String("focus-sidebar"),
            QLatin1String("floating-center"),
            QLatin1String("paper"),
            QLatin1String("tatami"),
        };
        for (const auto& id : builtinIds) {
            QVERIFY2(registry->hasAlgorithm(id), qPrintable(QStringLiteral("Missing algorithm: ") + id));
        }
    }

    void testBuiltIn_allAlgorithmsCalculateZones()
    {
        auto* registry = AlgorithmRegistry::instance();
        TilingState state(QStringLiteral("test"));
        const QRect screen(0, 0, 1920, 1080);
        const auto allIds = registry->availableAlgorithms();
        QVERIFY2(allIds.size() >= 15,
                 qPrintable(QStringLiteral("Expected at least 15 algorithms, got %1").arg(allIds.size())));
        for (const auto& id : allIds) {
            auto* algo = registry->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Null algorithm: ") + id));
            auto zones = algo->calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QVERIFY2(!zones.isEmpty(), qPrintable(QStringLiteral("No zones from: ") + id));
            if (algo->producesOverlappingZones()) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= 3,
                         qPrintable(QStringLiteral("Expected 1-3 zones from overlapping algo: ") + id
                                    + QStringLiteral(", got: ") + QString::number(zones.size())));
            } else {
                QCOMPARE(zones.size(), 3);
            }
        }
    }

    // =========================================================================
    // Default algorithm tests
    // =========================================================================

    void testDefault_algorithmId()
    {
        QCOMPARE(AlgorithmRegistry::defaultAlgorithmId(), QLatin1String("bsp"));
    }

    void testDefault_algorithmInstance()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* defaultAlgo = registry->defaultAlgorithm();
        auto* bsp = registry->algorithm(QLatin1String("bsp"));

        QVERIFY(defaultAlgo != nullptr);
        QCOMPARE(defaultAlgo, bsp);
    }

    // =========================================================================
    // Retrieval tests
    // =========================================================================

    void testRetrieval_unknownReturnsNull()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QStringLiteral("nonexistent-algorithm"));

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_emptyIdReturnsNull()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(QString());

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_hasAlgorithm()
    {
        auto* registry = AlgorithmRegistry::instance();

        QVERIFY(registry->hasAlgorithm(QLatin1String("master-stack")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("columns")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("bsp")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("rows")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("dwindle")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("spiral")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("monocle")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("three-column")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("grid")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("wide")));
        QVERIFY(registry->hasAlgorithm(QLatin1String("centered-master")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("nonexistent")));
    }

    void testRetrieval_allAlgorithms()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto all = registry->allAlgorithms();

        // At least 15 built-in algorithms are registered before any scripted loader runs
        QVERIFY(all.size() >= 15);

        for (auto* algo : all) {
            QVERIFY(algo != nullptr);
            QVERIFY(!algo->name().isEmpty());
        }
    }
    // =========================================================================
    // Monocle preview offset tests
    // =========================================================================

    // =========================================================================
    // Frozen globals validation — JS sandbox integrity
    // =========================================================================

    void testFrozenGlobals_jsReassignmentBlocked()
    {
        // TODO: The current test infrastructure does not expose a way to evaluate
        // arbitrary JS code in the sandboxed QJSEngine of a ScriptedAlgorithm.
        // The engine is private to each ScriptedAlgorithm instance, and there is
        // no public evaluateJs() method.
        //
        // What should be tested:
        //   1. Load any algorithm (e.g., "zen")
        //   2. In its sandbox, evaluate:
        //        try { PZ_MIN_ZONE_SIZE = 999; } catch(e) {}
        //        try { PZ_MIN_SPLIT = 999; } catch(e) {}
        //        try { PZ_MAX_SPLIT = 999; } catch(e) {}
        //        try { MAX_TREE_DEPTH = 999; } catch(e) {}
        //        try { distributeWithGaps = function() { return []; }; } catch(e) {}
        //        try { solveTwoPart = function() { return [0,0]; }; } catch(e) {}
        //   3. Verify each global still holds its original value
        //
        // Indirect validation: the determinism test in test_tiling_algo_edge_cases.cpp
        // (testFrozenGlobals_allAlgorithmsDeterministic) confirms that calling the
        // same algorithm twice produces identical results, which would fail if
        // globals were mutated between calls.

        // Verify determinism as an indirect frozen-globals check: two identical
        // calls must produce identical output.
        auto* registry = AlgorithmRegistry::instance();
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        const QRect screen(0, 0, 1920, 1080);

        // Test with algorithms that exercise different builtins
        const QStringList algoIds = {
            QLatin1String("zen"), // uses distributeWithGaps, solveTwoPart
            QLatin1String("tatami"), // uses distributeEvenly
            QLatin1String("three-column"), // uses solveThreeColumn
            QLatin1String("bsp"), // uses tree recursion with MAX_TREE_DEPTH
        };
        for (const auto& id : algoIds) {
            auto* algo = registry->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Missing algorithm: ") + id));
            auto zones1 = algo->calculateZones({5, screen, &state, 10, EdgeGaps::uniform(0)});
            auto zones2 = algo->calculateZones({5, screen, &state, 10, EdgeGaps::uniform(0)});
            QVERIFY2(
                zones1 == zones2,
                qPrintable(QStringLiteral("Algorithm '%1' non-deterministic — frozen globals may be mutable").arg(id)));
        }
    }

    // =========================================================================
    // Monocle preview offset tests
    // =========================================================================

    void testMonoclePreview_offsetStacking()
    {
        // Simulate monocle output: all zones are identical (full screen)
        const QRect previewRect(0, 0, 1000, 1000);
        QVector<QRect> identicalZones;
        for (int i = 0; i < 4; ++i) {
            identicalZones.append(previewRect);
        }

        auto result = AlgorithmRegistry::zonesToRelativeGeometry(identicalZones, previewRect);
        QCOMPARE(result.size(), 4);

        // Verify each zone has positive dimensions and offset stacking
        for (int i = 0; i < result.size(); ++i) {
            auto map = result[i].toMap();
            auto geo = map[QLatin1String("relativeGeometry")].toMap();
            qreal x = geo[QLatin1String("x")].toReal();
            qreal y = geo[QLatin1String("y")].toReal();
            qreal w = geo[QLatin1String("width")].toReal();
            qreal h = geo[QLatin1String("height")].toReal();

            QVERIFY2(w > 0.0, qPrintable(QStringLiteral("Zone %1 width (%2) must be > 0").arg(i).arg(w)));
            QVERIFY2(h > 0.0, qPrintable(QStringLiteral("Zone %1 height (%2) must be > 0").arg(i).arg(h)));
            QVERIFY2(x >= 0.0, qPrintable(QStringLiteral("Zone %1 x (%2) must be >= 0").arg(i).arg(x)));
            QVERIFY2(y >= 0.0, qPrintable(QStringLiteral("Zone %1 y (%2) must be >= 0").arg(i).arg(y)));
        }

        // Verify zones are not all identical (offset was applied)
        auto firstGeo = result[0].toMap()[QLatin1String("relativeGeometry")].toMap();
        auto lastGeo = result[result.size() - 1].toMap()[QLatin1String("relativeGeometry")].toMap();
        bool different = (firstGeo[QLatin1String("x")].toReal() != lastGeo[QLatin1String("x")].toReal())
            || (firstGeo[QLatin1String("y")].toReal() != lastGeo[QLatin1String("y")].toReal())
            || (firstGeo[QLatin1String("width")].toReal() != lastGeo[QLatin1String("width")].toReal())
            || (firstGeo[QLatin1String("height")].toReal() != lastGeo[QLatin1String("height")].toReal());
        QVERIFY2(different, "Monocle preview zones should have offset stacking, not be identical");
    }

    void testMonoclePreview_16Zones_offsetCapsAt045()
    {
        // With 16 identical zones and MonoclePreviewOffset=0.03, the raw offset
        // for zone 15 would be 15*0.03 = 0.45. Verify the cap is respected and
        // all zones remain valid (positive dimensions).
        const QRect previewRect(0, 0, 1000, 1000);
        QVector<QRect> identicalZones;
        for (int i = 0; i < 16; ++i) {
            identicalZones.append(previewRect);
        }

        auto result = AlgorithmRegistry::zonesToRelativeGeometry(identicalZones, previewRect);
        QCOMPARE(result.size(), 16);

        for (int i = 0; i < result.size(); ++i) {
            auto map = result[i].toMap();
            auto geo = map[QLatin1String("relativeGeometry")].toMap();
            qreal x = geo[QLatin1String("x")].toReal();
            qreal y = geo[QLatin1String("y")].toReal();
            qreal w = geo[QLatin1String("width")].toReal();
            qreal h = geo[QLatin1String("height")].toReal();

            // Offset must not exceed 0.45
            QVERIFY2(x <= 0.45 + 1e-9,
                     qPrintable(QStringLiteral("Zone %1 x offset (%2) exceeds 0.45 cap").arg(i).arg(x)));
            QVERIFY2(y <= 0.45 + 1e-9,
                     qPrintable(QStringLiteral("Zone %1 y offset (%2) exceeds 0.45 cap").arg(i).arg(y)));

            // All zones must have positive dimensions
            QVERIFY2(w > 0.0, qPrintable(QStringLiteral("Zone %1 width (%2) must be > 0").arg(i).arg(w)));
            QVERIFY2(h > 0.0, qPrintable(QStringLiteral("Zone %1 height (%2) must be > 0").arg(i).arg(h)));

            // All geometry values must be within [0, 1]
            QVERIFY2(x >= 0.0 && x <= 1.0, qPrintable(QStringLiteral("Zone %1 x (%2) out of [0,1]").arg(i).arg(x)));
            QVERIFY2(y >= 0.0 && y <= 1.0, qPrintable(QStringLiteral("Zone %1 y (%2) out of [0,1]").arg(i).arg(y)));
        }

        // Verify last few zones share the capped offset (zones 15 and beyond all get 0.45)
        auto geo15 = result[15].toMap()[QLatin1String("relativeGeometry")].toMap();
        qreal x15 = geo15[QLatin1String("x")].toReal();
        QVERIFY2(qAbs(x15 - 0.45) < 1e-9,
                 qPrintable(QStringLiteral("Zone 15 x offset (%1) should be at cap 0.45").arg(x15)));
    }

    void testMonoclePreview_singleZoneNoOffset()
    {
        // A single zone should not get offset treatment
        const QRect previewRect(0, 0, 1000, 1000);
        QVector<QRect> singleZone;
        singleZone.append(previewRect);

        auto result = AlgorithmRegistry::zonesToRelativeGeometry(singleZone, previewRect);
        QCOMPARE(result.size(), 1);

        auto geo = result[0].toMap()[QLatin1String("relativeGeometry")].toMap();
        QCOMPARE(geo[QLatin1String("x")].toReal(), 0.0);
        QCOMPARE(geo[QLatin1String("y")].toReal(), 0.0);
        QCOMPARE(geo[QLatin1String("width")].toReal(), 1.0);
        QCOMPARE(geo[QLatin1String("height")].toReal(), 1.0);
    }
};

QTEST_MAIN(TestAlgorithmRegistry)
#include "test_algorithm_registry.moc"
