// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Simple test algorithm for registration/unregistration tests
 */
class CustomTestAlgorithm : public TilingAlgorithm
{
    Q_OBJECT
public:
    explicit CustomTestAlgorithm(const QString& name = QStringLiteral("Test Algorithm"))
        : m_name(name)
    {
    }

    QString name() const noexcept override
    {
        return m_name;
    }
    QString description() const override
    {
        return QStringLiteral("Test algorithm for unit tests");
    }
    QVector<QRect> calculateZones(const TilingParams& params) const override
    {
        QVector<QRect> zones;
        if (params.windowCount <= 0) {
            return zones;
        }
        const auto& screen = params.screenGeometry;
        const int width = screen.width() / params.windowCount;
        for (int i = 0; i < params.windowCount; ++i) {
            zones.append(QRect(screen.x() + i * width, screen.y(), width, screen.height()));
        }
        return zones;
    }

private:
    QString m_name;
};

/**
 * @brief Tests for custom algorithm registration, order, and functionality
 */
class TestAlgorithmRegistryCustom : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void cleanupTestCase()
    {
        // Clean up any test algorithms that might still be registered
        auto* registry = AlgorithmRegistry::instance();
        const QStringList testIds = {
            QStringLiteral("test-replace"),  QStringLiteral("test-signal"),     QStringLiteral("test-double-1"),
            QStringLiteral("test-double-2"), QStringLiteral("test-unregister"), QStringLiteral("test-order-remove"),
            QStringLiteral("test-null"),
        };
        for (const auto& id : testIds) {
            if (registry->hasAlgorithm(id)) {
                registry->unregisterAlgorithm(id);
            }
        }
    }

    // =========================================================================
    // Registration edge case tests
    // =========================================================================

    void testRegister_emptyIdIgnored()
    {
        auto* registry = AlgorithmRegistry::instance();
        int countBefore = registry->availableAlgorithms().size();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(QString(), new CustomTestAlgorithm());

        QCOMPARE(registry->availableAlgorithms().size(), countBefore);
        QCOMPARE(spy.count(), 0);
    }

    void testRegister_nullptrIgnored()
    {
        auto* registry = AlgorithmRegistry::instance();
        int countBefore = registry->availableAlgorithms().size();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(QStringLiteral("test-null"), nullptr);

        QCOMPARE(registry->availableAlgorithms().size(), countBefore);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("test-null")));
    }

    void testRegister_replacesExisting()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-replace");

        auto* algo1 = new CustomTestAlgorithm(QStringLiteral("First"));
        registry->registerAlgorithm(testId, algo1);
        QVERIFY(registry->hasAlgorithm(testId));
        QCOMPARE(registry->algorithm(testId)->name(), QStringLiteral("First"));

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        auto* algo2 = new CustomTestAlgorithm(QStringLiteral("Second"));
        registry->registerAlgorithm(testId, algo2);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);
        QCOMPARE(registry->algorithm(testId)->name(), QStringLiteral("Second"));

        registry->unregisterAlgorithm(testId);
    }

    void testRegister_signalEmitted()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-signal");

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(testId, new CustomTestAlgorithm());

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);

        registry->unregisterAlgorithm(testId);
    }

    void testRegister_doubleRegistrationRejected()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QString id1 = QStringLiteral("test-double-1");
        const QString id2 = QStringLiteral("test-double-2");

        auto* algo = new CustomTestAlgorithm(QStringLiteral("Double Test"));
        registry->registerAlgorithm(id1, algo);
        QVERIFY(registry->hasAlgorithm(id1));

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmRegistered);
        registry->registerAlgorithm(id2, algo);

        QVERIFY(!registry->hasAlgorithm(id2));
        QCOMPARE(spy.count(), 0);
        QVERIFY(registry->hasAlgorithm(id1));
        QCOMPARE(registry->algorithm(id1), algo);

        registry->unregisterAlgorithm(id1);
    }

    // =========================================================================
    // Unregistration tests
    // =========================================================================

    void testUnregister_success()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-unregister");

        registry->registerAlgorithm(testId, new CustomTestAlgorithm());
        QVERIFY(registry->hasAlgorithm(testId));

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmUnregistered);
        bool result = registry->unregisterAlgorithm(testId);

        QVERIFY(result);
        QVERIFY(!registry->hasAlgorithm(testId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), testId);
    }

    void testUnregister_nonexistentReturnsFalse()
    {
        auto* registry = AlgorithmRegistry::instance();

        QSignalSpy spy(registry, &AlgorithmRegistry::algorithmUnregistered);
        bool result = registry->unregisterAlgorithm(QStringLiteral("nonexistent-id"));

        QVERIFY(!result);
        QCOMPARE(spy.count(), 0);
    }

    void testUnregister_removesFromOrder()
    {
        auto* registry = AlgorithmRegistry::instance();
        const QString testId = QStringLiteral("test-order-remove");

        registry->registerAlgorithm(testId, new CustomTestAlgorithm());
        QVERIFY(registry->availableAlgorithms().contains(testId));

        registry->unregisterAlgorithm(testId);
        QVERIFY(!registry->availableAlgorithms().contains(testId));
    }

    // =========================================================================
    // Registration order tests
    // =========================================================================

    void testOrder_preservedInAvailableAlgorithms()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();

        QVERIFY(available.size() >= 15);
        QCOMPARE(available[0], DBus::AutotileAlgorithm::BSP);
        QCOMPARE(available[1], DBus::AutotileAlgorithm::CenteredMaster);
        QCOMPARE(available[2], DBus::AutotileAlgorithm::Columns);
        QCOMPARE(available[3], DBus::AutotileAlgorithm::Dwindle);
        QCOMPARE(available[4], DBus::AutotileAlgorithm::DwindleMemory);
        QCOMPARE(available[5], DBus::AutotileAlgorithm::Grid);
        QCOMPARE(available[6], DBus::AutotileAlgorithm::MasterStack);
        QCOMPARE(available[7], DBus::AutotileAlgorithm::Monocle);
        QCOMPARE(available[8], DBus::AutotileAlgorithm::Rows);
        QCOMPARE(available[9], DBus::AutotileAlgorithm::Spiral);
        QCOMPARE(available[10], DBus::AutotileAlgorithm::ThreeColumn);
        QCOMPARE(available[11], DBus::AutotileAlgorithm::Wide);
        QCOMPARE(available[12], DBus::AutotileAlgorithm::Cascade);
        QCOMPARE(available[13], DBus::AutotileAlgorithm::Stair);
        QCOMPARE(available[14], DBus::AutotileAlgorithm::Spread);
    }

    void testOrder_matchesAllAlgorithms()
    {
        auto* registry = AlgorithmRegistry::instance();
        auto available = registry->availableAlgorithms();
        auto all = registry->allAlgorithms();

        QCOMPARE(available.size(), all.size());

        for (int i = 0; i < available.size(); ++i) {
            auto* expected = registry->algorithm(available[i]);
            QCOMPARE(all[i], expected);
        }
    }

    // =========================================================================
    // Algorithm functionality through registry tests
    // =========================================================================

    void testFunctionality_algorithmsWork()
    {
        auto* registry = AlgorithmRegistry::instance();
        QRect screen(0, 0, 1920, 1080);
        TilingState state(QStringLiteral("test"));

        for (const QString& id : registry->availableAlgorithms()) {
            auto* algo = registry->algorithm(id);
            QVERIFY(algo != nullptr);

            auto zones = algo->calculateZones({4, screen, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), 4);

            for (const QRect& zone : zones) {
                QVERIFY(zone.isValid());
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }
};

QTEST_MAIN(TestAlgorithmRegistryCustom)
#include "test_algorithm_registry_custom.moc"
