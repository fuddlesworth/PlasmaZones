// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_profile_store_sync.cpp
 * @brief How AnimationsPageController's inheritance resolution stays honest
 *        against the process-wide PhosphorProfileRegistry.
 *
 * This file used to be much longer, and about a hazard that no longer exists.
 * Per-event timing overrides were loose JSON files, the registry was fed by a
 * debounced watcher on the very directory the controller wrote, and every
 * removal path owed an explicit rescan or the registry kept serving a deleted
 * file's value. Schema v8 moved those overrides into config
 * (`Animations/MotionProfileTree`), so the controller reads the same store it
 * writes and there is nothing left to race.
 *
 * What survives is the part that was never about the watcher: the registry is
 * still the fallback for levels the user has NOT overridden (the shell
 * animation family seeds), and dropping that read would silently resolve every
 * unconfigured level to a library default. The other surviving boundary is a
 * cleared override that happened to EQUAL its inherited value — nothing the
 * user can see changes, and the announcement must still fire or the card has
 * nothing to re-read on.
 *
 * Override CRUD, the dirty-state announcements and the traversal gate live in
 * `test_animations_page_controller.cpp`; the suppression mirror in
 * `test_animations_suppression_mirror.cpp`; the QML contracts in
 * `test_animations_qml_contracts.cpp`.
 */

#include <QSignalSpy>
#include <QTest>

#include <QScopeGuard>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"

using namespace PlasmaZones;

class TestAnimationsProfileStoreSync : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every slot below publishes a stack-local registry as the PROCESS-WIDE
    /// default. Catch a leaked publish at its source rather than as a mystery
    /// failure in whichever slot happens to run next.
    void init()
    {
        QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

    /// A write is visible to the very next read the controller serves, and a
    /// clear falls through to the parent chain rather than to whatever the
    /// registry happens to be holding for that leaf.
    void mutationsAreVisibleToTheNextResolve()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 777}}));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 777);

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 888}}));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 888);

        // Revert the leaf: it must fall through to the parent's override.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 123}}));
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 123);
    }

    /// The registry read is the fallback for every level the user has not
    /// overridden — in the running app, the shell animation family seeds.
    /// Dropping it would resolve those levels to a library default while the
    /// daemon animates at the seeded value, which is the class of "the two
    /// disagree on one screen" bug this resolution exists to prevent.
    void resolvedProfileStillReadsTheRegistryWhereNoOverrideExists()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        PhosphorAnimation::PhosphorProfileRegistry registry;
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&registry);
        const auto unpublish = qScopeGuard([]() {
            PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        });

        PhosphorAnimation::Profile seeded;
        seeded.duration = 321;
        registry.registerProfile(QStringLiteral("editor"), seeded);

        // No override anywhere on the chain, so the leaf inherits the seed.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 321);

        // An override at the leaf outranks it.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 50}}));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 50);
    }

    /// Clearing an override whose value equalled the inherited one changes no
    /// resolved number, so the only evidence the user's card has that anything
    /// happened is the signal. It must fire regardless.
    void clearingAnOverrideEqualToTheInheritedValueStillAnnouncesIt()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}}));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("editor.snapIn"));
        // The resolved value is unchanged, which is exactly why the signal is
        // the only thing carrying the news.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 200);
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
    }
};

QTEST_MAIN(TestAnimationsProfileStoreSync)
#include "test_animations_profile_store_sync.moc"
