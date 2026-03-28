// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QSignalSpy>
#include <QStringList>

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
            QLatin1String("bsp"),     QLatin1String("cascade"),      QLatin1String("centered-master"),
            QLatin1String("columns"), QLatin1String("dwindle"),      QLatin1String("dwindle-memory"),
            QLatin1String("grid"),    QLatin1String("master-stack"), QLatin1String("monocle"),
            QLatin1String("rows"),    QLatin1String("spiral"),       QLatin1String("spread"),
            QLatin1String("stair"),   QLatin1String("three-column"), QLatin1String("wide"),
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
        const QStringList builtinIds = {
            QLatin1String("bsp"),     QLatin1String("cascade"),      QLatin1String("centered-master"),
            QLatin1String("columns"), QLatin1String("dwindle"),      QLatin1String("dwindle-memory"),
            QLatin1String("grid"),    QLatin1String("master-stack"), QLatin1String("monocle"),
            QLatin1String("rows"),    QLatin1String("spiral"),       QLatin1String("spread"),
            QLatin1String("stair"),   QLatin1String("three-column"), QLatin1String("wide"),
        };
        for (const auto& id : builtinIds) {
            auto* algo = registry->algorithm(id);
            QVERIFY2(algo != nullptr, qPrintable(QStringLiteral("Null algorithm: ") + id));
            auto zones = algo->calculateZones({3, screen, &state, 0, EdgeGaps::uniform(0)});
            QVERIFY2(!zones.isEmpty(), qPrintable(QStringLiteral("No zones from: ") + id));
            QVERIFY2(zones.size() == 3,
                     qPrintable(QStringLiteral("Expected 3 zones from: ") + id + QStringLiteral(", got: ")
                                + QString::number(zones.size())));
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
};

QTEST_MAIN(TestAlgorithmRegistry)
#include "test_algorithm_registry.moc"
