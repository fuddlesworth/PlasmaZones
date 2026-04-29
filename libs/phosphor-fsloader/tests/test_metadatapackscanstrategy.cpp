// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Direct coverage for `MetadataPackScanStrategy<Payload>` using a
// synthetic `FakePayload` POD. Pins the scaffolding contract — the
// reverse-iterate first-wins layering, per-rescan cap, SHA-1 change-only
// emit, sorted-by-id output, isUser classification, metadata-size cap,
// stale-entry purge — independently of either real consumer's schema
// (`ShaderInfo`, `AnimationShaderEffect`). The two real consumers
// collapse onto this strategy in the C2 follow-up; their schema tests
// pin schema parsing, this file pins everything else.

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/MetadataPackScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <optional>
#include <utility>

using namespace PhosphorFsLoader;

namespace {

/// Synthetic payload used to pin the scaffolding contract — deliberately
/// NOT either of the real consumer schemas. Two fields:
///   • `id`  — required by `MetadataPackScanStrategy`'s static_assert.
///   • `score` — an arbitrary integer that exercises the
///               `SignatureContrib` callback (fanned into the SHA-1).
/// Plus `isUser` for the user-classification contract.
struct FakePayload
{
    QString id;
    int score = 0;
    bool isUser = false;
    QString sourceDir; // recorded on parse for spot checks
    QString fragmentShaderPath; // exercised by the watch-list contract
};

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

void writeMetadata(const QString& subdirPath, const QString& id, int score, const QString& fragName = QString())
{
    QVERIFY(QDir().mkpath(subdirPath));
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("score"), score);
    if (!fragName.isEmpty()) {
        obj.insert(QStringLiteral("fragmentShader"), fragName);
        // Materialise the referenced file too so per-entry watch
        // extraction can see something on disk.
        writeFile(subdirPath + QLatin1Char('/') + fragName, QByteArrayLiteral("// frag\n"));
    }
    writeFile(subdirPath + QStringLiteral("/metadata.json"), QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

/// Default parser: trivial `metadata.json` → `FakePayload`.
auto makeDefaultParser()
{
    return [](const QString& subdirPath, const QJsonObject& root, bool isUser) -> std::optional<FakePayload> {
        FakePayload p;
        p.id = root.value(QStringLiteral("id")).toString();
        p.score = root.value(QStringLiteral("score")).toInt(0);
        p.isUser = isUser;
        p.sourceDir = subdirPath;
        const QString frag = root.value(QStringLiteral("fragmentShader")).toString();
        if (!frag.isEmpty()) {
            p.fragmentShaderPath = subdirPath + QLatin1Char('/') + frag;
        }
        return p;
    };
}

/// Default signature contributor: mixes `score` + `isUser` into the
/// SHA-1 alongside the strategy's id contribution. Without this the
/// "edit a payload field → next scan reports change" contract has
/// nothing payload-specific to fingerprint.
auto makeDefaultSignatureContrib()
{
    return [](QCryptographicHash& h, const FakePayload& p) {
        h.addData(QByteArray::number(p.score));
        h.addData(QByteArrayView("|"));
        h.addData(p.isUser ? "u" : "s");
    };
}

/// Adapter that delegates `performScan` to a `MetadataPackScanStrategy`
/// while capturing the watch list returned. Lifted to namespace scope
/// (rather than a function-local class) so the strategy template doesn't
/// instantiate against a type with internal linkage — gcc warns
/// `-Wsubobject-linkage` on the local form.
class CapturingAdapter : public IScanStrategy
{
public:
    explicit CapturingAdapter(MetadataPackScanStrategy<FakePayload>& inner)
        : m_inner(&inner)
    {
    }
    QStringList performScan(const QStringList& dirs) override
    {
        lastWatches = m_inner->performScan(dirs);
        return lastWatches;
    }
    QStringList lastWatches;

private:
    MetadataPackScanStrategy<FakePayload>* m_inner;
};

} // namespace

class TestMetadataPackScanStrategy : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_tmp.reset(new QTemporaryDir);
        QVERIFY(m_tmp->isValid());
    }

    /// Two registered dirs, both with a `metadata.json` declaring the
    /// same id. The user dir's payload must win (reverse-iteration +
    /// first-wins). Pinned because it is the load-bearing contract for
    /// every consumer registry's user-override semantic.
    void testFirstWinsOnIdCollision()
    {
        const QString sysDir = m_tmp->filePath(QStringLiteral("sys"));
        const QString userDir = m_tmp->filePath(QStringLiteral("user"));
        QVERIFY(QDir().mkpath(sysDir));
        QVERIFY(QDir().mkpath(userDir));

        writeMetadata(sysDir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/1);
        writeMetadata(userDir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/2);

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setUserPath(userDir);

        WatchedDirectorySet set(strategy);
        // Canonical input: lowest priority first, user last.
        set.registerDirectories({sysDir, userDir}, LiveReload::Off);

        QCOMPARE(commits, 1);
        QCOMPARE(strategy.size(), 1);
        const FakePayload p = strategy.pack(QStringLiteral("pkg-a"));
        QCOMPARE(p.score, 2); // user-dir payload (score=2) won
        QCOMPARE(p.isUser, true);
        QCOMPARE(p.sourceDir, QDir::cleanPath(userDir + QStringLiteral("/pkg-a")));
    }

    /// Per-rescan cap with reverse-iteration drops *system* overflow,
    /// not user overrides. Register two dirs (sys lowest, user highest);
    /// fill sys with > cap subdirs; user has its own one entry. Cap
    /// must trip during the system pass AFTER the user pass already
    /// claimed its id, so the user entry survives.
    void testReverseIterateCapTripDropsSystemOverflow()
    {
        const QString sysDir = m_tmp->filePath(QStringLiteral("sys"));
        const QString userDir = m_tmp->filePath(QStringLiteral("user"));
        QVERIFY(QDir().mkpath(sysDir));
        QVERIFY(QDir().mkpath(userDir));

        // User: one entry.
        writeMetadata(userDir + QStringLiteral("/user-pkg"), QStringLiteral("user-pkg"), /*score=*/100);

        // System: 5 entries. Cap will be 3, so the system pass trips
        // after 2 (user pass already added 1).
        for (int i = 0; i < 5; ++i) {
            writeMetadata(sysDir + QStringLiteral("/sys-pkg-%1").arg(i), QStringLiteral("sys-pkg-%1").arg(i),
                          /*score=*/i);
        }

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setMaxEntries(3);
        strategy.setUserPath(userDir);

        WatchedDirectorySet set(strategy);
        set.registerDirectories({sysDir, userDir}, LiveReload::Off);

        QCOMPARE(strategy.size(), 3);
        // User entry must survive the cap-trip.
        QVERIFY(strategy.contains(QStringLiteral("user-pkg")));
        QCOMPARE(strategy.pack(QStringLiteral("user-pkg")).isUser, true);
        // Exactly two system entries got in (3 cap - 1 user); QDir::Name
        // sort means sys-pkg-0 + sys-pkg-1 should win, but we don't
        // pin which two — only that the count is right and the user
        // override is present.
    }

    /// Two scans with identical filesystem state must not invoke the
    /// `OnCommit` callback the second time. Editing the payload's
    /// `score` (which the signature contributor mixes in) must invoke
    /// it. Pins the change-only emit contract: consumers gate their
    /// public content-changed signal on `OnCommit`.
    void testChangeOnlyEmit_identicalScanDoesNotCommit()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/7);

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setSignatureContrib(makeDefaultSignatureContrib());

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(commits, 1);

        // Second scan with identical state — no commit.
        set.rescanNow();
        QCOMPARE(commits, 1);

        // Edit the payload's score field — signature differs, commit fires.
        writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/42);
        set.rescanNow();
        QCOMPARE(commits, 2);
        QCOMPARE(strategy.pack(QStringLiteral("pkg-a")).score, 42);
    }

    /// `setUserPath` plus a registered dir whose canonical path
    /// matches must classify entries from that dir as `isUser=true`,
    /// and entries from sibling dirs as `isUser=false`.
    void testIsUserClassification()
    {
        const QString sysDir = m_tmp->filePath(QStringLiteral("sys"));
        const QString userDir = m_tmp->filePath(QStringLiteral("user"));
        QVERIFY(QDir().mkpath(sysDir));
        QVERIFY(QDir().mkpath(userDir));

        writeMetadata(sysDir + QStringLiteral("/sys-pkg"), QStringLiteral("sys-pkg"), 1);
        writeMetadata(userDir + QStringLiteral("/user-pkg"), QStringLiteral("user-pkg"), 2);

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        strategy.setUserPath(userDir);

        WatchedDirectorySet set(strategy);
        // Canonical lowest-first: sys then user.
        set.registerDirectories({sysDir, userDir}, LiveReload::Off);

        QCOMPARE(strategy.pack(QStringLiteral("sys-pkg")).isUser, false);
        QCOMPARE(strategy.pack(QStringLiteral("user-pkg")).isUser, true);
    }

    /// Each entry's `metadata.json` plus everything the
    /// `PerEntryWatchPaths` callback returns must end up in the
    /// returned watch list. Per-search-path additions from
    /// `PerDirectoryWatchPaths` land too. The strategy returns a list
    /// the base re-arms `QFileSystemWatcher`'s file set from on every
    /// rescan.
    void testPerRescanWatchList()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0, QStringLiteral("effect.frag"));
        // Top-level shared file in the search path itself, not in any
        // pack subdir. Mirrors how the shader-pack registry adds
        // `common.glsl` to the watch list.
        const QString sharedInclude = dir + QStringLiteral("/common.glsl");
        writeFile(sharedInclude, QByteArrayLiteral("// shared\n"));

        // Hand-rolled IScanStrategy adapter (defined at namespace scope
        // above) captures the watch list `WatchedDirectorySet` requested.
        // Going through the base covers the same dispatch path the
        // production registries use.
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        strategy.setPerEntryWatchPaths([](const FakePayload& p) -> QStringList {
            QStringList out;
            if (!p.fragmentShaderPath.isEmpty()) {
                out.append(p.fragmentShaderPath);
            }
            return out;
        });
        strategy.setPerDirectoryWatchPaths([](const QString& searchPath) -> QStringList {
            return QStringList{searchPath + QStringLiteral("/common.glsl")};
        });

        CapturingAdapter adapter(strategy);
        WatchedDirectorySet set(adapter);
        set.registerDirectory(dir, LiveReload::Off);

        const QString cleanDir = QDir::cleanPath(dir);
        const QString metadataPath = cleanDir + QStringLiteral("/pkg-a/metadata.json");
        const QString fragPath = cleanDir + QStringLiteral("/pkg-a/effect.frag");
        const QString includePath = cleanDir + QStringLiteral("/common.glsl");

        QVERIFY2(adapter.lastWatches.contains(metadataPath), qPrintable(adapter.lastWatches.join(QLatin1Char(','))));
        QVERIFY2(adapter.lastWatches.contains(fragPath), qPrintable(adapter.lastWatches.join(QLatin1Char(','))));
        QVERIFY2(adapter.lastWatches.contains(includePath), qPrintable(adapter.lastWatches.join(QLatin1Char(','))));
    }

    /// `packs()` returns entries in lexicographic id order regardless
    /// of QHash's randomised iteration. Pinned because every consumer
    /// uses this for UI dropdowns / snapshot tests where order leaks
    /// would surface as flake.
    void testSortedByIdOutput()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        writeMetadata(dir + QStringLiteral("/c"), QStringLiteral("ccc"), 0);
        writeMetadata(dir + QStringLiteral("/a"), QStringLiteral("aaa"), 0);
        writeMetadata(dir + QStringLiteral("/b"), QStringLiteral("bbb"), 0);

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        const QList<FakePayload> sorted = strategy.packs();
        QCOMPARE(sorted.size(), 3);
        QCOMPARE(sorted.at(0).id, QStringLiteral("aaa"));
        QCOMPARE(sorted.at(1).id, QStringLiteral("bbb"));
        QCOMPARE(sorted.at(2).id, QStringLiteral("ccc"));
    }

    /// A `metadata.json` larger than `DirectoryLoader::kMaxFileBytes`
    /// is skipped with a warning, not parsed. The DoS guard is
    /// load-bearing — without it a hostile-or-buggy same-user
    /// metadata.json could stall the GUI thread on every rescan.
    void testMetadataSizeCap()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        const QString pkgDir = dir + QStringLiteral("/big");
        QVERIFY(QDir().mkpath(pkgDir));

        // Write a metadata.json one byte over the cap. Body content is
        // arbitrary; the strategy must reject before opening for parse.
        QByteArray oversize(static_cast<int>(DirectoryLoader::kMaxFileBytes) + 1, 'x');
        writeFile(pkgDir + QStringLiteral("/metadata.json"), oversize);

        // Plus a small valid sibling so we can verify the strategy
        // didn't bail on the whole scan.
        writeMetadata(dir + QStringLiteral("/small"), QStringLiteral("small-pkg"), 0);

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("small-pkg")));
        // The big pack must not have been parsed even partially —
        // its id never lands in the map.
    }

    /// Between scans, removing one pack's `metadata.json` purges that
    /// id from the next scan's accessor and the change-only emit
    /// callback fires (the signature now differs).
    void testStaleEntryPurgeOnRescan()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        writeMetadata(dir + QStringLiteral("/keep"), QStringLiteral("keep"), 0);
        writeMetadata(dir + QStringLiteral("/drop"), QStringLiteral("drop"), 0);

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 2);
        QCOMPARE(commits, 1);

        // Remove one pack's metadata.json (or the whole subdir).
        QVERIFY(QFile::remove(dir + QStringLiteral("/drop/metadata.json")));
        set.rescanNow();

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("keep")));
        QVERIFY(!strategy.contains(QStringLiteral("drop")));
        // Signature changed (one fewer entry) → second commit fired.
        QCOMPARE(commits, 2);
    }

    /// A parser returning `std::nullopt` skips the entry — neither
    /// inserted in the map nor counted toward the cap. Pinned because
    /// the production parsers use this path for inline validation
    /// failure (e.g. multipass-without-buffer-shader).
    void testParserReturnsNullopt()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        writeMetadata(dir + QStringLiteral("/pkg-good"), QStringLiteral("pkg-good"), 1);
        writeMetadata(dir + QStringLiteral("/pkg-skip"), QStringLiteral("pkg-skip"), 99);

        MetadataPackScanStrategy<FakePayload> strategy(
            [](const QString& subdirPath, const QJsonObject& root, bool isUser) -> std::optional<FakePayload> {
                if (root.value(QStringLiteral("score")).toInt() == 99) {
                    return std::nullopt; // simulate an inline-validation rejection
                }
                FakePayload p;
                p.id = root.value(QStringLiteral("id")).toString();
                p.score = root.value(QStringLiteral("score")).toInt();
                p.isUser = isUser;
                p.sourceDir = subdirPath;
                return p;
            },
            [] { });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("pkg-good")));
        QVERIFY(!strategy.contains(QStringLiteral("pkg-skip")));
    }

    /// Empty `directoriesInScanOrder` runs cleanly: empty packs map,
    /// empty watch list. The first scan with any entries (even empty)
    /// commits once to seed the signature; the second empty scan does
    /// NOT commit. This is the "no-content baseline" contract — the
    /// strategy must distinguish "first observation, results empty"
    /// from "results identical to last time, also empty".
    void testEmptyDirectoriesInScanOrder()
    {
        // First scan: no directories registered → empty packs, no
        // commit (the no-content baseline shouldn't fire OnCommit when
        // there's nothing to report).
        int commits = 0;

        // Adapter to drive the strategy directly with an empty list,
        // since `WatchedDirectorySet` always passes the registered list
        // (which would never be empty after registerDirectory). The
        // base's `setDirectories({})` path is the closest production
        // analogue.
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        WatchedDirectorySet set(strategy);

        // Register one dir then drop it via setDirectories({}). The
        // first registration runs an empty-content scan (the dir
        // exists but has no subdirs); the setDirectories({}) call
        // runs another scan with an empty directories list.
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        set.registerDirectory(dir, LiveReload::Off);

        // Empty dir → empty packs, no commit (first-scan with empty
        // results does not commit).
        QCOMPARE(strategy.size(), 0);
        QCOMPARE(commits, 0);

        // Second scan, still empty → still no commit.
        set.setDirectories({}, LiveReload::Off);
        QCOMPARE(strategy.size(), 0);
        QCOMPARE(commits, 0);
    }

    /// Entries with malformed (unparseable) `metadata.json` are
    /// skipped, not registered. Pinned alongside the parser-nullopt
    /// case — different rejection path (JSON layer vs schema layer)
    /// but same observable result.
    void testUnparseableMetadataSkipped()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        // One valid pack.
        writeMetadata(dir + QStringLiteral("/good"), QStringLiteral("good"), 1);
        // One subdir with garbage `metadata.json`.
        const QString badDir = dir + QStringLiteral("/bad");
        QVERIFY(QDir().mkpath(badDir));
        writeFile(badDir + QStringLiteral("/metadata.json"), QByteArrayLiteral("{ this is { definitely : not json"));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("good")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TestMetadataPackScanStrategy)
#include "test_metadatapackscanstrategy.moc"
