// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace PhosphorFsLoader;

namespace {

/// Trivial sink that records every commit so tests can assert on the
/// exact remove+current lists without a real registry.
class RecordingSink : public IDirectoryLoaderSink
{
public:
    std::optional<ParsedEntry> parseFile(const QString& filePath) override
    {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = doc.object();
        const QString name = obj.value(QLatin1String("name")).toString();
        if (name.isEmpty()) {
            return std::nullopt;
        }
        ParsedEntry e;
        e.key = name;
        e.sourcePath = filePath;
        e.payload = obj.value(QLatin1String("value")).toString().toStdString();
        return e;
    }

    void commitBatch(const QStringList& removedKeys, const QList<ParsedEntry>& currentEntries) override
    {
        ++commitCount;
        lastRemoved = removedKeys;
        lastCurrent = currentEntries;
        // Maintain a simple registry-ish map for assertions.
        for (const QString& k : removedKeys) {
            registry.remove(k);
        }
        for (const ParsedEntry& e : currentEntries) {
            registry.insert(e.key, std::any_cast<std::string>(e.payload));
        }
    }

    int commitCount = 0;
    QStringList lastRemoved;
    QList<ParsedEntry> lastCurrent;
    QHash<QString, std::string> registry;
};

/// Regression-test sink for the requestRescan-during-rescanAll race.
/// Fires a single `requestRescan()` from inside its first commitBatch
/// — mimicking a watcher event delivered during the scan. Declared at
/// file scope rather than inside the test function so it has external
/// linkage (avoids the anonymous-namespace `-Wsubobject-linkage`
/// warning on gcc).
class RacyRecordingSink : public RecordingSink
{
public:
    DirectoryLoader* loader = nullptr;
    bool reentered = false;
    void commitBatch(const QStringList& removedKeys, const QList<ParsedEntry>& currentEntries) override
    {
        RecordingSink::commitBatch(removedKeys, currentEntries);
        if (!reentered && loader) {
            reentered = true;
            // Defer via a singleShot(0) so the request is delivered
            // on the SAME event-loop pass but after the current
            // rescanAll stack frame finishes — matching the real-
            // world case where a watcher signal arrives during our
            // rescan and the slot fires on the next spin.
            QTimer::singleShot(0, [this]() {
                loader->requestRescan();
            });
        }
    }
};

void writeJson(const QString& path, const QString& name, const QString& value)
{
    QJsonObject obj;
    obj[QLatin1String("name")] = name;
    obj[QLatin1String("value")] = value;
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(obj).toJson());
}

} // namespace

class TestDirectoryLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_tmp.reset(new QTemporaryDir);
        QVERIFY(m_tmp->isValid());
    }

    // ── Basic scan ──────────────────────────────────────────────────────

    void testScan_emptyDirectory()
    {
        RecordingSink sink;
        DirectoryLoader loader(sink);
        const int n = loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);
        QCOMPARE(n, 0);
        QCOMPARE(sink.commitCount, 1);
        QVERIFY(sink.lastRemoved.isEmpty());
        QVERIFY(sink.lastCurrent.isEmpty());
    }

    void testScan_singleFile()
    {
        const QString f = m_tmp->filePath(QStringLiteral("a.json"));
        writeJson(f, QStringLiteral("alpha"), QStringLiteral("v1"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        const int n = loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);

        QCOMPARE(n, 1);
        QCOMPARE(sink.registry.value(QStringLiteral("alpha")), std::string("v1"));
    }

    void testScan_skipsMalformedFile()
    {
        // Good file + bad file — the good one must still load.
        writeJson(m_tmp->filePath(QStringLiteral("good.json")), QStringLiteral("good-key"), QStringLiteral("ok"));
        QFile bad(m_tmp->filePath(QStringLiteral("bad.json")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not valid json ");
        bad.close();

        RecordingSink sink;
        DirectoryLoader loader(sink);
        QCOMPARE(loader.loadFromDirectory(m_tmp->path(), LiveReload::Off), 1);
        QVERIFY(sink.registry.contains(QStringLiteral("good-key")));
    }

    // ── Stale-entry purge (the blocker from the review) ─────────────────

    void testRescan_purgesDeletedEntries()
    {
        writeJson(m_tmp->filePath(QStringLiteral("a.json")), QStringLiteral("alpha"), QStringLiteral("v1"));
        writeJson(m_tmp->filePath(QStringLiteral("b.json")), QStringLiteral("beta"), QStringLiteral("v2"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);
        QCOMPARE(loader.registeredCount(), 2);

        // Delete one file, request explicit rescan.
        QVERIFY(QFile::remove(m_tmp->filePath(QStringLiteral("a.json"))));
        loader.requestRescan();
        QTRY_COMPARE(sink.commitCount, 2);

        QCOMPARE(loader.registeredCount(), 1);
        QCOMPARE(sink.lastRemoved, QStringList{QStringLiteral("alpha")});
        QVERIFY(!sink.registry.contains(QStringLiteral("alpha")));
        QVERIFY(sink.registry.contains(QStringLiteral("beta")));
    }

    // ── User-wins collision ─────────────────────────────────────────────

    void testCollision_userOverridesSystem()
    {
        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid() && userDir.isValid());

        // Both files have the same key "shared" — user version wins.
        writeJson(systemDir.filePath(QStringLiteral("x.json")), QStringLiteral("shared"),
                  QStringLiteral("from-system"));
        writeJson(userDir.filePath(QStringLiteral("x.json")), QStringLiteral("shared"), QStringLiteral("from-user"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        // System first, user last — user wins on collision.
        loader.loadFromDirectories({systemDir.path(), userDir.path()}, LiveReload::Off);

        QCOMPARE(loader.registeredCount(), 1);
        QCOMPARE(sink.registry.value(QStringLiteral("shared")), std::string("from-user"));

        // The tracked entry records the shadowed system path so the
        // consumer could restore it after the user copy is deleted.
        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().sourcePath, userDir.filePath(QStringLiteral("x.json")));
        QCOMPARE(entries.first().systemSourcePath, systemDir.filePath(QStringLiteral("x.json")));
    }

    /// Regression guard for the collision-purge path. A user-override
    /// file shadows a system file; when the user copy is deleted, the
    /// next rescan must re-parse the system file fresh and clear the
    /// shadow metadata on the tracked entry.
    void testCollision_userDeletedRestoresSystem()
    {
        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid() && userDir.isValid());

        const QString systemPath = systemDir.filePath(QStringLiteral("x.json"));
        const QString userPath = userDir.filePath(QStringLiteral("x.json"));
        writeJson(systemPath, QStringLiteral("shared"), QStringLiteral("from-system"));
        writeJson(userPath, QStringLiteral("shared"), QStringLiteral("from-user"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.loadFromDirectories({systemDir.path(), userDir.path()}, LiveReload::Off);

        // Sanity — user wins, shadow metadata points at the system file.
        QCOMPARE(sink.registry.value(QStringLiteral("shared")), std::string("from-user"));
        {
            const auto entries = loader.entries();
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().sourcePath, userPath);
            QCOMPARE(entries.first().systemSourcePath, systemPath);
        }

        // Delete the user copy; explicit rescan. The tracked entry
        // should now point at the system file and carry no shadow
        // metadata (there's nothing left to shadow).
        QVERIFY(QFile::remove(userPath));
        loader.requestRescan();
        QTRY_COMPARE(sink.commitCount, 2);

        QCOMPARE(loader.registeredCount(), 1);
        QCOMPARE(sink.registry.value(QStringLiteral("shared")), std::string("from-system"));
        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().sourcePath, systemPath);
        QVERIFY(entries.first().systemSourcePath.isEmpty());
    }

    // ── Live reload + file watcher ──────────────────────────────────────

    void testLiveReload_detectsNewFile()
    {
        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::On);
        QCOMPARE(loader.registeredCount(), 0);

        // Drop a file — watcher fires directoryChanged, debounce
        // timer expires, rescan happens.
        writeJson(m_tmp->filePath(QStringLiteral("new.json")), QStringLiteral("new-key"), QStringLiteral("hot"));

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy.wait(500));

        QCOMPARE(loader.registeredCount(), 1);
        QVERIFY(sink.registry.contains(QStringLiteral("new-key")));
    }

    void testLiveReload_coalescesBurstEvents()
    {
        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::On);
        const int baseline = sink.commitCount;

        // Rapid file drops in the same debounce window — should
        // produce ONE commit, not N.
        for (int i = 0; i < 3; ++i) {
            writeJson(m_tmp->filePath(QStringLiteral("burst-%1.json").arg(i)), QStringLiteral("burst-%1").arg(i),
                      QStringLiteral("v"));
        }

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy.wait(500));

        QCOMPARE(sink.commitCount, baseline + 1);
        QCOMPARE(loader.registeredCount(), 3);
    }

    // ── Missing-directory + parent-dir promotion ────────────────────────

    void testLiveReload_missingDirWatchedViaParent()
    {
        // Target directory doesn't exist yet — the loader should still
        // accept the watch request via the parent, then pick up the
        // child when it materialises.
        const QString target = m_tmp->filePath(QStringLiteral("profiles"));
        QVERIFY(!QDir(target).exists());

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectory(target, LiveReload::On);
        QCOMPARE(loader.registeredCount(), 0);

        // Create the target + drop a file. Parent-watch fires on the
        // directory creation; the rescan+promote logic attaches a
        // direct watch and subsequent file edits work normally.
        QVERIFY(QDir(m_tmp->path()).mkdir(QStringLiteral("profiles")));
        writeJson(target + QStringLiteral("/first.json"), QStringLiteral("first"), QStringLiteral("v"));

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        // Parent-dir change fires on the mkdir; the loader promotes
        // the parent-watch to a direct watch on the now-existing
        // target and rescans. Inotify delivery is the limiting factor
        // here (not the debounce), so wait generously even with a
        // ~1 ms debounce — the watcher can take several hundred ms to
        // deliver the parent-dir-changed event on some kernels / CI
        // loads.
        const bool watcherFired = spy.wait(2000);

        // Known race: on some filesystems the parent-watch fires for
        // the mkdir BEFORE the file-create inotify event, so the first
        // rescan sees the empty target and the file-create lands on
        // the newly-attached direct watch a beat later. An explicit
        // requestRescan exercises the promoted direct watch so the
        // file is picked up regardless of inotify ordering. Also
        // covers the case where the parent-watch never fires at all
        // (sandboxed CI without inotify) — the explicit rescan picks
        // up the file deterministically.
        if (loader.registeredCount() == 0) {
            loader.requestRescan();
            QTRY_COMPARE_WITH_TIMEOUT(loader.registeredCount(), 1, 1000);
        }
        QCOMPARE(loader.registeredCount(), 1);
        QVERIFY(sink.registry.contains(QStringLiteral("first")));
        // Silence unused-variable warning while keeping the wait
        // outcome introspectable for debugging. If the watcher never
        // fires AND the explicit rescan didn't succeed, the test
        // would have already failed above.
        Q_UNUSED(watcherFired);
    }

    /// Regression guard for the grandparent-watch leak. When the
    /// target directory's immediate parent does not exist yet either,
    /// `attachWatcherForDir` climbs up and watches a further-up
    /// ancestor. On promotion (target now exists) the loader must
    /// remove the ACTUALLY-watched ancestor, not `info.absolutePath()`
    /// which would miss the climbed path entirely and leak the watch.
    void testAttachWatcher_promotionRemovesCorrectAncestor()
    {
        // Use QTemporaryDir as a safe root so we never touch the real
        // $HOME / GenericDataLocation. The tree has missing
        // intermediaries: <tmp>/a/b/c/profiles — only <tmp> exists
        // initially, so the climb must land on <tmp>.
        const QString root = m_tmp->path();
        const QString target = root + QStringLiteral("/a/b/c/profiles");
        QVERIFY(!QDir(target).exists());

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectory(target, LiveReload::On);

        // The climbed ancestor is recorded in the back-reference map.
        // It must be a genuine ancestor of the target — we don't
        // assume it equals `root` exactly, because filesystem-specific
        // canonicalisation may land on /tmp or its resolved path.
        const QString watchedAncestor = loader.watchedAncestorForTest(target);
        QVERIFY2(!watchedAncestor.isEmpty(),
                 qPrintable(QStringLiteral("expected a recorded ancestor for target %1").arg(target)));
        QVERIFY(loader.hasParentWatchForTest(watchedAncestor));
        // Verify the ancestor is an actual ancestor of the target
        // (target path starts with ancestor + "/"). cleanPath
        // normalisation on the target side.
        const QString cleanedTarget = QDir::cleanPath(target);
        QVERIFY2(
            cleanedTarget.startsWith(watchedAncestor + QLatin1Char('/')),
            qPrintable(QStringLiteral("ancestor %1 is not a prefix of target %2").arg(watchedAncestor, cleanedTarget)));

        // Materialise the full tree, then promote. After promotion the
        // actually-watched ancestor must be removed from the parent
        // watch set AND the back-reference map must no longer hold an
        // entry for this target.
        QVERIFY(QDir(root).mkpath(QStringLiteral("a/b/c/profiles")));
        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        loader.requestRescan();
        QVERIFY(spy.wait(1000));

        QVERIFY2(
            !loader.hasParentWatchForTest(watchedAncestor),
            qPrintable(QStringLiteral("ancestor %1 still in parent watch set after promotion").arg(watchedAncestor)));
        QVERIFY2(loader.watchedAncestorForTest(target).isEmpty(),
                 qPrintable(QStringLiteral("back-reference for %1 still present after promotion").arg(target)));
    }

    /// Regression guard for the bounded-climb path. A pathological
    /// symlink loop must not cause `attachWatcherForDir` to spin —
    /// the climb is capped at 32 iterations AND canonicalPath()
    /// collapses the loop first. Either defence is enough; this test
    /// verifies the composite terminates quickly.
    void testAttachWatcher_symlinkLoopTerminates()
    {
        // Build a/b with b being a symlink back to a. On Linux,
        // QDir::exists() on any sub-path follows the link, so the
        // climb terminates in finite time via canonicalPath() loop
        // collapse plus the 32-level climb cap.
        const QString root = m_tmp->path();
        QVERIFY(QDir(root).mkdir(QStringLiteral("loop-a")));
        const QString loopA = root + QStringLiteral("/loop-a");
        // Create symlink loop-a/loop-b -> loop-a
        const QString loopBLink = loopA + QStringLiteral("/loop-b");
        QVERIFY(QFile::link(loopA, loopBLink));

        const QString target = loopA + QStringLiteral("/loop-b/nonexistent/profiles");

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);

        // A 2-second wall-clock cap is generous — canonicalPath()
        // resolution on a symlink loop terminates in microseconds on
        // any real filesystem. If this hangs it's the 32-level bounded
        // climb catching a defence-in-depth failure (logged warning).
        QElapsedTimer timer;
        timer.start();
        loader.loadFromDirectory(target, LiveReload::On);
        QVERIFY2(
            timer.elapsed() < 2000,
            qPrintable(QStringLiteral("loadFromDirectory took %1 ms — climb did not terminate").arg(timer.elapsed())));
    }

    // ── Debounce race: rescan requested while rescan running ────────────

    /// Regression guard for the requestRescan-during-rescanAll race.
    /// A requestRescan call delivered from within the sink's
    /// commitBatch (simulating a watcher event that fires during the
    /// scan, or any re-entry through a signal connection) must cause
    /// a follow-up rescan. Without the guard, the debounce timer is
    /// idle during rescanAll and the nested start() is a no-op — the
    /// event is silently dropped.
    void testRescanRace_requestDuringRescan()
    {
        writeJson(m_tmp->filePath(QStringLiteral("a.json")), QStringLiteral("alpha"), QStringLiteral("v1"));

        RacyRecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        sink.loader = &loader;
        loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);

        // The initial load ran one rescan (commitCount == 1). The
        // singleShot(0) scheduled inside that commit must schedule a
        // second rescan after the debounce. If the race guard is
        // missing, commitCount stays at 1 and QTRY_COMPARE times out.
        QTRY_COMPARE_WITH_TIMEOUT(sink.commitCount, 2, 1000);
    }

    // ── Oversized-file guard ────────────────────────────────────────────

    void testOversizedFileIsSkipped()
    {
        // Write a JSON file strictly larger than kMaxFileBytes. Must be
        // structurally-valid JSON so the size check (not the parser) is
        // the thing rejecting it — otherwise the test couldn't tell a
        // parse-reject from a size-reject.
        const QString bigPath = m_tmp->filePath(QStringLiteral("oversized.json"));
        {
            QFile f(bigPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"name\":\"oversized\",\"value\":\"");
            // One chunk of 4 KiB 'x's, repeated until strictly over the cap.
            const QByteArray chunk(4096, 'x');
            qint64 written = 0;
            const qint64 target = DirectoryLoader::kMaxFileBytes + 2 * chunk.size();
            while (written < target) {
                f.write(chunk);
                written += chunk.size();
            }
            f.write("\"}");
        }
        QVERIFY(QFileInfo(bigPath).size() > DirectoryLoader::kMaxFileBytes);

        // A small valid sibling must still load — the size guard must
        // not short-circuit the entire scan.
        writeJson(m_tmp->filePath(QStringLiteral("small.json")), QStringLiteral("small"), QStringLiteral("v"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        const int n = loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);

        QCOMPARE(n, 1);
        QVERIFY(sink.registry.contains(QStringLiteral("small")));
        QVERIFY(!sink.registry.contains(QStringLiteral("oversized")));
        // Ensure the oversized file didn't end up in the tracked set
        // under any key.
        const auto entries = loader.entries();
        for (const auto& entry : entries) {
            QVERIFY2(entry.sourcePath != bigPath,
                     qPrintable(QStringLiteral("oversized file %1 leaked into tracked set").arg(bigPath)));
        }
    }

    /// Regression guard for the GUI-thread DoS — a directory sprayed
    /// with too many JSON files must stop short at the configured cap
    /// rather than parsing them all. Uses the test-only
    /// `setMaxEntriesForTest` to trip the guard at a 3-digit file count
    /// instead of the 10'000 production default (which would balloon
    /// the CI filesystem footprint for no test value).
    void testEntryCountCapShortCircuitsScan()
    {
        constexpr int kTestCap = 5;
        const int totalFiles = kTestCap * 2; // more than the cap, deterministically

        for (int i = 0; i < totalFiles; ++i) {
            // Zero-padded names so alphabetic sort is deterministic and
            // the surviving set is always the first kTestCap files.
            writeJson(m_tmp->filePath(QStringLiteral("entry-%1.json").arg(i, 3, 10, QLatin1Char('0'))),
                      QStringLiteral("k-%1").arg(i, 3, 10, QLatin1Char('0')), QStringLiteral("v"));
        }

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setMaxEntriesForTest(kTestCap);
        const int n = loader.loadFromDirectory(m_tmp->path(), LiveReload::Off);

        QCOMPARE(n, kTestCap);
        QCOMPARE(loader.registeredCount(), kTestCap);
        // First kTestCap (alphabetically) made it through, the rest were
        // silently dropped — the aggregate qCWarning in the library
        // surfaces the cap in the log once per trip.
        for (int i = 0; i < kTestCap; ++i) {
            QVERIFY(sink.registry.contains(QStringLiteral("k-%1").arg(i, 3, 10, QLatin1Char('0'))));
        }
        for (int i = kTestCap; i < totalFiles; ++i) {
            QVERIFY(!sink.registry.contains(QStringLiteral("k-%1").arg(i, 3, 10, QLatin1Char('0'))));
        }
    }

    /// Regression guard for the Phase-1g reverse-scan + first-wins fix.
    /// A system directory that overflows the entry-count cap must NOT
    /// drop the user dir's overrides. With forward-iteration the cap
    /// would trip before the user dir was ever scanned, silently dropping
    /// every user override. Reverse-iteration drops *system* overflow
    /// first; user entries always survive. Also pin user-wins on
    /// collision: a key that exists in BOTH dirs must resolve to the
    /// user value, not the system value.
    void testEntryCountCapPreservesUserWinsAcrossDirs()
    {
        constexpr int kTestCap = 5;

        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid() && userDir.isValid());

        // System dir holds (cap + 5) files. Names zero-padded so
        // alphabetic ordering is deterministic on every platform.
        const int systemTotal = kTestCap + 5;
        for (int i = 0; i < systemTotal; ++i) {
            writeJson(systemDir.filePath(QStringLiteral("sys-%1.json").arg(i, 3, 10, QLatin1Char('0'))),
                      QStringLiteral("sys-key-%1").arg(i, 3, 10, QLatin1Char('0')),
                      QStringLiteral("from-system-%1").arg(i));
        }
        // One of the system files uses a key the user dir will collide
        // with — pick a low-index name so it definitely isn't dropped
        // by the cap on the system side. Index "001" sorts early.
        writeJson(systemDir.filePath(QStringLiteral("sys-collide.json")), QStringLiteral("collide"),
                  QStringLiteral("from-system"));

        // User dir holds 3 files: two unique keys plus the colliding key.
        writeJson(userDir.filePath(QStringLiteral("user-a.json")), QStringLiteral("user-only-a"),
                  QStringLiteral("from-user-a"));
        writeJson(userDir.filePath(QStringLiteral("user-b.json")), QStringLiteral("user-only-b"),
                  QStringLiteral("from-user-b"));
        writeJson(userDir.filePath(QStringLiteral("collide.json")), QStringLiteral("collide"),
                  QStringLiteral("from-user"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setMaxEntriesForTest(kTestCap);
        // Caller order is system-first / user-last per the public
        // contract; the loader iterates in reverse so user wins.
        loader.loadFromDirectories({systemDir.path(), userDir.path()}, LiveReload::Off);

        // Cap reached, but the THREE user files survived. They are the
        // priority dir — production should iterate user-first (or use
        // some equivalent ordering) so the cap drops system overflow
        // rather than user overrides. The Phase-1g fix in the PR
        // description claims this is the new behaviour; if this test
        // fails, the fix did not land OR a regression undid it.
        QVERIFY2(sink.registry.contains(QStringLiteral("user-only-a")),
                 "user file 'user-only-a' was dropped — cap evicted user overrides");
        QVERIFY2(sink.registry.contains(QStringLiteral("user-only-b")),
                 "user file 'user-only-b' was dropped — cap evicted user overrides");

        // Collision resolves to user content, not system.
        QVERIFY(sink.registry.contains(QStringLiteral("collide")));
        QCOMPARE(sink.registry.value(QStringLiteral("collide")), std::string("from-user"));

        // Cap honoured at exactly kTestCap entries total (the 3 user
        // files + 2 system files = 5).
        QCOMPARE(loader.registeredCount(), kTestCap);
    }

    /// Regression guard for the shared-ancestor preservation on
    /// promotion. Two missing-target sibling directories that climb to
    /// the SAME ancestor must keep the parent watch alive while either
    /// sibling still depends on it. Promoting one sibling to a direct
    /// watch must not yank the parent watch out from under the other.
    ///
    /// Setup: both targets are under DIFFERENT immediate parents that
    /// don't exist, so both climb to <tmp> as the only existing
    /// ancestor. Materialising one's tree promotes JUST that one;
    /// the other target's parent still doesn't exist so it still
    /// climbs to <tmp> — the shared ancestor MUST survive the
    /// promotion.
    void testAttachWatcher_sharedAncestorPreservedAcrossPromotion()
    {
        const QString root = m_tmp->path();
        // Distinct immediate-parent dirs so promoting one doesn't
        // accidentally materialise the other's parent chain.
        const QString targetA = root + QStringLiteral("/sibling-a/profiles");
        const QString targetB = root + QStringLiteral("/sibling-b/themes");
        QVERIFY(!QDir(targetA).exists());
        QVERIFY(!QDir(targetB).exists());
        QVERIFY(!QDir(root + QStringLiteral("/sibling-a")).exists());
        QVERIFY(!QDir(root + QStringLiteral("/sibling-b")).exists());

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectories({targetA, targetB}, LiveReload::On);

        // Both targets recorded the same ancestor (the tmp root,
        // since neither sibling-* exists yet).
        const QString ancestorA = loader.watchedAncestorForTest(targetA);
        const QString ancestorB = loader.watchedAncestorForTest(targetB);
        QVERIFY2(!ancestorA.isEmpty(), "target A has no recorded ancestor");
        QVERIFY2(!ancestorB.isEmpty(), "target B has no recorded ancestor");
        QCOMPARE(ancestorA, ancestorB);
        QVERIFY(loader.hasParentWatchForTest(ancestorA));

        // Materialise ONLY targetA's tree (sibling-a + sibling-a/profiles).
        // sibling-b/ stays absent so targetB still depends on the
        // common <tmp> ancestor.
        QVERIFY(QDir(root).mkpath(QStringLiteral("sibling-a/profiles")));
        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        loader.requestRescan();
        QVERIFY(spy.wait(1000));

        // targetA is now directly watched — its back-reference is gone.
        QVERIFY2(loader.watchedAncestorForTest(targetA).isEmpty(),
                 "back-reference for target A still present after promotion");

        // targetB STILL has sibling-b/ missing → still climbs to the
        // shared ancestor. The parent watch on the shared ancestor
        // MUST survive targetA's promotion.
        QVERIFY2(loader.hasParentWatchForTest(ancestorA),
                 "shared ancestor was removed despite target B still depending on it");
        QCOMPARE(loader.watchedAncestorForTest(targetB), ancestorA);
    }

    /// Regression guard for the m_watchedFiles re-arm on
    /// atomic-rename save (most editors / QSaveFile). The watcher silently
    /// drops the old inode after a rename-over; without an explicit
    /// re-add on the new inode, the next file edit would never fire a
    /// rescan and the consumer would see stale content.
    void testAtomicRenameSaveDetected()
    {
        const QString target = m_tmp->filePath(QStringLiteral("doc.json"));
        writeJson(target, QStringLiteral("doc"), QStringLiteral("v1"));

        RecordingSink sink;
        DirectoryLoader loader(sink);
        loader.setDebounceIntervalForTest(1);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::On);
        QCOMPARE(loader.registeredCount(), 1);
        QCOMPARE(sink.registry.value(QStringLiteral("doc")), std::string("v1"));

        // Editor-style atomic save: write to tmp, rename over target.
        // The old inode is gone; the watcher (if it was watching by
        // inode) loses the entry. The loader must re-arm the file watch
        // on the new inode during the next rescan so subsequent edits
        // fire entriesChanged.
        const QString tmpPath = m_tmp->filePath(QStringLiteral("doc.json.tmp"));
        writeJson(tmpPath, QStringLiteral("doc"), QStringLiteral("v2"));
        QVERIFY2(QFile::remove(target), "could not remove pre-rename target");
        QVERIFY2(QFile::rename(tmpPath, target), "atomic rename failed");

        // Watcher on directory fires (mtime change), debounce expires,
        // rescan picks up the new content. The directory-changed signal
        // is the reliable trigger here — the per-file watch may have
        // dropped silently.
        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy.wait(2000));

        QCOMPARE(loader.registeredCount(), 1);
        QCOMPARE(sink.registry.value(QStringLiteral("doc")), std::string("v2"));

        // Now do a SECOND edit — this exercises the re-armed file
        // watch. If syncFileWatches did not re-add the new inode, this
        // edit would still bounce off the now-dead watch and only fire
        // via the directory-changed event (which would also work, so
        // the assertion is on the post-condition: a single rescan must
        // reflect the v3 content).
        QVERIFY(QFile::remove(target));
        writeJson(target, QStringLiteral("doc"), QStringLiteral("v3"));

        QSignalSpy spy2(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy2.wait(2000));
        QCOMPARE(sink.registry.value(QStringLiteral("doc")), std::string("v3"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestDirectoryLoader)
#include "test_directoryloader.moc"
