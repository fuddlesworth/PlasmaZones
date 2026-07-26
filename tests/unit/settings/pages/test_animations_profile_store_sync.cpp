// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_profile_store_sync.cpp
 * @brief How AnimationsPageController's inheritance resolution stays honest
 *        against the process-wide PhosphorProfileRegistry.
 *
 * The settings app resolves against a registry fed by a `ProfileLoader`
 * watching the very directory the controller writes, and that watch is
 * debounced with no rescan signal reaching the open page. Everything the
 * controller mutates therefore has to be visible to the NEXT read it serves,
 * without waiting for the watch. A write is covered by reading the user files
 * ahead of the registry; a delete has nothing left on disk to outrank the
 * stale entry, so the controller forces the loader to catch up first. These
 * slots pin both halves, every removal call site that owes a refresh, the
 * inverse regression of dropping the registry read altogether, and the
 * boundary where the cleared override happened to equal the inherited value
 * (the announcement must still fire, or the UI has nothing to re-read on).
 *
 * Split out of `test_animations_page_controller.cpp`, which owns path
 * discovery, override CRUD, the dirty-state announcements, the traversal
 * gate, and the suppression mirror. The QML↔controller contracts live in
 * `test_animations_qml_contracts.cpp`.
 *
 * Every slot redirects override-file I/O into a tmpdir via
 * `setUserProfilesDirOverride()`, so the real user XDG dirs are never touched.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;

namespace {

/// Rewrite an override file's duration the way an EXTERNAL writer would: a
/// hand-edit in the profiles directory, behind the controller's back and with
/// none of its cache bookkeeping. Reads the file the controller itself wrote,
/// so the envelope keeps exactly the shape it produces.
///
/// Parsed and re-serialised rather than text-substituted. A substitution would
/// be unanchored and global, so it would also rewrite the same digits appearing
/// in `minDistance`, in a preset name, or in any field added later, and would
/// silently corrupt the fixture rather than failing.
[[nodiscard]] bool rewriteDurationOnDisk(const QString& filePath, int expectedFrom, int to)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;
    QJsonObject root = doc.object();
    // Checked, so a fixture whose duration moved fails here rather than
    // rewriting a value the test did not expect to be there.
    if (root.value(QLatin1String("duration")).toInt(-1) != expectedFrom)
        return false;
    root.insert(QLatin1String("duration"), to);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(QJsonDocument(root).toJson()) > 0;
}

} // namespace

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

    /// The settings app resolves against a registry fed by a ProfileLoader
    /// watching the very directory this controller writes, and that watch is
    /// debounced by 50 ms with no rescan signal reaching the open page.
    /// Everything the controller mutates therefore has to be visible to the
    /// NEXT read it serves, without waiting for the watch:
    ///
    ///   * a write is covered by reading the user files ahead of the registry
    ///   * a delete has nothing left on disk to outrank the stale entry, so
    ///     the controller forces the loader to catch up first
    ///
    /// The delete half is the reported bug: the revert link removed the
    /// file, the registry kept serving the removed duration, and the control
    /// did not move until the settings app was restarted.
    void mutationsAreVisibleToTheNextResolveDespiteTheDebouncedWatch()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        PhosphorAnimation::CurveRegistry curves;
        PhosphorAnimation::PhosphorProfileRegistry registry;
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&registry);
        // Unpublish before the registry leaves scope: the static default is
        // process-wide and a dangling pointer would outlive this test.
        const auto unpublish = qScopeGuard([]() {
            PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        });
        PhosphorAnimation::ProfileLoader loader(registry, curves, QStringLiteral("test-user-profiles"));
        // LiveReload::Off deliberately: with no watcher, the injected refresher
        // is the ONLY route to a current registry, so an assertion below cannot
        // be satisfied by the registry quietly catching up on its own.
        // `rescanNow()` works independently of the watcher, so nothing is lost.
        loader.loadFromDirectory(tmp.path(), PhosphorAnimation::LiveReload::Off);
        // What main.cpp injects.
        c.setProfileStoreRefresher([&loader]() {
            loader.rescanNow();
        });

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 777}}));
        // Stand in for the debounce landing: from here the registry holds 777
        // and knows nothing of what follows until it is told. Unwrapped step by
        // step — a bare .value().duration.value() would throw out of the test
        // binary on a regression instead of failing with a readable diff.
        loader.rescanNow();
        const auto seeded = registry.resolve(QStringLiteral("editor.snapIn"));
        QVERIFY(seeded.has_value());
        QVERIFY(seeded->duration.has_value());
        QCOMPARE(seeded->duration.value(), 777);

        // A second write lands on disk while the registry still holds 777.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 888}}));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 888);

        // Revert the leaf: the file goes, so the leaf must fall through to
        // the parent chain rather than to the entry the registry was holding.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 123}}));
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 123);
    }

    /// clearOverride is not the only path that removes an override file: the
    /// scoped discard, the bulk clears, and BOTH global discards (the
    /// synchronous one and the async worker the UI actually dispatches) all
    /// do, and each carries the same hazard — the file is gone, so nothing on
    /// disk outranks the entry the registry is still holding. Each leg below
    /// is a distinct `refreshProfileStore()` call site, seeded with its own
    /// stale registry value; deleting any one of the four fails this test.
    void everyBatchRemovalPathRefreshesTheProfileStore()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        PhosphorAnimation::CurveRegistry curves;
        PhosphorAnimation::PhosphorProfileRegistry registry;
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&registry);
        const auto unpublish = qScopeGuard([]() {
            PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        });
        PhosphorAnimation::ProfileLoader loader(registry, curves, QStringLiteral("test-user-profiles"));
        // LiveReload::Off deliberately. With the watcher armed, the 5 s event-loop
        // spin in leg 4 below gives its 50 ms debounce ample room to land, and a
        // registry that caught up on its own would satisfy the post-loop
        // assertions even with the refresh under test deleted. `rescanNow()` is
        // documented to work independently of the watcher, so the injected
        // refresher stays the ONLY route to a current registry and every leg
        // stays mutation-sensitive.
        loader.loadFromDirectory(tmp.path(), PhosphorAnimation::LiveReload::Off);
        c.setProfileStoreRefresher([&loader]() {
            loader.rescanNow();
        });

        // The parent holds the value each leg must fall back to.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 123}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 777}}));
        loader.rescanNow();

        // Leg 1 — scoped discard (the per-page kebab Discard). The leaf file
        // did not exist before this session, so reverting removes it.
        QVERIFY(c.revertPendingUnder(QStringList{QStringLiteral("editor.snapIn")}));
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 123);

        // Leg 2 — bulk clear (the page Reset buttons), which deletes every
        // in-scope file and refreshes once for the batch.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 888}}));
        loader.rescanNow();
        QCOMPARE(c.clearOverridesUnder(QStringList{QStringLiteral("editor.snapIn")}), 1);
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 123);

        // Leg 3 — synchronous global discard, which takes the parent with it,
        // so the leaf falls all the way through to the library default.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 999}}));
        loader.rescanNow();
        QVERIFY(c.revertPending());
        QVERIFY(!c.hasOverride(QStringLiteral("editor")));
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toDouble(),
                 PhosphorAnimation::Profile::DefaultDuration);

        // Leg 4 — the ASYNC discard, which is what StagingDomain::discard()
        // dispatches and therefore the path the UI actually takes. It restores
        // off the GUI thread, so its refresh lives in the finished handler and
        // is a separate call site from leg 3's.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 456}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 654}}));
        loader.rescanNow();
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 654);

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        c.asyncRevertPending();
        // QTRY, not spy.wait(): asyncRevertPending emits discardResult
        // SYNCHRONOUSLY on two early-return branches (a worker already in
        // flight, or nothing pending), and wait() ignores an emission already
        // recorded before it was called. If either branch were ever taken,
        // wait() would block the full timeout and then fail for the wrong
        // reason.
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(!c.hasOverride(QStringLiteral("editor")));
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toDouble(),
                 PhosphorAnimation::Profile::DefaultDuration);
    }

    /// The registry is still the source for every level this controller does
    /// NOT write, which in the settings app means bundled and system-dir
    /// profiles. Dropping it in favour of the disk read would have silenced
    /// them.
    void resolvedProfile_stillReadsTheRegistryWhereNoUserFileExists()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        PhosphorAnimation::PhosphorProfileRegistry registry;
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&registry);
        const auto unpublish = qScopeGuard([]() {
            PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        });

        PhosphorAnimation::Profile bundled;
        bundled.duration = 456;
        registry.registerProfile(QStringLiteral("editor"), bundled);

        // No user file anywhere on the chain: the parent's registry entry
        // reaches the leaf.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 456);
    }

    /// Clearing an override whose value HAPPENS TO EQUAL the inherited one must
    /// still emit `overrideChanged`. The QML re-read hangs off that signal, so
    /// if the emit were ever gated on the resolved value changing — the shape
    /// the project's "only emit when the value actually changed" rule invites —
    /// the revert link would go dead for exactly the case where the user cannot
    /// tell from the number whether it worked. `clearOverride` emits
    /// unconditionally on a successful removal; this pins that.
    void clearingAnOverrideEqualToTheInheritedValueStillAnnouncesIt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Parent and leaf carry the SAME duration, so clearing the leaf moves
        // the resolved value not at all.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 321}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 321}}));
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 321);

        QSignalSpy announced(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));

        QCOMPARE(announced.count(), 1);
        QCOMPARE(announced.first().at(0).toString(), QStringLiteral("editor.snapIn"));
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        // Same number, now inherited rather than owned.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 321);
    }

    /// The OTHER writer's entry point. `resolvedProfile` memoises what it read
    /// off disk, and every mutation the controller performs drops that memo
    /// itself — but the profiles directory is a filesystem boundary the user
    /// can hand-edit, and the page even offers to open it. `main.cpp` wires
    /// `ProfileLoader::profilesChanged` to `forgetCachedOverrideFiles` for
    /// exactly that case. Both halves are pinned: the memo goes, and the
    /// tree-wide reload broadcast fires with an EMPTY path so an already-open
    /// card re-reads rather than only the next one to be built.
    void forgetCachedOverrideFilesDropsTheMemoAndBroadcasts()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 500}}));
        // Reading it is what populates the memo.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 500);

        QVERIFY(rewriteDurationOnDisk(tmp.path() + QStringLiteral("/editor.snapIn.json"), 500, 640));
        // Still the memo — nothing has told the controller the file moved.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 500);

        QSignalSpy announced(&c, &AnimationsPageController::overrideChanged);
        c.forgetCachedOverrideFiles();

        QCOMPARE(announced.count(), 1);
        QVERIFY2(announced.first().at(0).toString().isEmpty(),
                 "the external-writer broadcast must carry an EMPTY path — consumers key their tree-wide reload on it");
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 640);
    }

    /// The loader's `rescanNow()` emits `profilesChanged` synchronously, so a
    /// controller-driven refresh re-enters `forgetCachedOverrideFiles` on its
    /// own stack. The broadcast is suppressed for that window, because the
    /// caller is already about to send the precise per-path signals — without
    /// the suppression every removal answers itself with a tree-wide reload.
    ///
    /// The memo drop is NOT suppressed, and that half matters just as much: it
    /// is what stops a future "simplification" from moving the early return
    /// above the invalidation and quietly serving stale values again.
    void forgetCachedOverrideFilesStaysSilentDuringOurOwnRescan()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 123}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 777}}));
        // Memoise the parent so the invalidation below has something to drop.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor")).value(QStringLiteral("duration")).toInt(), 123);

        // Stands in for ProfileLoader::profilesChanged firing out of
        // rescanNow(), with an external edit landing in the same window.
        bool rewrote = false;
        c.setProfileStoreRefresher([&]() {
            if (rewrote) {
                c.forgetCachedOverrideFiles();
                return;
            }
            // Re-read BEFORE the out-of-band write, so the memo holds the
            // pre-edit value when forgetCachedOverrideFiles runs. Without this
            // the memo is already empty at that point (refreshProfileStore
            // invalidates unconditionally on its way in), so moving the
            // suppression early-return ABOVE the invalidation would be
            // invisible — which is precisely the regression the assertion
            // below claims to prevent.
            (void)c.resolvedProfile(QStringLiteral("editor"));
            rewrote = rewriteDurationOnDisk(tmp.path() + QStringLiteral("/editor.json"), 123, 456);
            c.forgetCachedOverrideFiles();
        });

        QSignalSpy announced(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));
        QVERIFY(rewrote);

        QCOMPARE(announced.count(), 1);
        QCOMPARE(announced.first().at(0).toString(), QStringLiteral("editor.snapIn"));
        // Suppressed, but not skipped: the out-of-band write is visible.
        QCOMPARE(c.resolvedProfile(QStringLiteral("editor")).value(QStringLiteral("duration")).toInt(), 456);

        // The depth guard unwound, so an ordinary external notification
        // broadcasts again.
        QSignalSpy later(&c, &AnimationsPageController::overrideChanged);
        c.forgetCachedOverrideFiles();
        QCOMPARE(later.count(), 1);
        QVERIFY(later.first().at(0).toString().isEmpty());
    }
};

QTEST_MAIN(TestAnimationsProfileStoreSync)
#include "test_animations_profile_store_sync.moc"
