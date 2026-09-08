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
    /// comment in test_profileloader.cpp. The low-precedence tag is
    /// reset here too: clear() deliberately preserves it (it's
    /// configuration, not data), so a test that sets the tag and then
    /// aborts on a failed assertion would otherwise leak it into every
    /// subsequent test.
    void cleanup()
    {
        m_registry.clear();
        m_registry.setLowPrecedenceOwnerTag(QString());
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

    /// snapshotExcludingLowPrecedence must omit seed-owned entries so a
    /// publisher flattening the registry into a ProfileTree cannot promote
    /// low-precedence family defaults into overrides that outrank the
    /// consumer's own baseline profile (a `window` seed shipped as an
    /// override pins every window leg's duration, turning the user's
    /// global animation settings into a no-op — the #795 regression).
    void testSnapshotExcludingLowPrecedenceOmitsSeeds()
    {
        const QString seedTag = QStringLiteral("family-seeds");

        Profile seed;
        seed.duration = 200.0;
        m_registry.registerProfile(QStringLiteral("window"), seed, seedTag);

        Profile userProfile;
        userProfile.duration = 2000.0;
        m_registry.registerProfile(QStringLiteral("window.movement.placeIn"), userProfile,
                                   QStringLiteral("user-files"));
        m_registry.registerProfile(QStringLiteral("Global"), Profile{});

        // No tag configured: identical to snapshot().
        QCOMPARE(m_registry.snapshotExcludingLowPrecedence().size(), 3);

        m_registry.setLowPrecedenceOwnerTag(seedTag);
        const auto filtered = m_registry.snapshotExcludingLowPrecedence();
        QCOMPARE(filtered.size(), 2);
        QVERIFY(!filtered.contains(QStringLiteral("window")));
        QVERIFY(filtered.contains(QStringLiteral("window.movement.placeIn")));
        QVERIFY(filtered.contains(QStringLiteral("Global")));

        // Seeds are NOT in either snapshot: they live in their own layer, so
        // the exclusion is structural rather than a filter. Both accessors
        // answer with the non-seed entries only.
        QCOMPARE(m_registry.snapshot().size(), 2);

        // But they are still registered content — `resolve` and `hasProfile`
        // answer across both layers, or a registry whose entire content is
        // seeds (the shell tier's) would look empty.
        QVERIFY(m_registry.hasProfile(QStringLiteral("window")));
        const auto seedEntry = m_registry.resolve(QStringLiteral("window"));
        QVERIFY(seedEntry.has_value());
        QCOMPARE(seedEntry->duration.value_or(0.0), 200.0);
    }

    /// `registerProfile`'s SEED BRANCH, driven directly.
    ///
    /// The slot above sets the low-precedence tag AFTER registering, so what it
    /// actually exercises is the migration loop inside setLowPrecedenceOwnerTag
    /// — invert the seed branch and it still passes, because the migration puts
    /// the entry back where the assertions expect it. This one sets the tag
    /// first, which is the ordering every composition root uses, so the branch
    /// itself decides where the entry lands.
    ///
    /// It matters more than a coverage gap: that branch is where a seed-tagged
    /// write silently leaves an untagged entry standing at the same path, which
    /// is what let a cleared global profile keep outranking every family seed
    /// for the rest of a session.
    void testSeedBranchPlacesSeedsInTheirOwnLayerWhenTheTagIsSetFirst()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile seed;
        seed.duration = 200.0;
        m_registry.registerProfile(QStringLiteral("window"), seed, seedTag);

        // Landed in the seed layer, not the upper one. Inverting the branch
        // fails here rather than anywhere downstream.
        QVERIFY(!m_registry.snapshot().contains(QStringLiteral("window")));
        QVERIFY(m_registry.snapshotExcludingLowPrecedence().isEmpty());
        QVERIFY(m_registry.hasProfile(QStringLiteral("window")));
        QCOMPARE(m_registry.resolve(QStringLiteral("window"))->duration.value_or(0.0), 200.0);

        // And a byte-identical UNTAGGED write at the same path is a separate
        // entry in the upper layer rather than an update of the seed — the two
        // stores coexist, which is the property the layer split exists for.
        m_registry.registerProfile(QStringLiteral("window"), seed);
        QVERIFY(m_registry.snapshot().contains(QStringLiteral("window")));
        QVERIFY(m_registry.hasProfile(QStringLiteral("window")));
    }

    /// `unregisterProfile` removes from the non-seed layer ONLY, and a
    /// seed-only path is therefore untouched by it.
    ///
    /// That asymmetry is the mechanism the composition roots rely on when they
    /// move the global profile between layers: the caller drops its own
    /// untagged entry and registers under the seed tag, and this is what makes
    /// the drop hit the right one. A seed-aware unregisterProfile would delete
    /// the layer the move is aiming at.
    void testUnregisterLeavesASeedOnlyPathAlone()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile seed;
        seed.duration = 200.0;
        m_registry.registerProfile(QStringLiteral("widget"), seed, seedTag);

        m_registry.unregisterProfile(QStringLiteral("widget"));

        QVERIFY2(m_registry.hasProfile(QStringLiteral("widget")),
                 "unregisterProfile removed a seed; clearing an override would then drop to library defaults "
                 "instead of revealing the seed underneath");
        QCOMPARE(m_registry.resolve(QStringLiteral("widget"))->duration.value_or(0.0), 200.0);

        // And with an override on top, unregister reveals the seed rather than
        // emptying the path — the reveal the two stores exist for.
        Profile override;
        override.duration = 900.0;
        m_registry.registerProfile(QStringLiteral("widget"), override);
        QCOMPARE(m_registry.resolve(QStringLiteral("widget"))->duration.value_or(0.0), 900.0);
        m_registry.unregisterProfile(QStringLiteral("widget"));
        QCOMPARE(m_registry.resolve(QStringLiteral("widget"))->duration.value_or(0.0), 200.0);
    }

    /// A user override at a seeded path must WIN without destroying the seed,
    /// and clearing it must reveal the seed again.
    ///
    /// This is the regression guard for the whole two-layer storage change.
    /// Before it the registry held one slot per path, so the override
    /// overwrote the seed outright and the later clear removed the path
    /// entirely — dropping to library defaults for the rest of the session,
    /// with nothing short of a restart to re-seed.
    void testAnOverrideAtASeededPathDoesNotDestroyTheSeed()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile seed;
        seed.duration = 150.0;
        m_registry.registerProfile(QStringLiteral("window.appearance.close"), seed, seedTag);

        Profile override;
        override.duration = 900.0;
        m_registry.reloadFromOwner(QStringLiteral("tree"), {{QStringLiteral("window.appearance.close"), override}});
        QCOMPARE(m_registry.resolveWithInheritance(QStringLiteral("window.appearance.close")).effectiveDuration(),
                 900.0);

        // The user clears it: the whole partition is replaced with an empty map.
        m_registry.reloadFromOwner(QStringLiteral("tree"), {});
        QCOMPARE(m_registry.resolveWithInheritance(QStringLiteral("window.appearance.close")).effectiveDuration(),
                 150.0);
    }

    /// A user override at a SHALLOWER path beats a seed at a deeper one.
    ///
    /// The reason `resolveWithInheritance` is two passes at all: within one
    /// layer the deeper entry wins, but across layers every user entry
    /// outranks every seed regardless of depth. A single-pass deeper-wins walk
    /// answers 500 here.
    void testAParentOverrideBeatsALeafSeed()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile leafSeed;
        leafSeed.duration = 500.0;
        m_registry.registerProfile(QStringLiteral("widget.pulse.fast"), leafSeed, seedTag);

        Profile parentOverride;
        parentOverride.duration = 800.0;
        m_registry.registerProfile(QStringLiteral("widget"), parentOverride, QStringLiteral("tree"));

        QCOMPARE(m_registry.resolveWithInheritance(QStringLiteral("widget.pulse.fast")).effectiveDuration(), 800.0);
    }

    /// `clear()` must empty the seed layer too.
    ///
    /// It is the test fixture's own reset, so a seed surviving it would leak
    /// into every later slot in this file and make their assertions depend on
    /// execution order.
    void testClearWipesTheSeedLayer()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile seed;
        seed.duration = 150.0;
        m_registry.registerProfile(QStringLiteral("window"), seed, seedTag);
        QVERIFY(m_registry.hasProfile(QStringLiteral("window")));

        m_registry.clear();
        QVERIFY(!m_registry.hasProfile(QStringLiteral("window")));
        QVERIFY(!m_registry.resolve(QStringLiteral("window")).has_value());
    }

    /// `clearOwner(seedTag)` must actually clear seeds.
    ///
    /// Seeds are not in the owner map, so a loop over it finds nothing for the
    /// seed tag and the call would be a silent no-op.
    void testClearOwnerRemovesSeeds()
    {
        const QString seedTag = QStringLiteral("family-seeds");
        m_registry.setLowPrecedenceOwnerTag(seedTag);

        Profile seed;
        seed.duration = 150.0;
        m_registry.registerProfile(QStringLiteral("window"), seed, seedTag);

        m_registry.clearOwner(seedTag);
        QVERIFY(!m_registry.hasProfile(QStringLiteral("window")));
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
