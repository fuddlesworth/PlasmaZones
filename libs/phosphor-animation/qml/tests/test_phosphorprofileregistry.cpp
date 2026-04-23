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
};

QTEST_MAIN(TestPhosphorProfileRegistry)
#include "test_phosphorprofileregistry.moc"
