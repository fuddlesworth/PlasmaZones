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

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestWatchedDirectorySet)
#include "test_watcheddirectoryset.moc"
