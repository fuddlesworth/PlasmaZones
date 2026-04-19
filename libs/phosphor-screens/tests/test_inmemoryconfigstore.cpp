// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/InMemoryConfigStore.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QSignalSpy>
#include <QTest>

using Phosphor::Screens::InMemoryConfigStore;
using Phosphor::Screens::VirtualScreenConfig;
using Phosphor::Screens::VirtualScreenDef;

namespace {

constexpr QLatin1String kPhys{"Dell:U2722D:115107"};

VirtualScreenConfig makeHalves()
{
    VirtualScreenConfig cfg;
    cfg.physicalScreenId = kPhys;

    VirtualScreenDef a;
    a.index = 0;
    a.id = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
    a.physicalScreenId = kPhys;
    a.displayName = QStringLiteral("Left");
    a.region = QRectF(0.0, 0.0, 0.5, 1.0);

    VirtualScreenDef b = a;
    b.index = 1;
    b.id = PhosphorIdentity::VirtualScreenId::make(kPhys, 1);
    b.displayName = QStringLiteral("Right");
    b.region = QRectF(0.5, 0.0, 0.5, 1.0);

    cfg.screens.append(a);
    cfg.screens.append(b);
    return cfg;
}

} // namespace

class TestInMemoryConfigStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testSaveEmitsChanged()
    {
        InMemoryConfigStore store;
        QSignalSpy spy(&store, &InMemoryConfigStore::changed);

        QVERIFY(store.save(kPhys, makeHalves()));
        QCOMPARE(spy.count(), 1);
    }

    void testSaveNoOpDoesNotEmit()
    {
        InMemoryConfigStore store;
        const auto cfg = makeHalves();
        QVERIFY(store.save(kPhys, cfg));

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);
        QVERIFY(store.save(kPhys, cfg));
        QCOMPARE(spy.count(), 0);
    }

    void testSaveEmptyConfigRemoves()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHalves()));
        QCOMPARE(store.loadAll().size(), 1);

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);
        VirtualScreenConfig empty;
        empty.physicalScreenId = kPhys;
        QVERIFY(store.save(kPhys, empty));

        QVERIFY(store.loadAll().isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    void testRemoveMissingIsQuietSuccess()
    {
        InMemoryConfigStore store;
        QSignalSpy spy(&store, &InMemoryConfigStore::changed);
        QVERIFY(store.remove(kPhys));
        QCOMPARE(spy.count(), 0);
    }

    void testRejectsInvalidConfig()
    {
        InMemoryConfigStore store;

        // Single-screen "subdivision" must be rejected per isValid semantics.
        VirtualScreenConfig bad;
        bad.physicalScreenId = kPhys;
        VirtualScreenDef only;
        only.index = 0;
        only.id = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        only.physicalScreenId = kPhys;
        only.region = QRectF(0.0, 0.0, 1.0, 1.0);
        bad.screens.append(only);

        QSignalSpy spy(&store, &InMemoryConfigStore::changed);
        QVERIFY(!store.save(kPhys, bad));
        QCOMPARE(spy.count(), 0);
    }

    void testLoadAllReturnsSnapshot()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHalves()));

        const auto snapshot = store.loadAll();
        QCOMPARE(snapshot.size(), 1);
        QVERIFY(snapshot.contains(kPhys));
        QCOMPARE(snapshot.value(kPhys).screens.size(), 2);
    }

    void testGetReturnsEmptyForMissing()
    {
        InMemoryConfigStore store;
        const auto cfg = store.get(QStringLiteral("nonexistent"));
        QVERIFY(cfg.isEmpty());
    }

    void testCapAwareCtorRejectsOverLimit()
    {
        // Production parity: with a cap of 2, a 3-entry config that the
        // default-ctor store would accept must be rejected — same admission
        // surface SettingsConfigStore exposes in the daemon.
        InMemoryConfigStore store(/*maxScreensPerPhysical=*/2);
        QSignalSpy spy(&store, &InMemoryConfigStore::changed);

        VirtualScreenConfig tooMany;
        tooMany.physicalScreenId = kPhys;
        for (int i = 0; i < 3; ++i) {
            VirtualScreenDef d;
            d.index = i;
            d.id = PhosphorIdentity::VirtualScreenId::make(kPhys, i);
            d.physicalScreenId = kPhys;
            d.region = QRectF(i / 3.0, 0.0, 1.0 / 3.0, 1.0);
            tooMany.screens.append(d);
        }

        QVERIFY(!store.save(kPhys, tooMany));
        QCOMPARE(spy.count(), 0);
    }

    void testCapAwareCtorAcceptsAtLimit()
    {
        InMemoryConfigStore store(/*maxScreensPerPhysical=*/2);
        QVERIFY(store.save(kPhys, makeHalves()));
    }
};

QTEST_APPLESS_MAIN(TestInMemoryConfigStore)
#include "test_inmemoryconfigstore.moc"
