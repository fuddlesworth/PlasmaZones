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
 * @brief Simple test algorithm for registration/unregistration tests
 */
class TestAlgorithm : public TilingAlgorithm
{
    Q_OBJECT
public:
    explicit TestAlgorithm(const QString &name = QStringLiteral("Test Algorithm"))
        : m_name(name)
    {
    }

    QString name() const noexcept override { return m_name; }
    QString description() const override { return QStringLiteral("Test algorithm for unit tests"); }
    QString icon() const noexcept override { return QStringLiteral("test-icon"); }

    QVector<QRect> calculateZones(int windowCount, const QRect &screen,
                                  const TilingState &state) const override
    {
        Q_UNUSED(state)
        QVector<QRect> zones;
        if (windowCount <= 0) {
            return zones;
        }
        // Simple equal columns
        const int width = screen.width() / windowCount;
        for (int i = 0; i < windowCount; ++i) {
            zones.append(QRect(screen.x() + i * width, screen.y(),
                               width, screen.height()));
        }
        return zones;
    }

private:
    QString m_name;
};

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
    // Registration edge case tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testRegister_emptyIdIgnored()
    {
        auto *registry = AlgorithmRegistry::instance();
        int countBefore = registry->availableAlgorithms().size();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(QString(), new TestAlgorithm());

        // Should not register - count unchanged, no signal
        QCOMPARE(registry->availableAlgorithms().size(), countBefore);
        QCOMPARE(spy.count(), 0);
    }

    void testRegister_nullptrIgnored()
    {
        auto *registry = AlgorithmRegistry::instance();
        int countBefore = registry->availableAlgorithms().size();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(QStringLiteral("test-null"), nullptr);

        // Should not register
        QCOMPARE(registry->availableAlgorithms().size(), countBefore);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("test-null")));
    }

    void testRegister_replacesExisting()
    {
        auto *registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-replace");

        // Register first algorithm
        auto *algo1 = new TestAlgorithm(QStringLiteral("First"));
        registry->registerAlgorithm(testId, algo1);
        QVERIFY(registry->hasAlgorithm(testId));
        QCOMPARE(registry->algorithm(testId)->name(), QStringLiteral("First"));

        // Register replacement with same ID
        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        auto *algo2 = new TestAlgorithm(QStringLiteral("Second"));
        registry->registerAlgorithm(testId, algo2);

        // Should replace - signal emitted, new algorithm returned
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);
        QCOMPARE(registry->algorithm(testId)->name(), QStringLiteral("Second"));

        // Cleanup
        registry->unregisterAlgorithm(testId);
    }

    void testRegister_signalEmitted()
    {
        auto *registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-signal");

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(testId, new TestAlgorithm());

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);

        // Cleanup
        registry->unregisterAlgorithm(testId);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Unregistration tests
    // ═══════════════════════════════════════════════════════════════════════════

    void testUnregister_success()
    {
        auto *registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-unregister");

        // Register first
        registry->registerAlgorithm(testId, new TestAlgorithm());
        QVERIFY(registry->hasAlgorithm(testId));

        // Unregister
        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmUnregistered);
        bool result = registry->unregisterAlgorithm(testId);

        QVERIFY(result);
        QVERIFY(!registry->hasAlgorithm(testId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);
    }

    void testUnregister_nonexistentReturnsFalse()
    {
        auto *registry = AlgorithmRegistry::instance();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmUnregistered);
        bool result = registry->unregisterAlgorithm(QStringLiteral("nonexistent-id"));

        QVERIFY(!result);
        QCOMPARE(spy.count(), 0);
    }

    void testUnregister_removesFromOrder()
    {
        auto *registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-order-remove");

        registry->registerAlgorithm(testId, new TestAlgorithm());
        QVERIFY(registry->availableAlgorithms().contains(testId));

        registry->unregisterAlgorithm(testId);
        QVERIFY(!registry->availableAlgorithms().contains(testId));
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
