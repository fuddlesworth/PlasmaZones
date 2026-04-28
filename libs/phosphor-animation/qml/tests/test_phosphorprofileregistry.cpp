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

private:
    /// Per-test-case registry. Each test method gets a freshly default-
    /// constructed registry via init() — no shared global state, no
    /// cleanup-from-prior-test contamination. Replaces the prior
    /// `PhosphorProfileRegistry::instance()` Meyers singleton (Phase A3
    /// of the architecture refactor: composition roots own registries
    /// directly, tests follow the same DI pattern).
    PhosphorProfileRegistry m_registry;

private Q_SLOTS:
    void init()
    {
        m_registry.clear();
    }

    /// Guarantees no registry state leaks between test methods even
    /// when a test forgets to clean up after itself. See the matching
    /// comment in test_profileloader.cpp.
    void cleanup()
    {
        m_registry.clear();
    }

    /// Verify that the published default-registry handle round-trips —
    /// composition roots use this pair to expose their owned registry
    /// to QML, so a regression here would silently break every QML
    /// `PhosphorMotionAnimation { profile: "<path>" }` lookup.
    void testDefaultRegistryHandleRoundTrips()
    {
        QCOMPARE(PhosphorProfileRegistry::defaultRegistry(), nullptr);

        PhosphorProfileRegistry::setDefaultRegistry(&m_registry);
        QCOMPARE(PhosphorProfileRegistry::defaultRegistry(), &m_registry);

        PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        QCOMPARE(PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

    void testEmptyRegistryResolvesToNullopt()
    {
        auto resolved = m_registry.resolve(QStringLiteral("nope"));
        QVERIFY(!resolved.has_value());
    }

    void testRegisterAndResolve()
    {
        Profile p;
        p.duration = 250.0;
        m_registry.registerProfile(QStringLiteral("overlay.fade"), p);

        auto resolved = m_registry.resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectiveDuration(), 250.0);
    }

    /// profileChanged(path) fires on register. Consumers bound to the
    /// path re-resolve via this signal — load-bearing for the live-
    /// settings update contract.
    void testRegisterFiresSignal()
    {
        QSignalSpy spy(&m_registry, &PhosphorProfileRegistry::profileChanged);
        Profile p;
        m_registry.registerProfile(QStringLiteral("x"), p);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("x"));
    }

    /// Replacing an existing profile also fires the signal — the
    /// consumer's cached resolved value is out of date.
    void testReplaceFiresSignal()
    {
        m_registry.registerProfile(QStringLiteral("x"), Profile{});
        QSignalSpy spy(&m_registry, &PhosphorProfileRegistry::profileChanged);
        Profile updated;
        updated.duration = 500.0;
        m_registry.registerProfile(QStringLiteral("x"), updated);
        QCOMPARE(spy.count(), 1);
    }

    /// unregister of an existing path fires; unregister of a missing
    /// path does not (nothing to invalidate).
    void testUnregisterFiresOnlyIfExisted()
    {
        m_registry.registerProfile(QStringLiteral("x"), Profile{});
        QSignalSpy spy(&m_registry, &PhosphorProfileRegistry::profileChanged);
        m_registry.unregisterProfile(QStringLiteral("x"));
        QCOMPARE(spy.count(), 1);
        m_registry.unregisterProfile(QStringLiteral("not-there"));
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

        QSignalSpy changedSpy(&m_registry, &PhosphorProfileRegistry::profileChanged);
        QSignalSpy reloadedSpy(&m_registry, &PhosphorProfileRegistry::profilesReloaded);
        m_registry.reloadAll(replacement);

        QCOMPARE(reloadedSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 0); // bulk reload coalesces to one signal
        QCOMPARE(m_registry.profileCount(), 2);
        QVERIFY(m_registry.hasProfile(QStringLiteral("b")));
    }

    void testProfileCountAndHasProfile()
    {
        QCOMPARE(m_registry.profileCount(), 0);
        QVERIFY(!m_registry.hasProfile(QStringLiteral("x")));

        m_registry.registerProfile(QStringLiteral("x"), Profile{});
        QCOMPARE(m_registry.profileCount(), 1);
        QVERIFY(m_registry.hasProfile(QStringLiteral("x")));

        m_registry.registerProfile(QStringLiteral("y"), Profile{});
        QCOMPARE(m_registry.profileCount(), 2);
    }

    /// `ownerReloaded(tag)` fires exactly once per partitioned-reload
    /// batch, AFTER every per-path `profileChanged` signal. Consumers
    /// that want to coalesce UI updates across a rescan use this as
    /// the batch-boundary marker instead of reacting to each per-path
    /// signal individually.
    void testOwnerReloadedFiresOnceAfterPerPathBurst()
    {
        const QString tag = QStringLiteral("test-owner");

        QHash<QString, Profile> initial;
        initial.insert(QStringLiteral("a.one"), Profile{});
        Profile withDuration;
        withDuration.duration = 200.0;
        initial.insert(QStringLiteral("a.two"), withDuration);

        QSignalSpy changedSpy(&m_registry, &PhosphorProfileRegistry::profileChanged);
        QSignalSpy reloadedSpy(&m_registry, &PhosphorProfileRegistry::ownerReloaded);
        QSignalSpy bulkSpy(&m_registry, &PhosphorProfileRegistry::profilesReloaded);

        m_registry.reloadFromOwner(tag, initial);

        QCOMPARE(changedSpy.count(), 2); // one per path that changed
        QCOMPARE(reloadedSpy.count(), 1); // one batch boundary signal
        QCOMPARE(reloadedSpy.first().at(0).toString(), tag);
        QCOMPARE(bulkSpy.count(), 0); // profilesReloaded is wholesale-only

        // No-op reload (same content): zero signals — including ownerReloaded.
        changedSpy.clear();
        reloadedSpy.clear();
        m_registry.reloadFromOwner(tag, initial);
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(reloadedSpy.count(), 0);

        // clearOwner also fires ownerReloaded after the per-path removals.
        m_registry.reloadFromOwner(tag, initial); // seed
        reloadedSpy.clear();
        changedSpy.clear();
        m_registry.clearOwner(tag);
        QCOMPARE(changedSpy.count(), 2); // one per removed path
        QCOMPARE(reloadedSpy.count(), 1);
        QCOMPARE(reloadedSpy.first().at(0).toString(), tag);
    }
};

QTEST_MAIN(TestPhosphorProfileRegistry)
#include "test_phosphorprofileregistry.moc"
