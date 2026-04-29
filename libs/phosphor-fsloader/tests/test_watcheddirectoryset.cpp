// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Direct coverage for WatchedDirectorySet using a hand-rolled fake
// strategy. The DirectoryLoader test suite exercises the same
// scaffolding through the JSON specialisation; this file pins the
// contract in isolation so a refactor that breaks the strategy contract
// surfaces here, not in a downstream test.

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorFsLoader;

namespace {

/// Records every `performScan` call, returning the file paths the
/// caller registered as "currently relevant" via `setReportedFiles`.
class RecordingStrategy : public IScanStrategy
{
public:
    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        ++scanCount;
        lastDirectories = directoriesInScanOrder;
        return reportedFiles;
    }

    void setReportedFiles(const QStringList& paths)
    {
        reportedFiles = paths;
    }

    int scanCount = 0;
    QStringList lastDirectories;
    QStringList reportedFiles;
};

void touchFile(const QString& path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("x");
}

} // namespace

class TestWatchedDirectorySet : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_tmp.reset(new QTemporaryDir);
        QVERIFY(m_tmp->isValid());
    }

    /// Registering directories runs the strategy synchronously and
    /// passes the registration order through verbatim.
    void testRegistration_runsScanInOrder()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        QVERIFY(a.isValid() && b.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectories({a.path(), b.path()}, LiveReload::Off);

        QCOMPARE(strategy.scanCount, 1);
        QCOMPARE(strategy.lastDirectories.size(), 2);
        QCOMPARE(strategy.lastDirectories.at(0), QDir::cleanPath(a.path()));
        QCOMPARE(strategy.lastDirectories.at(1), QDir::cleanPath(b.path()));
    }

    /// Idempotency: registering the same directory twice is a no-op
    /// after the first call (still triggers a rescan because
    /// registerDirectory always runs the strategy, but the directory
    /// list does not grow).
    void testRegistration_idempotent()
    {
        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectory(m_tmp->path(), LiveReload::Off);
        set.registerDirectory(m_tmp->path(), LiveReload::Off);

        QCOMPARE(set.directories().size(), 1);
        QCOMPARE(strategy.scanCount, 2);
    }

    /// `RegistrationOrder::HighestPriorityFirst` reverses the input
    /// before storing so the strategy always sees the canonical
    /// `[lowest, ..., highest]` shape — this is the load-bearing
    /// contract that lets every in-tree strategy reverse-iterate
    /// first-wins without negotiating order from comments. A future
    /// refactor that drops the normalisation here would silently invert
    /// every override on `locateAll`-shaped input.
    void testRegistrationOrder_highestPriorityFirstReversesToCanonical()
    {
        QTemporaryDir userDir;
        QTemporaryDir sysHigh;
        QTemporaryDir sysLow;
        QVERIFY(userDir.isValid() && sysHigh.isValid() && sysLow.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        // Caller passes input in the natural `locateAll`-after-prepend
        // order: user wins, then system dirs in descending priority.
        set.registerDirectories({userDir.path(), sysHigh.path(), sysLow.path()}, LiveReload::Off,
                                RegistrationOrder::HighestPriorityFirst);

        // The strategy must see the reversed (canonical) shape: lowest
        // priority first, user last.
        QCOMPARE(strategy.lastDirectories.size(), 3);
        QCOMPARE(strategy.lastDirectories.at(0), QDir::cleanPath(sysLow.path()));
        QCOMPARE(strategy.lastDirectories.at(1), QDir::cleanPath(sysHigh.path()));
        QCOMPARE(strategy.lastDirectories.at(2), QDir::cleanPath(userDir.path()));
        // `directories()` reflects the stored (canonical) form too —
        // consumers reading this back never observe the caller's
        // pre-normalisation order.
        QCOMPARE(set.directories(), strategy.lastDirectories);
    }

    /// `RegistrationOrder::LowestPriorityFirst` (the default) is a
    /// pass-through. Pinned so a future "always reverse" refactor in
    /// the base would surface here, not in downstream consumers.
    void testRegistrationOrder_lowestPriorityFirstIsPassThrough()
    {
        QTemporaryDir sysLow;
        QTemporaryDir sysHigh;
        QTemporaryDir userDir;
        QVERIFY(sysLow.isValid() && sysHigh.isValid() && userDir.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectories({sysLow.path(), sysHigh.path(), userDir.path()}, LiveReload::Off,
                                RegistrationOrder::LowestPriorityFirst);

        QCOMPARE(strategy.lastDirectories.size(), 3);
        QCOMPARE(strategy.lastDirectories.at(0), QDir::cleanPath(sysLow.path()));
        QCOMPARE(strategy.lastDirectories.at(1), QDir::cleanPath(sysHigh.path()));
        QCOMPARE(strategy.lastDirectories.at(2), QDir::cleanPath(userDir.path()));
    }

    /// `setDirectories` honours `RegistrationOrder` the same way
    /// `registerDirectories` does — full-replacement callers (e.g.
    /// `ScriptedAlgorithmLoader::scanAndRegister`) get the same
    /// normalisation guarantee as append-only ones.
    void testSetDirectories_honoursRegistrationOrder()
    {
        QTemporaryDir userDir;
        QTemporaryDir sysHigh;
        QTemporaryDir sysLow;
        QVERIFY(userDir.isValid() && sysHigh.isValid() && sysLow.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDirectories({userDir.path(), sysHigh.path(), sysLow.path()}, LiveReload::Off,
                           RegistrationOrder::HighestPriorityFirst);

        QCOMPARE(strategy.lastDirectories.size(), 3);
        QCOMPARE(strategy.lastDirectories.at(0), QDir::cleanPath(sysLow.path()));
        QCOMPARE(strategy.lastDirectories.at(1), QDir::cleanPath(sysHigh.path()));
        QCOMPARE(strategy.lastDirectories.at(2), QDir::cleanPath(userDir.path()));
    }

    /// Live-reload watcher fires the strategy on filesystem edits, the
    /// debounce coalesces a burst into one scan.
    void testLiveReload_coalescesBurst()
    {
        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(m_tmp->path(), LiveReload::On);
        const int baseline = strategy.scanCount;

        for (int i = 0; i < 3; ++i) {
            touchFile(m_tmp->filePath(QStringLiteral("burst-%1.txt").arg(i)));
        }

        QSignalSpy spy(&set, &WatchedDirectorySet::rescanCompleted);
        QVERIFY(spy.wait(500));
        QCOMPARE(strategy.scanCount, baseline + 1);
    }

    /// requestRescan delivered DURING a scan (synchronously, while
    /// performScan is on the stack) is replayed at the end of the
    /// running scan rather than being silently dropped. This is the
    /// in-flight race-guard path: m_rescanInProgress is true at the
    /// time of the call, so requestRescan must set the
    /// m_rescanRequestedWhileRunning flag and rescanAll must re-arm
    /// the debounce timer after returning. Without the guard, the
    /// debounce timer is idle while rescanAll runs, m_debounceTimer.start()
    /// is a no-op for the inner call, and the second scan never fires.
    void testRescanRace_requestDuringScan()
    {
        class ReentrantStrategy : public IScanStrategy
        {
        public:
            WatchedDirectorySet* set = nullptr;
            int scanCount = 0;
            bool reentered = false;
            QStringList performScan(const QStringList&) override
            {
                ++scanCount;
                if (!reentered && set) {
                    reentered = true;
                    // Synchronous re-entry — the call lands while the
                    // outer rescanAll is still on the stack and
                    // m_rescanInProgress == true. Exercises the
                    // race-guard branch (must set the deferred flag,
                    // not start the debounce timer directly).
                    set->requestRescan();
                }
                return {};
            }
        };

        ReentrantStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        strategy.set = &set;
        set.registerDirectory(m_tmp->path(), LiveReload::Off);

        // Initial registration ran scan #1; the synchronous
        // requestRescan inside it triggered the race guard, which
        // re-arms the debounce timer at the end of rescanAll. Wait for
        // scan #2 to fire from that re-armed timer.
        QTRY_COMPARE_WITH_TIMEOUT(strategy.scanCount, 2, 1000);
    }

    /// Strategies that report "currently relevant" file paths get
    /// per-file watches re-armed on every rescan. Verified by
    /// observing that an in-place file edit after the first rescan
    /// still triggers a follow-up rescan (the per-file watch was
    /// armed despite the directory contents being structurally
    /// unchanged).
    void testFileWatches_armedFromStrategyReport()
    {
        const QString filePath = m_tmp->filePath(QStringLiteral("watched.txt"));
        touchFile(filePath);

        RecordingStrategy strategy;
        strategy.setReportedFiles({filePath});
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(m_tmp->path(), LiveReload::On);
        const int baseline = strategy.scanCount;

        // In-place rewrite of the existing file. Directory mtime may
        // not change on some filesystems for a same-size rewrite, but
        // the per-file watch installed from `reportedFiles` will fire.
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("yyyyy");
        f.close();

        QSignalSpy spy(&set, &WatchedDirectorySet::rescanCompleted);
        QVERIFY(spy.wait(2000));
        QVERIFY(strategy.scanCount > baseline);
    }

    /// Registering a target whose nearest existing ancestor is `$HOME`
    /// (or another forbidden root: /, GenericData/Config/Cache/Temp/
    /// Runtime locations) must NOT install a watch on that ancestor —
    /// otherwise every unrelated file operation in the user's home
    /// triggers a full rescan. The check fires both on the registered
    /// target itself AND on the climbed ancestor.
    void testForbiddenRoot_doesNotWatchHome()
    {
        // Build a path under $HOME that doesn't exist, where the climb
        // would land directly on $HOME. We can't easily mutate
        // QDir::homePath in-process portably, but a path of the form
        // `$HOME/<fresh-uuid>/never/exists` will climb up missing
        // segments and stop at $HOME — which `isForbiddenWatchRoot`
        // rejects.
        const QString home = QDir::homePath();
        QVERIFY(!home.isEmpty());
        const QString uniqueLeaf =
            QStringLiteral("phosphor-fsloader-forbid-test-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString neverExists = home + QLatin1Char('/') + uniqueLeaf + QStringLiteral("/never/exists");

        // Sanity: this path must not exist.
        QVERIFY(!QFileInfo::exists(neverExists));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectory(neverExists, LiveReload::On);

        // The strategy still ran (to produce the initial empty rescan)
        // but no parent watch was installed on $HOME.
        QCOMPARE(set.hasParentWatchForTest(home), false);
        QCOMPARE(set.watchedAncestorForTest(neverExists), QString());
    }

    /// Registering `$HOME` directly must also be refused — the climb
    /// branch and the direct-watch branch both gate on
    /// `isForbiddenWatchRoot`. Without this the comment claiming "we
    /// refuse to watch $HOME" only holds for the climb path.
    void testForbiddenRoot_directRegistrationRefused()
    {
        const QString home = QDir::homePath();
        QVERIFY(!home.isEmpty());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectory(home, LiveReload::On);

        // The strategy ran (registered dirs include $HOME — that's
        // fine, we still scan it on demand) but no direct watch was
        // installed.
        QCOMPARE(set.hasParentWatchForTest(home), false);
        // No ancestor mapping either — direct registration doesn't
        // populate `m_parentWatchFor`.
        QCOMPARE(set.watchedAncestorForTest(home), QString());
    }

    /// When `attachWatcherForDir` climbs past missing intermediate
    /// directories to land on a grandparent ancestor, then later the
    /// target materialises, the watch on the climbed ancestor must
    /// be cleaned up — and only via the `m_parentWatchFor` back-
    /// reference, not via `info.absolutePath()`. The previous
    /// implementation leaked the ancestor watch in this case;
    /// regression coverage pins it.
    void testParentWatch_grandparentCleanupOnPromotion()
    {
        // Build target = m_tmp/<uuid>/intermediate/leaf where neither
        // the uuid dir nor the intermediate dir exists. The climb
        // must land on m_tmp itself (a grandparent / further), not
        // on `intermediate` (which doesn't exist).
        const QString uniqueParent = m_tmp->filePath(QStringLiteral("uuid-pwt"));
        const QString intermediate = uniqueParent + QStringLiteral("/intermediate");
        const QString leaf = intermediate + QStringLiteral("/leaf");
        QVERIFY(!QFileInfo::exists(uniqueParent));
        QVERIFY(!QFileInfo::exists(intermediate));
        QVERIFY(!QFileInfo::exists(leaf));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(leaf, LiveReload::On);

        // The climb landed on m_tmp (the only existing ancestor).
        const QString tmp = QDir::cleanPath(m_tmp->path());
        QCOMPARE(set.hasParentWatchForTest(tmp), true);
        QCOMPARE(set.watchedAncestorForTest(leaf), tmp);

        // Materialise the full tree atomically and trigger a rescan —
        // the watcher's promotion logic must remove the m_tmp ancestor
        // watch (no other target needs it) and replace with a direct
        // watch on `leaf`.
        QVERIFY(QDir().mkpath(leaf));
        set.rescanNow();

        QCOMPARE(set.hasParentWatchForTest(tmp), false);
        QCOMPARE(set.watchedAncestorForTest(leaf), QString());
    }

    /// Two missing-sibling targets that climb to the SAME ancestor must
    /// share that ancestor watch; promoting one of them (its tree
    /// materialises) must NOT remove the ancestor watch as long as the
    /// other still relies on it. The reference-counting branch in
    /// `attachWatcherForDir` (iterates m_parentWatchFor values to check
    /// "still needed") was uncovered before.
    void testParentWatch_sharedAncestorRetainedWhilePeerStillNeedsIt()
    {
        const QString tmp = QDir::cleanPath(m_tmp->path());
        const QString siblingA = tmp + QStringLiteral("/missing-a/leaf");
        const QString siblingB = tmp + QStringLiteral("/missing-b/leaf");
        QVERIFY(!QFileInfo::exists(siblingA));
        QVERIFY(!QFileInfo::exists(siblingB));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectories({siblingA, siblingB}, LiveReload::On);

        // Both targets climb to m_tmp.
        QCOMPARE(set.hasParentWatchForTest(tmp), true);
        QCOMPARE(set.watchedAncestorForTest(siblingA), tmp);
        QCOMPARE(set.watchedAncestorForTest(siblingB), tmp);

        // Materialise sibling A only and trigger a rescan — the
        // promotion of A must not remove the m_tmp ancestor watch
        // because B still relies on it.
        QVERIFY(QDir().mkpath(siblingA));
        set.rescanNow();

        QCOMPARE(set.hasParentWatchForTest(tmp), true);
        QCOMPARE(set.watchedAncestorForTest(siblingA), QString());
        QCOMPARE(set.watchedAncestorForTest(siblingB), tmp);

        // Now materialise sibling B too — the ancestor watch can finally
        // be released (no peer needs it).
        QVERIFY(QDir().mkpath(siblingB));
        set.rescanNow();

        QCOMPARE(set.hasParentWatchForTest(tmp), false);
        QCOMPARE(set.watchedAncestorForTest(siblingB), QString());
    }

    /// `LiveReload::On` is documented as a one-way enable: a subsequent
    /// `Off` must NOT disarm the watcher. After flipping on, edits keep
    /// firing rescans even when the second registration passes Off.
    void testLiveReload_oneWayEnable()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        QVERIFY(a.isValid() && b.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(a.path(), LiveReload::On);
        // Second registration with Off — must not disarm watching.
        set.registerDirectory(b.path(), LiveReload::Off);
        const int baseline = strategy.scanCount;

        // An edit in dir A should still fire a rescan because the
        // watcher remains armed for A.
        touchFile(a.filePath(QStringLiteral("after-off.txt")));

        QSignalSpy spy(&set, &WatchedDirectorySet::rescanCompleted);
        QVERIFY(spy.wait(500));
        QVERIFY(strategy.scanCount > baseline);
    }

    /// A directory that's directly watched, then deleted, then recreated
    /// must recover live-reload. On rmdir, `QFileSystemWatcher` auto-drops
    /// the direct watch — the next rescan must re-attach (climbing onto
    /// an ancestor since the target is gone). On mkdir-recreate, the
    /// ancestor watch fires, and the next rescan promotes back to a
    /// direct watch on the now-existing target.
    void testDirectWatch_deleteThenRecreateRecovers()
    {
        // Build under m_tmp/leaf so the climb on rmdir lands on m_tmp
        // (a non-forbidden ancestor — m_tmp itself is under
        // QStandardPaths::TempLocation but the per-test subdir created
        // by QTemporaryDir is NOT TempLocation itself, so the climb
        // stops cleanly there).
        const QString tmp = QDir::cleanPath(m_tmp->path());
        const QString target = tmp + QStringLiteral("/leaf");
        QVERIFY(QDir().mkpath(target));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(target, LiveReload::On);

        // Initial state: target exists → direct watch installed, no
        // ancestor back-reference.
        QCOMPARE(set.watchedAncestorForTest(target), QString());

        // rmdir the target. The watcher's directoryChanged on m_tmp
        // (the parent) eventually delivers — we don't depend on that
        // path though; an explicit rescanNow() exercises the recovery
        // logic deterministically.
        QVERIFY(QDir(target).removeRecursively());
        QVERIFY(!QFileInfo::exists(target));
        set.rescanNow();

        // Climb landed on m_tmp; back-reference recorded.
        QCOMPARE(set.watchedAncestorForTest(target), tmp);
        QVERIFY(set.hasParentWatchForTest(tmp));

        // Recreate the dir + rescan. Promotion to direct watch must
        // remove the ancestor back-reference.
        QVERIFY(QDir().mkpath(target));
        set.rescanNow();
        QCOMPARE(set.watchedAncestorForTest(target), QString());
        QCOMPARE(set.hasParentWatchForTest(tmp), false);
    }

    /// `setDirectories` is a full replacement: dirs in the prior set but
    /// not in the new set get dropped (with their direct watches
    /// removed), dirs in both are preserved untouched, dirs new to the
    /// set are appended. The strategy sees the post-replacement order on
    /// the synchronous rescan that follows.
    void testSetDirectories_replacesAddsAndDrops()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        QTemporaryDir c;
        QVERIFY(a.isValid() && b.isValid() && c.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectories({a.path(), b.path()}, LiveReload::Off);
        QCOMPARE(set.directories().size(), 2);

        // Replace {a, b} with {b, c} — `a` is dropped, `b` survives,
        // `c` is appended.
        set.setDirectories({b.path(), c.path()}, LiveReload::Off);

        const QStringList listed = set.directories();
        QCOMPARE(listed.size(), 2);
        QCOMPARE(listed.at(0), QDir::cleanPath(b.path()));
        QCOMPARE(listed.at(1), QDir::cleanPath(c.path()));
        // The strategy's last-seen scan order matches the post-replacement
        // directory list — `a` does not show up.
        QCOMPARE(strategy.lastDirectories.size(), 2);
        QCOMPARE(strategy.lastDirectories.at(0), QDir::cleanPath(b.path()));
        QCOMPARE(strategy.lastDirectories.at(1), QDir::cleanPath(c.path()));
    }

    /// Replacing the set with an empty list still runs the strategy with
    /// an empty list — that's how consumers signal "everything's gone,
    /// drop your registered entries". Without going through this path
    /// (the prior in-tree workaround called the strategy directly,
    /// bypassing the watcher) the directory list would never shrink and
    /// stale dirs would keep being scanned.
    void testSetDirectories_emptyDrivesEmptyScan()
    {
        QTemporaryDir a;
        QVERIFY(a.isValid());

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectory(a.path(), LiveReload::Off);
        QCOMPARE(set.directories().size(), 1);

        set.setDirectories({}, LiveReload::Off);

        QVERIFY(set.directories().isEmpty());
        // The strategy's most recent scan must have been driven with an
        // empty directory list — that's the contract that lets a
        // consumer's strategy unregister stale entries.
        QVERIFY(strategy.lastDirectories.isEmpty());
        QVERIFY(strategy.scanCount >= 2); // initial register + post-set
    }

    /// When a target whose only watch is a parent-proxy is dropped via
    /// `setDirectories`, the parent watch is released — unless another
    /// surviving target still depends on it. Mirrors the shared-ancestor
    /// refcounting tested for promotion, but on the drop path.
    void testSetDirectories_releasesAncestorWatchOnDrop()
    {
        const QString tmp = QDir::cleanPath(m_tmp->path());
        const QString siblingA = tmp + QStringLiteral("/missing-set-a/leaf");
        const QString siblingB = tmp + QStringLiteral("/missing-set-b/leaf");
        QVERIFY(!QFileInfo::exists(siblingA));
        QVERIFY(!QFileInfo::exists(siblingB));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectories({siblingA, siblingB}, LiveReload::On);

        // Both targets share the m_tmp ancestor watch.
        QVERIFY(set.hasParentWatchForTest(tmp));
        QCOMPARE(set.watchedAncestorForTest(siblingA), tmp);
        QCOMPARE(set.watchedAncestorForTest(siblingB), tmp);

        // Drop sibling A — the shared ancestor must survive because B
        // still depends on it.
        set.setDirectories({siblingB}, LiveReload::On);
        QVERIFY(set.hasParentWatchForTest(tmp));
        QCOMPARE(set.watchedAncestorForTest(siblingA), QString());
        QCOMPARE(set.watchedAncestorForTest(siblingB), tmp);

        // Drop sibling B too — now nothing needs the ancestor watch, so
        // it must finally be released.
        set.setDirectories({}, LiveReload::On);
        QCOMPARE(set.hasParentWatchForTest(tmp), false);
        QCOMPARE(set.watchedAncestorForTest(siblingB), QString());
    }

    /// $HOME-class forbidden roots include not just $HOME itself but also
    /// the user-data roots that aren't XDG-rooted yet are still high-
    /// churn for typical users (Documents, Downloads). Watching them
    /// would otherwise trigger a full rescan for every browser download
    /// or editor save in those trees.
    void testForbiddenRoot_documentsLocationRefused()
    {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (documents.isEmpty()) {
            QSKIP("Platform has no DocumentsLocation");
        }
        const QString uniqueLeaf =
            QStringLiteral("phosphor-fsloader-doc-test-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString neverExists = documents + QLatin1Char('/') + uniqueLeaf + QStringLiteral("/never/exists");
        QVERIFY(!QFileInfo::exists(neverExists));

        RecordingStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.registerDirectory(neverExists, LiveReload::On);

        // Climb landed on Documents, which is now forbidden — no
        // ancestor watch installed.
        QCOMPARE(set.hasParentWatchForTest(documents), false);
        QCOMPARE(set.watchedAncestorForTest(neverExists), QString());
    }

    /// `syncFileWatches` silently dedupes when a strategy reports a path
    /// the base ALREADY watches via `attachWatcherForDir` (e.g. the
    /// strategy reports a registered root directory). The base must not
    /// log a "failed to add watch" warning in that case; covered here
    /// indirectly by asserting the rescan completes cleanly.
    void testSyncFileWatches_silentDedupeOfRegisteredRoot()
    {
        // Strategy reports the registered directory itself as a desired
        // file watch. The base already added that path via
        // attachWatcherForDir — case-1 dedupe path.
        const QString dirPath = QDir::cleanPath(m_tmp->path());
        RecordingStrategy strategy;
        strategy.setReportedFiles({dirPath});

        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        set.registerDirectory(dirPath, LiveReload::On);

        // Trigger a follow-up rescan to exercise the syncFileWatches
        // re-arming path — first scan installs, second scan re-encounters.
        touchFile(m_tmp->filePath(QStringLiteral("trigger.txt")));
        QSignalSpy spy(&set, &WatchedDirectorySet::rescanCompleted);
        QVERIFY(spy.wait(500));
        // No assertion on internal state — the regression coverage is
        // "this completed without QFileSystemWatcher complaining and
        // without warnings". A failure surfaces as a noisy test log
        // (the warning path) or a stuck rescan (failure to add).
        QVERIFY(strategy.scanCount >= 2);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestWatchedDirectorySet)
#include "test_watcheddirectoryset.moc"
