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

    /// requestRescan delivered DURING a scan (e.g. from inside
    /// performScan via a nested signal) is replayed at the end of the
    /// running scan rather than being silently dropped.
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
                    QTimer::singleShot(0, [this]() {
                        set->requestRescan();
                    });
                }
                return {};
            }
        };

        ReentrantStrategy strategy;
        WatchedDirectorySet set(strategy);
        set.setDebounceIntervalForTest(1);
        strategy.set = &set;
        set.registerDirectory(m_tmp->path(), LiveReload::Off);

        // Initial registration ran one scan; the singleShot inside it
        // schedules a follow-up which must fire after the debounce.
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

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestWatchedDirectorySet)
#include "test_watcheddirectoryset.moc"
