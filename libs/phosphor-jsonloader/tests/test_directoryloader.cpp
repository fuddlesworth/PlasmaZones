// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorJsonLoader;

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
        DirectoryLoader loader(&sink);
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
        DirectoryLoader loader(&sink);
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
        DirectoryLoader loader(&sink);
        QCOMPARE(loader.loadFromDirectory(m_tmp->path(), LiveReload::Off), 1);
        QVERIFY(sink.registry.contains(QStringLiteral("good-key")));
    }

    // ── Stale-entry purge (the blocker from the review) ─────────────────

    void testRescan_purgesDeletedEntries()
    {
        writeJson(m_tmp->filePath(QStringLiteral("a.json")), QStringLiteral("alpha"), QStringLiteral("v1"));
        writeJson(m_tmp->filePath(QStringLiteral("b.json")), QStringLiteral("beta"), QStringLiteral("v2"));

        RecordingSink sink;
        DirectoryLoader loader(&sink);
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
        DirectoryLoader loader(&sink);
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

    // ── Live reload + file watcher ──────────────────────────────────────

    void testLiveReload_detectsNewFile()
    {
        RecordingSink sink;
        DirectoryLoader loader(&sink);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::On);
        QCOMPARE(loader.registeredCount(), 0);

        // Drop a file — watcher fires directoryChanged, debounce
        // timer expires, rescan happens.
        writeJson(m_tmp->filePath(QStringLiteral("new.json")), QStringLiteral("new-key"), QStringLiteral("hot"));

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy.wait(2000));

        QCOMPARE(loader.registeredCount(), 1);
        QVERIFY(sink.registry.contains(QStringLiteral("new-key")));
    }

    void testLiveReload_coalescesBurstEvents()
    {
        RecordingSink sink;
        DirectoryLoader loader(&sink);
        loader.loadFromDirectory(m_tmp->path(), LiveReload::On);
        const int baseline = sink.commitCount;

        // Rapid file drops in the same debounce window — should
        // produce ONE commit, not N.
        for (int i = 0; i < 3; ++i) {
            writeJson(m_tmp->filePath(QStringLiteral("burst-%1.json").arg(i)), QStringLiteral("burst-%1").arg(i),
                      QStringLiteral("v"));
        }

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        QVERIFY(spy.wait(2000));

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
        DirectoryLoader loader(&sink);
        loader.loadFromDirectory(target, LiveReload::On);
        QCOMPARE(loader.registeredCount(), 0);

        // Create the target + drop a file. Parent-watch fires on the
        // directory creation; the rescan+promote logic attaches a
        // direct watch and subsequent file edits work normally.
        QVERIFY(QDir(m_tmp->path()).mkdir(QStringLiteral("profiles")));
        writeJson(target + QStringLiteral("/first.json"), QStringLiteral("first"), QStringLiteral("v"));

        QSignalSpy spy(&loader, &DirectoryLoader::entriesChanged);
        // Two possible events — the parent-dir change AND the file
        // create — but the debounce coalesces. Wait generously.
        QVERIFY(spy.wait(3000));

        // The file may or may not have been picked up on the first
        // wake depending on filesystem ordering. Pump once more if
        // needed.
        if (loader.registeredCount() == 0) {
            loader.requestRescan();
            QTRY_COMPARE_WITH_TIMEOUT(loader.registeredCount(), 1, 2000);
        }
        QVERIFY(sink.registry.contains(QStringLiteral("first")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestDirectoryLoader)
#include "test_directoryloader.moc"
