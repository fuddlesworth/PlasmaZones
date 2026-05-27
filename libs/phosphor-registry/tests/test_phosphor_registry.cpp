// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Unit tests for Registry<T>. Verifies the register / unregister /
// lookup / enumerate / signal contract documented in Registry.h.

#include "test_registry_helpers.h"

#include <PhosphorRegistry/Registry.h>
#include <PhosphorRegistry/RegistryNotifier.h>

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorRegistry;

class TestRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void register_addsFactoryAndFiresSignal();
    void register_duplicateIdRejected();
    void register_nullFactoryRejected();
    void register_emptyIdRejected();
    void unregister_removesFactoryAndFiresSignal();
    void unregister_unknownIdNoOp();
    void factory_lookupReturnsRegistered();
    void factory_unknownReturnsNull();
    void ids_enumeratesAll();
    void forEach_visitsEveryFactory();
    void empty_reportsState();
    void size_tracksCount();
    void signals_fireInRegistrationOrder();
};

void TestRegistry::register_addsFactoryAndFiresSignal()
{
    Registry<IBarWidgetFactory> reg;
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryRegistered);

    auto factory = std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("Clock"));
    reg.registerFactory(factory);

    QCOMPARE(reg.size(), 1);
    QVERIFY(!reg.isEmpty());
    QCOMPARE(reg.factory(QStringLiteral("clock")).get(), factory.get());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("clock"));
}

void TestRegistry::register_duplicateIdRejected()
{
    Registry<IBarWidgetFactory> reg;
    auto first = std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("First"));
    auto second = std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("Second"));
    reg.registerFactory(first);
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryRegistered);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("duplicate id.*clock.*ignored")));
    reg.registerFactory(second);

    QCOMPARE(reg.size(), 1);
    QCOMPARE(reg.factory(QStringLiteral("clock"))->displayName(), QStringLiteral("First"));
    QCOMPARE(spy.count(), 0);
}

void TestRegistry::register_nullFactoryRejected()
{
    Registry<IBarWidgetFactory> reg;
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryRegistered);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null factory ignored")));
    reg.registerFactory(nullptr);
    QCOMPARE(reg.size(), 0);
    QCOMPARE(spy.count(), 0);
}

void TestRegistry::register_emptyIdRejected()
{
    Registry<IBarWidgetFactory> reg;
    auto factory = std::make_shared<FakeBarWidgetFactory>(QString(), QStringLiteral("Empty"));
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryRegistered);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("empty id ignored")));
    reg.registerFactory(factory);
    QCOMPARE(reg.size(), 0);
    QCOMPARE(spy.count(), 0);
}

void TestRegistry::unregister_removesFactoryAndFiresSignal()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("Clock")));
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryUnregistered);

    reg.unregisterFactory(QStringLiteral("clock"));

    QCOMPARE(reg.size(), 0);
    QVERIFY(reg.isEmpty());
    QVERIFY(!reg.factory(QStringLiteral("clock")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("clock"));
}

void TestRegistry::unregister_unknownIdNoOp()
{
    Registry<IBarWidgetFactory> reg;
    QSignalSpy spy(reg.notifier(), &RegistryNotifier::factoryUnregistered);
    reg.unregisterFactory(QStringLiteral("nonexistent"));
    QCOMPARE(spy.count(), 0);
}

void TestRegistry::factory_lookupReturnsRegistered()
{
    Registry<IBarWidgetFactory> reg;
    auto factory = std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("Clock"));
    reg.registerFactory(factory);
    auto retrieved = reg.factory(QStringLiteral("clock"));
    QCOMPARE(retrieved.get(), factory.get());
    QCOMPARE(retrieved->displayName(), QStringLiteral("Clock"));
}

void TestRegistry::factory_unknownReturnsNull()
{
    Registry<IBarWidgetFactory> reg;
    QVERIFY(!reg.factory(QStringLiteral("ghost")));
}

void TestRegistry::ids_enumeratesAll()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("c"), QStringLiteral("C")));
    QList<QString> ids = reg.ids();
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, (QList<QString>{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

void TestRegistry::forEach_visitsEveryFactory()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));

    QStringList visited;
    reg.forEach([&](const std::shared_ptr<IBarWidgetFactory>& f) {
        visited.append(f->id());
    });
    visited.sort();
    QCOMPARE(visited, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestRegistry::empty_reportsState()
{
    Registry<IBarWidgetFactory> reg;
    QVERIFY(reg.isEmpty());
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    QVERIFY(!reg.isEmpty());
    reg.unregisterFactory(QStringLiteral("a"));
    QVERIFY(reg.isEmpty());
}

void TestRegistry::size_tracksCount()
{
    Registry<IBarWidgetFactory> reg;
    QCOMPARE(reg.size(), 0);
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    QCOMPARE(reg.size(), 1);
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));
    QCOMPARE(reg.size(), 2);
    reg.unregisterFactory(QStringLiteral("a"));
    QCOMPARE(reg.size(), 1);
}

void TestRegistry::signals_fireInRegistrationOrder()
{
    // Locks the contract that factoryRegistered / factoryUnregistered
    // arrive in the exact order register / unregister are called.
    // QML consumers (model facades) rely on this ordering to keep a
    // stable row order without resorting after every signal.
    Registry<IBarWidgetFactory> reg;
    QSignalSpy regSpy(reg.notifier(), &RegistryNotifier::factoryRegistered);
    QSignalSpy unregSpy(reg.notifier(), &RegistryNotifier::factoryUnregistered);

    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));
    reg.unregisterFactory(QStringLiteral("a"));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("c"), QStringLiteral("C")));
    reg.unregisterFactory(QStringLiteral("b"));

    QCOMPARE(regSpy.count(), 3);
    QCOMPARE(regSpy.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(regSpy.at(1).at(0).toString(), QStringLiteral("b"));
    QCOMPARE(regSpy.at(2).at(0).toString(), QStringLiteral("c"));

    QCOMPARE(unregSpy.count(), 2);
    QCOMPARE(unregSpy.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(unregSpy.at(1).at(0).toString(), QStringLiteral("b"));
}

QTEST_MAIN(TestRegistry)
#include "test_phosphor_registry.moc"
