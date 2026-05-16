// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QSignalSpy>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"
#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Unit tests for PhosphorTiles::AlgorithmRegistry: built-in algorithms,
 *        retrieval, and default algorithm behavior.
 *
 * The registry is no longer a process-global singleton; composition roots
 * (daemon, editor, settings, this test fixture) each own their own instance.
 * Tests share a fixture registry via ScriptedAlgoTestSetup::registry() so
 * built-ins only register once per test-process run.
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
    // Fixture sanity
    // =========================================================================

    void testFixture_registryNonNull()
    {
        // Sanity-check the fixture: the test setup hands back a non-null
        // registry and the same instance for both calls (it's a
        // fixture-owned instance, not a process singleton — but within
        // one test run the fixture's accessor is stable).
        auto* instance1 = m_scriptSetup.registry();
        auto* instance2 = m_scriptSetup.registry();

        QVERIFY(instance1 != nullptr);
        QCOMPARE(instance1, instance2);
    }

    // =========================================================================
    // Built-in algorithm tests
    // =========================================================================

    void testBuiltIn_masterStackRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("master-stack"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Master + Stack"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_columnsRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("columns"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Columns"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_bspRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("bsp"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Binary Split"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_rowsRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("rows"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Rows"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_dwindleRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("dwindle"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Dwindle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_spiralRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("spiral"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Spiral"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_monocleRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("monocle"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Monocle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_threeColumnRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("three-column"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Three Column"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_gridRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("grid"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Grid"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_wideRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("wide"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Wide"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_centeredMasterRegistered()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QLatin1String("centered-master"));

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Centered Master"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_allRegistered()
    {
        auto* registry = m_scriptSetup.registry();
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

    void testBuiltIn_idBackwardsCompatibility()
    {
        auto* registry = m_scriptSetup.registry();
        const QStringList expectedIds = {
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
        for (const auto& id : expectedIds) {
            QVERIFY2(registry->hasAlgorithm(id), qPrintable(QStringLiteral("Missing algorithm: ") + id));
        }
    }

    void testBuiltIn_allAlgorithmsCalculateZones()
    {
        auto* registry = m_scriptSetup.registry();
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        const QRect screen(0, 0, 1920, 1080);
        const auto allIds = registry->availableAlgorithms();
        QVERIFY2(allIds.size() >= 15,
                 qPrintable(QStringLiteral("Expected at least 15 algorithms, got %1").arg(allIds.size())));
        for (const auto& id : allIds) {
            auto* algo = registry->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Null algorithm: ") + id));
            auto zones = algo->calculateZones(makeParams(3, screen, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
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
        QCOMPARE(PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId(), QLatin1String("bsp"));
        // Round-trip through the virtual interface — concrete registry's
        // override resolves to the same id, so a future test fake that
        // overrides the policy can substitute its own.
        auto* registry = m_scriptSetup.registry();
        QCOMPARE(registry->defaultAlgorithmId(), QLatin1String("bsp"));
    }

    void testDefault_algorithmInstance()
    {
        auto* registry = m_scriptSetup.registry();
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
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QStringLiteral("nonexistent-algorithm"));

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_emptyIdReturnsNull()
    {
        auto* registry = m_scriptSetup.registry();
        auto* algo = registry->algorithm(QString());

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_hasAlgorithm()
    {
        auto* registry = m_scriptSetup.registry();

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
        auto* registry = m_scriptSetup.registry();
        auto all = registry->allAlgorithms();

        // At least 15 built-in algorithms are registered before any scripted loader runs
        QVERIFY(all.size() >= 15);

        for (auto* algo : all) {
            QVERIFY(algo != nullptr);
            QVERIFY(!algo->name().isEmpty());
        }
    }

    // =========================================================================
    // Frozen globals validation — JS sandbox integrity
    // =========================================================================

    void testFrozenGlobals_jsReassignmentBlocked()
    {
        // TODO: The current test infrastructure does not expose a way to evaluate
        // arbitrary JS code in the sandboxed QJSEngine of a PhosphorTiles::ScriptedAlgorithm.
        // The engine is private to each PhosphorTiles::ScriptedAlgorithm instance, and there is
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
        auto* registry = m_scriptSetup.registry();
        PhosphorTiles::TilingState state(QStringLiteral("test"));
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
            auto zones1 =
                algo->calculateZones(makeParams(5, screen, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
            auto zones2 =
                algo->calculateZones(makeParams(5, screen, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
            QVERIFY2(
                zones1 == zones2,
                qPrintable(QStringLiteral("Algorithm '%1' non-deterministic — frozen globals may be mutable").arg(id)));
        }
    }
};

QTEST_MAIN(TestAlgorithmRegistry)
#include "test_algorithm_registry.moc"
