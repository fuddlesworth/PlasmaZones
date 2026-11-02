// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Coverage for WatchedFileSet against a real QFileSystemWatcher. Every row is a
// way a watched file reaches its next state (created late, created under a
// directory that is itself created late, replaced by rename, edited in place,
// deleted) plus the two negatives the class exists for: an unrelated write
// beside a watched file reports nothing, and setPaths never accumulates
// watches.

#include <PhosphorFsLoader/WatchedFileSet.h>

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using PhosphorFsLoader::WatchedFileSet;

namespace {

/// Short enough to keep the suite fast, long enough that a write and its
/// follow-up events land in one window.
constexpr int kDebounceMs = 20;
/// How long a positive row waits for the signal.
constexpr int kSignalTimeoutMs = 2000;
/// How long a negative row waits to be sure nothing arrives: many debounce
/// windows, so a stray report would have had every chance to fire.
constexpr int kQuietMs = 300;

[[nodiscard]] bool writeInPlace(const QString& path, const QByteArray& content)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(content) == content.size();
}

/// Atomic replacement through a temporary file and rename(), the way editors
/// and QSaveFile save. The path ends up on a NEW inode.
[[nodiscard]] bool replaceByRename(const QString& path, const QByteArray& content)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(content) == content.size() && f.commit();
}

} // namespace

class TestWatchedFileSet : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_tmp;

    [[nodiscard]] QString path(const QString& relative) const
    {
        return QDir::cleanPath(m_tmp->filePath(relative));
    }

    [[nodiscard]] std::unique_ptr<WatchedFileSet> makeSet(const QStringList& paths) const
    {
        auto set = std::make_unique<WatchedFileSet>();
        set->setDebounceInterval(kDebounceMs);
        set->setPaths(paths);
        return set;
    }

private Q_SLOTS:

    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
    }

    void cleanup()
    {
        m_tmp.reset();
    }

    /// setPaths normalises to clean absolute form, drops empties and
    /// duplicates, keeps caller order, and does not itself report anything.
    void setPaths_normalisesAndDoesNotReport()
    {
        const QString a = path(QStringLiteral("a.json"));
        const QString b = path(QStringLiteral("b.json"));
        QVERIFY(writeInPlace(a, "a"));

        WatchedFileSet set;
        QSignalSpy spy(&set, &WatchedFileSet::filesChanged);
        set.setPaths({b, QString(), m_tmp->path() + QStringLiteral("/./a.json"), a, b});

        QCOMPARE(set.paths(), (QStringList{b, a}));
        QCOMPARE(set.debounceInterval(), WatchedFileSet::DefaultDebounceMs);
        QTest::qWait(WatchedFileSet::DefaultDebounceMs + kQuietMs);
        QCOMPARE(spy.count(), 0);
    }

    /// A path that does not exist yet reports when it is created.
    void missingFile_reportsOnCreation()
    {
        const QString target = path(QStringLiteral("config.json"));
        auto set = makeSet({target});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(writeInPlace(target, "{}"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});
    }

    /// The file's parent directory does not exist either. Creating the
    /// directory is not a change of the file and reports nothing, but it must
    /// move the ancestor watch down, so the file created inside it afterwards
    /// is still noticed.
    void missingParentDirectory_reportsOnceFileAppears()
    {
        const QString directory = path(QStringLiteral("sub/deeper"));
        const QString target = directory + QStringLiteral("/theme.json");
        auto set = makeSet({target});
        QCOMPARE(set->watchedPathsForTest(), QStringList{QDir::cleanPath(m_tmp->path())});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(QDir().mkpath(directory));
        // Creating "sub" raised the event on the temporary root, and the
        // re-resolution after it moved the watch down to the deepest directory
        // that now exists, right above the file, releasing the root.
        QTRY_COMPARE_WITH_TIMEOUT(set->watchedPathsForTest(), QStringList{directory}, kSignalTimeoutMs);
        QTest::qWait(kQuietMs);
        QCOMPARE(spy.count(), 0);

        QVERIFY(writeInPlace(target, "{}"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});
    }

    /// Atomic replacement by rename lands a new inode under the same name.
    /// It reports, and the file watch is re-armed onto the new inode so a
    /// later in-place edit (which only the file's own watch can see) reports
    /// too.
    void atomicReplace_reportsAndRearms()
    {
        const QString target = path(QStringLiteral("config.json"));
        QVERIFY(writeInPlace(target, "one"));
        auto set = makeSet({target});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(replaceByRename(target, "two!"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});

        spy.clear();
        QVERIFY(writeInPlace(target, "three!!"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});
    }

    /// An in-place edit (same inode) reports.
    void inPlaceEdit_reports()
    {
        const QString target = path(QStringLiteral("config.json"));
        QVERIFY(writeInPlace(target, "one"));
        auto set = makeSet({target});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(writeInPlace(target, "a longer second body"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});
    }

    /// Deletion reports, and so does recreating the file afterwards.
    void deletion_reportsThenRecreationReports()
    {
        const QString target = path(QStringLiteral("config.json"));
        QVERIFY(writeInPlace(target, "one"));
        auto set = makeSet({target});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(QFile::remove(target));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});

        spy.clear();
        QVERIFY(writeInPlace(target, "back"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{target});
    }

    /// Writes beside a watched file wake the directory watch but change no
    /// watched fingerprint, so nothing is reported. Covers both a present and
    /// a missing watched file, since the missing one relies on the directory
    /// watch alone.
    void unrelatedSiblingWrite_reportsNothing()
    {
        const QString present = path(QStringLiteral("config.json"));
        const QString missing = path(QStringLiteral("theme.json"));
        QVERIFY(writeInPlace(present, "one"));
        auto set = makeSet({present, missing});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        const QString sibling = path(QStringLiteral("unrelated.txt"));
        QVERIFY(writeInPlace(sibling, "noise"));
        QVERIFY(replaceByRename(sibling, "more noise"));
        QVERIFY(QFile::remove(sibling));
        QTest::qWait(kQuietMs);
        QCOMPARE(spy.count(), 0);
    }

    /// Changes to several watched files inside one debounce window arrive as
    /// one report listing exactly the files that changed, in paths() order.
    void severalChanges_coalesceIntoOneReport()
    {
        const QString a = path(QStringLiteral("a.json"));
        const QString b = path(QStringLiteral("b.json"));
        const QString c = path(QStringLiteral("c.json"));
        QVERIFY(writeInPlace(a, "a"));
        QVERIFY(writeInPlace(c, "c"));
        auto set = std::make_unique<WatchedFileSet>();
        set->setDebounceInterval(200);
        set->setPaths({a, b, c});
        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);

        QVERIFY(writeInPlace(c, "changed c"));
        QVERIFY(writeInPlace(a, "changed a"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), (QStringList{a, c}));
    }

    /// setPaths reconciles: watches for a dropped path (its file and its
    /// directory) are removed, nothing accumulates across calls, and a
    /// dropped path no longer reports.
    void setPaths_reconcilesWatches()
    {
        QVERIFY(QDir(m_tmp->path()).mkpath(QStringLiteral("one")));
        QVERIFY(QDir(m_tmp->path()).mkpath(QStringLiteral("two")));
        const QString dirOne = path(QStringLiteral("one"));
        const QString dirTwo = path(QStringLiteral("two"));
        const QString a = dirOne + QStringLiteral("/a.json");
        const QString b = dirTwo + QStringLiteral("/b.json");
        QVERIFY(writeInPlace(a, "a"));
        QVERIFY(writeInPlace(b, "b"));

        auto set = makeSet({a, b});
        QCOMPARE(set->watchedPathsForTest(), (QStringList{dirOne, a, dirTwo, b}));

        set->setPaths({b});
        QCOMPARE(set->watchedPathsForTest(), (QStringList{dirTwo, b}));
        set->setPaths({b});
        QCOMPARE(set->watchedPathsForTest(), (QStringList{dirTwo, b}));
        QCOMPARE(set->paths(), QStringList{b});

        QSignalSpy spy(set.get(), &WatchedFileSet::filesChanged);
        QVERIFY(writeInPlace(a, "dropped path edited"));
        QTest::qWait(kQuietMs);
        QCOMPARE(spy.count(), 0);

        QVERIFY(writeInPlace(b, "kept path edited"));
        QVERIFY(spy.wait(kSignalTimeoutMs));
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{b});

        set->setPaths({});
        QCOMPARE(set->watchedPathsForTest(), QStringList{});
    }
};

QTEST_MAIN(TestWatchedFileSet)
#include "test_watchedfileset.moc"
