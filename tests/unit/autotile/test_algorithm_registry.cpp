// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/MasterStackAlgorithm.h"
#include "autotile/algorithms/ColumnsAlgorithm.h"
#include "autotile/algorithms/BSPAlgorithm.h"
#include "autotile/algorithms/RowsAlgorithm.h"
#include "autotile/algorithms/DwindleAlgorithm.h"
#include "autotile/algorithms/SpiralAlgorithm.h"
#include "autotile/algorithms/MonocleAlgorithm.h"
#include "autotile/algorithms/ThreeColumnAlgorithm.h"
#include "autotile/algorithms/GridAlgorithm.h"
#include "autotile/algorithms/WideAlgorithm.h"
#include "autotile/algorithms/CenteredMasterAlgorithm.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for AlgorithmRegistry: singleton, built-in algorithms,
 *        retrieval, and default algorithm behavior.
 */
class TestAlgorithmRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::MasterStack);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Master + Stack"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_columnsRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Columns);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Columns"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_bspRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::BSP);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Binary Split"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_rowsRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Rows);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Rows"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_dwindleRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Dwindle);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Dwindle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_spiralRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Spiral);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Spiral"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_monocleRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Monocle);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Monocle"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_threeColumnRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::ThreeColumn);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Three Column"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_gridRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Grid);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Grid"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_wideRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::Wide);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Wide"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_centeredMasterRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry->algorithm(DBus::AutotileAlgorithm::CenteredMaster);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Centered Master"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_allRegistered()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();

        QVERIFY(available.contains(DBus::AutotileAlgorithm::MasterStack));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Wide));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Columns));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::BSP));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Rows));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Dwindle));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Spiral));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Monocle));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::ThreeColumn));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Grid));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::CenteredMaster));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Cascade));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Stair));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Spread));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::DwindleMemory));
        // At least 15 built-in algorithms; scripted algorithms may add more
        QVERIFY(available.size() >= 15);
    }

    // =========================================================================
    // Default algorithm tests
    // =========================================================================

    void testDefault_algorithmId()
    {
        QCOMPARE(AlgorithmRegistry::defaultAlgorithmId(), DBus::AutotileAlgorithm::BSP);
    }

    void testDefault_algorithmInstance()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto* defaultAlgo = registry->defaultAlgorithm();
        auto* bsp = registry->algorithm(DBus::AutotileAlgorithm::BSP);

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

        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::MasterStack));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Columns));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::BSP));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Rows));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Dwindle));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Spiral));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Monocle));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::ThreeColumn));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Grid));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Wide));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::CenteredMaster));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("nonexistent")));
    }

    void testRetrieval_allAlgorithms()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto all = registry->allAlgorithms();

        QVERIFY(all.size() >= 15);

        for (auto* algo : all) {
            QVERIFY(algo != nullptr);
            QVERIFY(!algo->name().isEmpty());
        }
    }
};

QTEST_MAIN(TestAlgorithmRegistry)
#include "test_algorithm_registry.moc"
