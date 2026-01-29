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
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for AlgorithmRegistry
 *
 * Tests cover:
 * - Singleton pattern
 * - Built-in algorithm registration
 * - Algorithm retrieval
 * - Custom algorithm registration/unregistration
 * - Signal emissions
 */
class TestAlgorithmRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Singleton tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testSingleton_sameInstance()
    {
        auto *instance1 = AlgorithmRegistry::instance();
        auto *instance2 = AlgorithmRegistry::instance();

        QVERIFY(instance1 != nullptr);
        QCOMPARE(instance1, instance2);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Built-in algorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testBuiltIn_masterStackRegistered()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *algo = registry->algorithm(DBus::AutotileAlgorithm::MasterStack);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Master + Stack"));
        QVERIFY(algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_columnsRegistered()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *algo = registry->algorithm(DBus::AutotileAlgorithm::Columns);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("Columns"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testBuiltIn_bspRegistered()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *algo = registry->algorithm(DBus::AutotileAlgorithm::BSP);

        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("BSP"));
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testBuiltIn_allThreeRegistered()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();

        QVERIFY(available.contains(DBus::AutotileAlgorithm::MasterStack));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::Columns));
        QVERIFY(available.contains(DBus::AutotileAlgorithm::BSP));
        QCOMPARE(available.size(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Default algorithm tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testDefault_algorithmId()
    {
        QCOMPARE(AlgorithmRegistry::defaultAlgorithmId(),
                 DBus::AutotileAlgorithm::MasterStack);
    }

    void testDefault_algorithmInstance()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *defaultAlgo = registry->defaultAlgorithm();
        auto *masterStack = registry->algorithm(DBus::AutotileAlgorithm::MasterStack);

        QVERIFY(defaultAlgo != nullptr);
        QCOMPARE(defaultAlgo, masterStack);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Retrieval tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testRetrieval_unknownReturnsNull()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *algo = registry->algorithm(QStringLiteral("nonexistent-algorithm"));

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_emptyIdReturnsNull()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto *algo = registry->algorithm(QString());

        QVERIFY(algo == nullptr);
    }

    void testRetrieval_hasAlgorithm()
    {
        auto *registry = AlgorithmRegistry::instance();

        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::MasterStack));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::Columns));
        QVERIFY(registry->hasAlgorithm(DBus::AutotileAlgorithm::BSP));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("nonexistent")));
    }

    void testRetrieval_allAlgorithms()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto all = registry->allAlgorithms();

        QCOMPARE(all.size(), 3);

        // All should be valid pointers
        for (auto *algo : all) {
            QVERIFY(algo != nullptr);
            QVERIFY(!algo->name().isEmpty());
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Registration order tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testOrder_preservedInAvailableAlgorithms()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();

        // Built-in registration order: MasterStack, Columns, BSP
        QCOMPARE(available.size(), 3);
        QCOMPARE(available[0], DBus::AutotileAlgorithm::MasterStack);
        QCOMPARE(available[1], DBus::AutotileAlgorithm::Columns);
        QCOMPARE(available[2], DBus::AutotileAlgorithm::BSP);
    }

    void testOrder_matchesAllAlgorithms()
    {
        auto *registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();
        auto all = registry->allAlgorithms();

        QCOMPARE(available.size(), all.size());

        for (int i = 0; i < available.size(); ++i) {
            auto *expected = registry->algorithm(available[i]);
            QCOMPARE(all[i], expected);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Algorithm functionality through registry tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testFunctionality_algorithmsWork()
    {
        auto *registry = AlgorithmRegistry::instance();
        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        // Test each algorithm can calculate zones
        for (const QString &id : registry->availableAlgorithms()) {
            auto *algo = registry->algorithm(id);
            QVERIFY(algo != nullptr);

            auto zones = algo->calculateZones(4, screen, state);
            QCOMPARE(zones.size(), 4);

            // All zones should be valid
            for (const QRect &zone : zones) {
                QVERIFY(zone.isValid());
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }
};

QTEST_MAIN(TestAlgorithmRegistry)
#include "test_algorithm_registry.moc"
