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
    void replacePolicy_overwritesAndSignals();
    void ownerTag_bulkUnregister();
    void ids_returnRegistrationOrder();
    void reentrantSlot_noDeadlock();
    void forEach_mutationDuringVisitIsSafe();
    void clear_dropsAllWithoutSignals();
    void replace_adoptsNewOwnerTag();
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

// DuplicatePolicy::Replace overwrites an existing entry (and fires
// unregister(old) + register(new)); the default still rejects.
void TestRegistry::replacePolicy_overwritesAndSignals()
{
    Registry<IBarWidgetFactory> reg;
    QSignalSpy regSpy(reg.notifier(), &RegistryNotifier::factoryRegistered);
    QSignalSpy unregSpy(reg.notifier(), &RegistryNotifier::factoryUnregistered);

    QVERIFY(reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V1"))));
    // Default reject: second registration is a no-op, returns false.
    QVERIFY(
        !reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V2"))));
    QCOMPARE(reg.factory(QStringLiteral("clock"))->displayName(), QStringLiteral("V1"));

    // Replace: overwrite, fires unregister(old) + register(new).
    QVERIFY(reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V3")),
                                QString(), DuplicatePolicy::Replace));
    QCOMPARE(reg.factory(QStringLiteral("clock"))->displayName(), QStringLiteral("V3"));
    QCOMPARE(reg.size(), 1);
    QCOMPARE(regSpy.count(), 2); // V1 + V3
    QCOMPARE(unregSpy.count(), 1); // the replaced V1
}

// unregisterByOwner drops every entry sharing a tag and nothing else;
// an empty tag matches nothing.
void TestRegistry::ownerTag_bulkUnregister()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")),
                        QStringLiteral("plugin-x"));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")),
                        QStringLiteral("plugin-x"));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("c"), QStringLiteral("C")),
                        QStringLiteral("plugin-y"));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("d"), QStringLiteral("D"))); // untagged

    QSignalSpy unregSpy(reg.notifier(), &RegistryNotifier::factoryUnregistered);

    QCOMPARE(reg.unregisterByOwner(QString()), 0); // empty tag matches nothing
    QCOMPARE(reg.size(), 4);

    QCOMPARE(reg.unregisterByOwner(QStringLiteral("plugin-x")), 2);
    QCOMPARE(reg.size(), 2);
    QVERIFY(reg.factory(QStringLiteral("a")) == nullptr);
    QVERIFY(reg.factory(QStringLiteral("b")) == nullptr);
    QVERIFY(reg.factory(QStringLiteral("c")) != nullptr); // plugin-y untouched
    QVERIFY(reg.factory(QStringLiteral("d")) != nullptr); // untagged untouched
    QCOMPARE(unregSpy.count(), 2);
    // The bulk-unregister signals arrive in registration order (a before b),
    // not QHash order — unregisterByOwner walks m_order to uphold the same
    // ordered-signal contract as signals_fireInRegistrationOrder.
    QCOMPARE(unregSpy.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(unregSpy.at(1).at(0).toString(), QStringLiteral("b"));
}

// ids() / forEach() iterate in registration (insertion) order — NOT hash
// order. A Replace keeps the original position; an unregister removes it.
void TestRegistry::ids_returnRegistrationOrder()
{
    Registry<IBarWidgetFactory> reg;
    // Register in a deliberately non-alphabetical order.
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("zulu"), QStringLiteral("Z")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("alpha"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("mike"), QStringLiteral("M")));
    QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("zulu"), QStringLiteral("alpha"), QStringLiteral("mike")}));

    // forEach visits in the same order.
    QStringList visited;
    reg.forEach([&](const std::shared_ptr<IBarWidgetFactory>& f) {
        visited.append(f->id());
    });
    QCOMPARE(visited, (QStringList{QStringLiteral("zulu"), QStringLiteral("alpha"), QStringLiteral("mike")}));

    // Replace keeps the position (does not move to the end).
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("alpha"), QStringLiteral("A2")),
                        QString(), DuplicatePolicy::Replace);
    QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("zulu"), QStringLiteral("alpha"), QStringLiteral("mike")}));

    // Unregister removes from the order; a later register appends.
    reg.unregisterFactory(QStringLiteral("zulu"));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("bravo"), QStringLiteral("B")));
    QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("alpha"), QStringLiteral("mike"), QStringLiteral("bravo")}));
}

// Thread-safety contract part 1: change signals fire OUTSIDE the registry's
// (non-recursive) mutex, so a slot may re-enter the registry without a
// self-deadlock. If an emit ran while the lock was held, the locking calls
// inside the slot (factory/size) would hang the test.
void TestRegistry::reentrantSlot_noDeadlock()
{
    Registry<IBarWidgetFactory> reg;
    QObject ctx;
    bool slotRan = false;
    QObject::connect(reg.notifier(), &RegistryNotifier::factoryRegistered, &ctx, [&](const QString& id) {
        slotRan = true;
        // Both calls take the registry mutex — a deadlock here would hang.
        QVERIFY(reg.factory(id) != nullptr);
        QCOMPARE(reg.size(), 1);
    });
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("Clock")));
    QVERIFY(slotRan);
}

// Thread-safety contract part 2: forEach iterates a SNAPSHOT taken under the
// lock and runs the visitor outside it, so mutating the registry from the
// visitor is safe and does not affect the in-flight iteration (no QHash
// iterator-invalidation UB).
void TestRegistry::forEach_mutationDuringVisitIsSafe()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));

    QStringList visited;
    reg.forEach([&](const std::shared_ptr<IBarWidgetFactory>& f) {
        visited.append(f->id());
        // Mutate mid-iteration — must not corrupt the snapshot being visited.
        reg.unregisterFactory(f->id());
        reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("c"), QStringLiteral("C")), QString(),
                            DuplicatePolicy::Replace);
    });
    visited.sort();
    // The snapshot saw exactly the two entries present when forEach was called.
    QCOMPARE(visited, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    // And the mutations did take effect on the live registry.
    QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("c")}));
}

// clear() drops every entry at once and resets ids()/size() in lockstep,
// WITHOUT firing per-entry factoryUnregistered signals (the bulk-teardown
// contract used by composition-root shutdown).
void TestRegistry::clear_dropsAllWithoutSignals()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("a"), QStringLiteral("A")));
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("b"), QStringLiteral("B")));
    QCOMPARE(reg.size(), 2);

    QSignalSpy regSpy(reg.notifier(), &RegistryNotifier::factoryRegistered);
    QSignalSpy unregSpy(reg.notifier(), &RegistryNotifier::factoryUnregistered);

    reg.clear();

    QCOMPARE(reg.size(), 0);
    QVERIFY(reg.isEmpty());
    QVERIFY(reg.ids().isEmpty());
    QVERIFY(!reg.factory(QStringLiteral("a")));
    QCOMPARE(unregSpy.count(), 0); // silent — no per-entry signals
    QCOMPARE(regSpy.count(), 0);
    // The registry is reusable after clear().
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("c"), QStringLiteral("C")));
    QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("c")}));
}

// A DuplicatePolicy::Replace adopts the NEW call's owner tag — a tag-less
// Replace moves the entry to the untagged group (no longer bulk-removable);
// a re-tagging Replace moves it into the new owner group.
void TestRegistry::replace_adoptsNewOwnerTag()
{
    Registry<IBarWidgetFactory> reg;
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V1")),
                        QStringLiteral("plugin-x"));

    // Tag-less Replace → entry leaves the "plugin-x" owner group.
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V2")),
                        QString(), DuplicatePolicy::Replace);
    QCOMPARE(reg.unregisterByOwner(QStringLiteral("plugin-x")), 0); // no longer matches
    QVERIFY(reg.factory(QStringLiteral("clock")) != nullptr); // still registered (untagged)

    // Re-tagging Replace → entry joins the new "plugin-y" owner group.
    reg.registerFactory(std::make_shared<FakeBarWidgetFactory>(QStringLiteral("clock"), QStringLiteral("V3")),
                        QStringLiteral("plugin-y"), DuplicatePolicy::Replace);
    QCOMPARE(reg.unregisterByOwner(QStringLiteral("plugin-y")), 1); // now matches
    QVERIFY(reg.factory(QStringLiteral("clock")) == nullptr); // removed
}

QTEST_MAIN(TestRegistry)
#include "test_phosphor_registry.moc"
