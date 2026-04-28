// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QObject>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorAnimation;

class TestPhosphorProfileRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        PhosphorProfileRegistry::instance().clear();
    }

    /// Guarantees no registry state leaks between test methods even
    /// when a test forgets to clean up after itself. See the matching
    /// comment in test_profileloader.cpp.
    void cleanup()
    {
        PhosphorProfileRegistry::instance().clear();
    }

    void testSingletonIdentity()
    {
        PhosphorProfileRegistry& a = PhosphorProfileRegistry::instance();
        PhosphorProfileRegistry& b = PhosphorProfileRegistry::instance();
        QCOMPARE(&a, &b);
    }

    void testEmptyRegistryResolvesToNullopt()
    {
        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("nope"));
        QVERIFY(!resolved.has_value());
    }

    void testRegisterAndResolve()
    {
        Profile p;
        p.duration = 250.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("overlay.fade"), p);

        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectiveDuration(), 250.0);
    }

    /// profileChanged(path) fires on register. Consumers bound to the
    /// path re-resolve via this signal — load-bearing for the live-
    /// settings update contract.
    void testRegisterFiresSignal()
    {
        QSignalSpy spy(&PhosphorProfileRegistry::instance(), &PhosphorProfileRegistry::profileChanged);
        Profile p;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("x"), p);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("x"));
    }

    /// Replacing an existing profile also fires the signal — the
    /// consumer's cached resolved value is out of date.
    void testReplaceFiresSignal()
    {
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("x"), Profile{});
        QSignalSpy spy(&PhosphorProfileRegistry::instance(), &PhosphorProfileRegistry::profileChanged);
        Profile updated;
        updated.duration = 500.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("x"), updated);
        QCOMPARE(spy.count(), 1);
    }

    /// unregister of an existing path fires; unregister of a missing
    /// path does not (nothing to invalidate).
    void testUnregisterFiresOnlyIfExisted()
    {
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("x"), Profile{});
        QSignalSpy spy(&PhosphorProfileRegistry::instance(), &PhosphorProfileRegistry::profileChanged);
        PhosphorProfileRegistry::instance().unregisterProfile(QStringLiteral("x"));
        QCOMPARE(spy.count(), 1);
        PhosphorProfileRegistry::instance().unregisterProfile(QStringLiteral("not-there"));
        QCOMPARE(spy.count(), 1); // unchanged
    }

    /// reloadAll swaps the entire contents and fires profilesReloaded
    /// — load-bearing for the sub-commit-5 loader rescan path.
    void testReloadAllEmitsReloadSignal()
    {
        QHash<QString, Profile> replacement;
        replacement.insert(QStringLiteral("a"), Profile{});
        Profile withDuration;
        withDuration.duration = 300.0;
        replacement.insert(QStringLiteral("b"), withDuration);

        QSignalSpy changedSpy(&PhosphorProfileRegistry::instance(), &PhosphorProfileRegistry::profileChanged);
        QSignalSpy reloadedSpy(&PhosphorProfileRegistry::instance(), &PhosphorProfileRegistry::profilesReloaded);
        PhosphorProfileRegistry::instance().reloadAll(replacement);

        QCOMPARE(reloadedSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 0); // bulk reload coalesces to one signal
        QCOMPARE(PhosphorProfileRegistry::instance().profileCount(), 2);
        QVERIFY(PhosphorProfileRegistry::instance().hasProfile(QStringLiteral("b")));
    }

    void testProfileCountAndHasProfile()
    {
        PhosphorProfileRegistry& r = PhosphorProfileRegistry::instance();
        QCOMPARE(r.profileCount(), 0);
        QVERIFY(!r.hasProfile(QStringLiteral("x")));

        r.registerProfile(QStringLiteral("x"), Profile{});
        QCOMPARE(r.profileCount(), 1);
        QVERIFY(r.hasProfile(QStringLiteral("x")));

        r.registerProfile(QStringLiteral("y"), Profile{});
        QCOMPARE(r.profileCount(), 2);
    }

    /// `ownerReloaded(tag)` fires exactly once per partitioned-reload
    /// batch, AFTER every per-path `profileChanged` signal. Consumers
    /// that want to coalesce UI updates across a rescan use this as
    /// the batch-boundary marker instead of reacting to each per-path
    /// signal individually.
    void testOwnerReloadedFiresOnceAfterPerPathBurst()
    {
        PhosphorProfileRegistry& r = PhosphorProfileRegistry::instance();
        const QString tag = QStringLiteral("test-owner");

        QHash<QString, Profile> initial;
        initial.insert(QStringLiteral("a.one"), Profile{});
        Profile withDuration;
        withDuration.duration = 200.0;
        initial.insert(QStringLiteral("a.two"), withDuration);

        QSignalSpy changedSpy(&r, &PhosphorProfileRegistry::profileChanged);
        QSignalSpy reloadedSpy(&r, &PhosphorProfileRegistry::ownerReloaded);
        QSignalSpy bulkSpy(&r, &PhosphorProfileRegistry::profilesReloaded);

        r.reloadFromOwner(tag, initial);

        QCOMPARE(changedSpy.count(), 2); // one per path that changed
        QCOMPARE(reloadedSpy.count(), 1); // one batch boundary signal
        QCOMPARE(reloadedSpy.first().at(0).toString(), tag);
        QCOMPARE(bulkSpy.count(), 0); // profilesReloaded is wholesale-only

        // No-op reload (same content): zero signals — including ownerReloaded.
        changedSpy.clear();
        reloadedSpy.clear();
        r.reloadFromOwner(tag, initial);
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(reloadedSpy.count(), 0);

        // clearOwner also fires ownerReloaded after the per-path removals.
        r.reloadFromOwner(tag, initial); // seed
        reloadedSpy.clear();
        changedSpy.clear();
        r.clearOwner(tag);
        QCOMPARE(changedSpy.count(), 2); // one per removed path
        QCOMPARE(reloadedSpy.count(), 1);
        QCOMPARE(reloadedSpy.first().at(0).toString(), tag);
    }
};

QTEST_MAIN(TestPhosphorProfileRegistry)
#include "test_phosphorprofileregistry.moc"
