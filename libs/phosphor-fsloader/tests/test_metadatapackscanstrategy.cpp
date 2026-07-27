// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Direct coverage for `MetadataPackScanStrategy<Payload>` using a
// synthetic `FakePayload` POD. Pins the scaffolding contract — the
// reverse-iterate first-wins layering, per-rescan cap, SHA-1 change-only
// emit, sorted-by-id output, isUser classification, metadata-size cap,
// stale-entry purge — independently of any real consumer's schema. All
// three production consumers (ShaderPack, AnimationPack, SurfacePack, each
// hosted by PhosphorRegistry::MetadataPackLoader) sit on this strategy
// today; their own schema tests pin schema parsing, this file pins
// everything else.

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/MetadataPackScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <PhosphorFsLoader/DirectoryLoader.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

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

/// `[[nodiscard]] bool`, not a void helper containing QVERIFY: QVERIFY expands
/// to `if (!qVerify(...)) return;`, so a void fixture helper marks the test
/// failed and then hands control back to a caller that carries on against a
/// missing or empty file, turning one clear failure into a pile of misleading
/// secondary ones. Returning the outcome lets the call site QVERIFY and stop.
/// The write result is checked too, so a short or failed write is not silently
/// accepted. Same shape as `test_profileloader.cpp`'s helper.
[[nodiscard]] bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(bytes) == bytes.size();
}

[[nodiscard]] bool writeMetadata(const QString& subdirPath, const QString& id, int score,
                                 const QString& fragName = QString())
{
    if (!QDir().mkpath(subdirPath)) {
        return false;
    }
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("score"), score);
    if (!fragName.isEmpty()) {
        obj.insert(QLatin1String("fragmentShader"), fragName);
        // Materialise the referenced file too so per-entry watch
        // extraction can see something on disk.
        if (!writeFile(subdirPath + QLatin1Char('/') + fragName, QByteArrayLiteral("// frag\n"))) {
            return false;
        }
    }
    return writeFile(subdirPath + QStringLiteral("/metadata.json"), QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

/// Default parser: trivial `metadata.json` → `FakePayload`.
auto makeDefaultParser()
{
    return [](const QString& subdirPath, const QJsonObject& root, bool isUser) -> std::optional<FakePayload> {
        FakePayload p;
        p.id = root.value(QLatin1String("id")).toString();
        p.score = root.value(QLatin1String("score")).toInt(0);
        p.isUser = isUser;
        p.sourceDir = subdirPath;
        const QString frag = root.value(QLatin1String("fragmentShader")).toString();
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

        QVERIFY(writeMetadata(sysDir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/1));
        QVERIFY(writeMetadata(userDir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/2));

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
        QVERIFY(writeMetadata(userDir + QStringLiteral("/user-pkg"), QStringLiteral("user-pkg"), /*score=*/100));

        // System: 5 entries. Cap will be 3, so the system pass trips
        // after 2 (user pass already added 1).
        for (int i = 0; i < 5; ++i) {
            QVERIFY(writeMetadata(sysDir + QStringLiteral("/sys-pkg-%1").arg(i), QStringLiteral("sys-pkg-%1").arg(i),
                                  /*score=*/i));
        }

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setMaxEntries(3);
        strategy.setUserPath(userDir);

        WatchedDirectorySet set(strategy);
        set.registerDirectories({sysDir, userDir}, LiveReload::Off);

        // One commit for the whole registration, cap-trip included: a
        // truncated scan still commits its batch exactly once.
        QCOMPARE(commits, 1);
        QCOMPARE(strategy.size(), 3);
        // User entry must survive the cap-trip.
        QVERIFY(strategy.contains(QStringLiteral("user-pkg")));
        QCOMPARE(strategy.pack(QStringLiteral("user-pkg")).isUser, true);
        // Exactly two system entries got in (3 cap - 1 user); QDir::Name
        // sort means sys-pkg-0 + sys-pkg-1 should win, but we don't
        // pin which two — only that the count is right and the user
        // override is present.
    }

    /// The cap must bound subdirs CONSIDERED, not entries registered. A
    /// spray of packs whose metadata.json never parses registers nothing, so
    /// a registration-counting cap never trips and every one of them is
    /// stat'd, opened, read and parsed on the GUI thread on every watcher
    /// fire — the exact attack the guard names. It also armed a filesystem
    /// watch per broken pack before any of the reject paths ran, so the spray
    /// grew the watch set without bound too, toward the inotify per-user
    /// limit.
    ///
    /// The watch list is the cheapest observable for both halves: it is
    /// appended once per subdir the loop actually reaches.
    void testCapBoundsSubdirsConsideredNotEntriesRegistered()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("broken"));
        QVERIFY(QDir().mkpath(dir));

        constexpr int kCap = 5;
        constexpr int kSubdirs = 20;
        for (int i = 0; i < kSubdirs; ++i) {
            const QString sub = dir + QStringLiteral("/pkg-%1").arg(i, 3, 10, QLatin1Char('0'));
            QVERIFY(QDir().mkpath(sub));
            QVERIFY(writeFile(sub + QStringLiteral("/metadata.json"), QByteArrayLiteral("{ nope")));
        }

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        strategy.setMaxEntries(kCap);

        CapturingAdapter adapter(strategy);
        WatchedDirectorySet set(adapter);
        set.registerDirectory(dir, LiveReload::Off);

        // Nothing parses, so nothing registers either way — that is precisely
        // why a registration count could not see this.
        QCOMPARE(strategy.size(), 0);
        // Exact equality works as a proxy for "subdirs considered" because each
        // broken subdir here HAS a metadata.json, so it contributes exactly one
        // watch path. A subdir with no metadata.json contributes the directory
        // instead (see testSubdirWithoutMetadataIsWatched), still one entry.
        QVERIFY2(adapter.lastWatches.size() == kCap,
                 qPrintable(QStringLiteral("cap let %1 broken subdirs arm a watch with a cap of %2")
                                .arg(adapter.lastWatches.size())
                                .arg(kCap)));
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
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/7));

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
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), /*score=*/42));
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

        QVERIFY(writeMetadata(sysDir + QStringLiteral("/sys-pkg"), QStringLiteral("sys-pkg"), 1));
        QVERIFY(writeMetadata(userDir + QStringLiteral("/user-pkg"), QStringLiteral("user-pkg"), 2));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        strategy.setUserPath(userDir);

        WatchedDirectorySet set(strategy);
        // Canonical lowest-first: sys then user.
        set.registerDirectories({sysDir, userDir}, LiveReload::Off);

        // Anchor existence first: `pack()` returns a default-constructed payload
        // (isUser == false) for an id it does not hold, so the isUser assertion
        // alone is satisfied by a regression that drops the system path entirely.
        QCOMPARE(strategy.size(), 2);
        QVERIFY(strategy.contains(QStringLiteral("sys-pkg")));
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
        QVERIFY(
            writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0, QStringLiteral("effect.frag")));
        // Top-level shared file in the search path itself, not in any
        // pack subdir. Mirrors how the shader-pack registry adds
        // `common.glsl` to the watch list.
        const QString sharedInclude = dir + QStringLiteral("/common.glsl");
        QVERIFY(writeFile(sharedInclude, QByteArrayLiteral("// shared\n")));

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
        QVERIFY(writeMetadata(dir + QStringLiteral("/c"), QStringLiteral("ccc"), 0));
        QVERIFY(writeMetadata(dir + QStringLiteral("/a"), QStringLiteral("aaa"), 0));
        QVERIFY(writeMetadata(dir + QStringLiteral("/b"), QStringLiteral("bbb"), 0));

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

        // Structurally VALID JSON, padded past the cap — not a run of 'x'.
        // Junk bytes are rejected by the parser whether or not the size guard
        // exists, so a junk payload makes this test pass with the guard deleted
        // and proves nothing. Valid oversize JSON separates the two: with the
        // guard, size() stays 1; without it, the pack parses and size() is 2.
        // Same construction, and the same reason, as
        // `test_directoryloader.cpp::testOversizedFileIsSkipped`.
        QJsonObject bigObj;
        bigObj.insert(QLatin1String("id"), QStringLiteral("big-pkg"));
        bigObj.insert(QLatin1String("score"), 0);
        const qint64 cap = DirectoryLoader::kMaxFileBytes;
        bigObj.insert(QLatin1String("pad"), QString(static_cast<int>(cap), QLatin1Char('x')));
        const QByteArray oversize = QJsonDocument(bigObj).toJson(QJsonDocument::Compact);
        QVERIFY(oversize.size() > cap);
        QVERIFY(writeFile(pkgDir + QStringLiteral("/metadata.json"), oversize));

        // Plus a small valid sibling so we can verify the strategy
        // didn't bail on the whole scan.
        QVERIFY(writeMetadata(dir + QStringLiteral("/small"), QStringLiteral("small-pkg"), 0));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("small-pkg")));
        // The big pack is well-formed and declares a usable id, so the ONLY
        // thing keeping it out of the map is the size guard.
        QVERIFY(!strategy.contains(QStringLiteral("big-pkg")));
    }

    /// A subdirectory with no `metadata.json` yet is watched via the DIRECTORY,
    /// because `QFileSystemWatcher` cannot watch a file that does not exist. Only
    /// the registered search paths get directory watches, so without this a
    /// `cp -r mypack <packs>/` whose mkdir wakes the debounced scan before the
    /// metadata lands leaves the pack invisible until an unrelated rescan.
    ///
    /// Deleting `desiredWatches.append(subdirPath)` drops the subdir from the
    /// returned list and fails the contains() below.
    void testSubdirWithoutMetadataIsWatched()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        const QString emptyPack = dir + QStringLiteral("/pkg-pending");
        QVERIFY(QDir().mkpath(emptyPack));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-ready"), QStringLiteral("pkg-ready"), 0));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        CapturingAdapter adapter(strategy);
        WatchedDirectorySet set(adapter);
        set.registerDirectory(dir, LiveReload::Off);

        // The ready pack registered; the pending one did not.
        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("pkg-ready")));

        // But the pending pack's DIRECTORY is watched, so the metadata.json
        // landing inside it will wake the next rescan.
        QVERIFY2(adapter.lastWatches.contains(QDir::cleanPath(emptyPack)),
                 "a subdir with no metadata.json is not watched, so its metadata landing fires nothing");
    }

    /// Between scans, removing one pack's `metadata.json` purges that
    /// id from the next scan's accessor and the change-only emit
    /// callback fires (the signature now differs).
    void testStaleEntryPurgeOnRescan()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/keep"), QStringLiteral("keep"), 0));
        QVERIFY(writeMetadata(dir + QStringLiteral("/drop"), QStringLiteral("drop"), 0));

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

    /// A parser returning `std::nullopt` skips the entry: it is not
    /// inserted in the map. It IS counted toward the cap, because
    /// `subdirsConsidered` is incremented before the parser runs — the
    /// stat, open and JSON parse it already cost are exactly the work the
    /// cap exists to bound. See
    /// `testCapBoundsSubdirsConsideredNotEntriesRegistered` above. Pinned
    /// because the production parsers use this path for inline validation
    /// failure (e.g. multipass-without-buffer-shader).
    void testParserReturnsNullopt()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-good"), QStringLiteral("pkg-good"), 1));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-skip"), QStringLiteral("pkg-skip"), 99));

        MetadataPackScanStrategy<FakePayload> strategy(
            [](const QString& subdirPath, const QJsonObject& root, bool isUser) -> std::optional<FakePayload> {
                if (root.value(QLatin1String("score")).toInt() == 99) {
                    return std::nullopt; // simulate an inline-validation rejection
                }
                FakePayload p;
                p.id = root.value(QLatin1String("id")).toString();
                p.score = root.value(QLatin1String("score")).toInt();
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
    /// empty watch list, and no commit on either scan here. This is the
    /// "no-content baseline" contract. `changed` is
    /// `isFirstScan ? !fresh.isEmpty() : signature != m_lastSignature`, so an
    /// empty result set cannot commit on the FIRST scan, and cannot commit on a
    /// later scan whose signature is IDENTICAL to the previous one. Note those
    /// are not the same condition: the signature also mixes the watch-set
    /// fingerprint, and a rejected subdir still arms a metadata.json watch, so
    /// editing one broken pack shifts the signature and commits with an empty
    /// result on both sides. It also commits when the previous scan was not
    /// empty, which is the purge case pinned by
    /// `testEmptyScanAfterNonEmptyCommitsThePurge` below.
    void testEmptyDirectoriesInScanOrder()
    {
        // First scan: a registered directory that holds no pack subdirs → empty
        // packs, no commit (the no-content baseline shouldn't fire OnCommit
        // when there's nothing to report).
        int commits = 0;

        // `setDirectories({})` is used rather than calling performScan with
        // an empty list directly: `WatchedDirectorySet` always passes its
        // registered list, so it is the closest production analogue.
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

    /// The other half of the no-content-baseline contract: a scan that DROPS
    /// content is a change and must commit, even though the result set it
    /// lands on is empty. Without this, `testEmptyDirectoriesInScanOrder`
    /// alone is satisfied by a strategy that never commits for an empty
    /// result under any circumstances — e.g. a regression narrowing `changed`
    /// to `!fresh.isEmpty() && ...` would keep every other slot green.
    void testEmptyScanAfterNonEmptyCommitsThePurge()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 1));

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        WatchedDirectorySet set(strategy);

        set.registerDirectory(dir, LiveReload::Off);
        QCOMPARE(strategy.size(), 1);
        QCOMPARE(commits, 1);

        // Drop every directory. The result set is empty, but it differs from
        // the signature already seeded, so this IS a commit.
        set.setDirectories({}, LiveReload::Off);
        QCOMPARE(strategy.size(), 0);
        QCOMPARE(commits, 2);
    }

    /// Editing a `metadata.json` field that the parser consumes but
    /// the SignatureContrib does NOT fingerprint must still trip
    /// `OnCommit`. The strategy's own contribution mixes in the
    /// `metadata.json`'s size+mtime per entry, so any byte-changing
    /// edit is caught regardless of whether the contributor enumerates
    /// the affected field. Without this contract the consumer's
    /// content-changed signal silently lies about edits to
    /// metadata-only display fields (e.g. `description`, `category`,
    /// `parameters[].name`, etc.).
    void testMetadataMtimeFingerprintCatchesUntrackedFieldEdit()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        const QString pkgDir = dir + QStringLiteral("/pkg-a");
        QVERIFY(QDir().mkpath(pkgDir));

        // Initial metadata: id=pkg-a, score=0, plus a "displayName"
        // field which the parser stores in `sourceDir` (we abuse a
        // FakePayload field to record the parsed value) but the
        // contributor below does NOT fingerprint.
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("pkg-a"));
        obj.insert(QLatin1String("score"), 0);
        obj.insert(QLatin1String("displayName"), QStringLiteral("First"));
        QVERIFY(
            writeFile(pkgDir + QStringLiteral("/metadata.json"), QJsonDocument(obj).toJson(QJsonDocument::Compact)));

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(
            [](const QString& /*subdirPath*/, const QJsonObject& root, bool isUser) -> std::optional<FakePayload> {
                FakePayload p;
                p.id = root.value(QLatin1String("id")).toString();
                p.score = root.value(QLatin1String("score")).toInt(0);
                p.isUser = isUser;
                // Stash displayName in sourceDir so the test can verify
                // the parsed value updated even when the signature
                // contributor doesn't fingerprint it.
                p.sourceDir = root.value(QLatin1String("displayName")).toString();
                return p;
            },
            [&]() {
                ++commits;
            });
        // Contributor INTENTIONALLY does NOT fingerprint displayName
        // (or score). Without the strategy's metadata-mtime mix-in the
        // edit below would not change the signature.
        strategy.setSignatureContrib([](QCryptographicHash& h, const FakePayload& p) {
            h.addData(p.isUser ? "u" : "s");
        });

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);
        QCOMPARE(commits, 1);
        QCOMPARE(strategy.pack(QStringLiteral("pkg-a")).sourceDir, QStringLiteral("First"));

        // QFileSystemWatcher mtime-resolution edge: ensure the rewrite
        // produces a mtime distinguishable from the original. Sleeping
        // 10ms is enough on every filesystem we care about; the
        // strategy mixes the file SIZE in too, so even on filesystems
        // that round mtime to the second we'd still get a different
        // signature from the byte-count change. Make the byte count
        // differ explicitly to be belt-and-braces.
        QTest::qWait(10);

        // Edit only `displayName` — score unchanged (so the
        // contributor's view is identical), but bytes change so
        // metadata.json size+mtime shift.
        obj[QLatin1String("displayName")] = QStringLiteral("Second-edition");
        QVERIFY(
            writeFile(pkgDir + QStringLiteral("/metadata.json"), QJsonDocument(obj).toJson(QJsonDocument::Compact)));

        set.rescanNow();

        // Commit MUST fire because the strategy mixes metadata
        // size+mtime into the signature. Without that mix-in the
        // contributor (which we deliberately wrote to NOT see
        // displayName) would have produced an identical signature.
        QCOMPARE(commits, 2);
        QCOMPARE(strategy.pack(QStringLiteral("pkg-a")).sourceDir, QStringLiteral("Second-edition"));
    }

    /// `setPerSubdirSkip` lets the caller reserve a sentinel subdirectory
    /// name (the shader-pack registry uses `none` for "no shader"). The
    /// strategy must skip such subdirs entirely — neither parsing the
    /// `metadata.json` (so a malformed one inside a skipped subdir
    /// doesn't even warn) nor counting against the per-rescan cap.
    void testPerSubdirSkip()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        // One legit pack.
        QVERIFY(writeMetadata(dir + QStringLiteral("/keep"), QStringLiteral("keep"), 1));
        // One pack inside the sentinel subdir — would be valid if not skipped, so
        // this test fails loudly if skip is broken.
        //
        // Named to sort FIRST under QDir::Name, which is what makes the cap leg
        // below bite. Enumeration order is alphabetical, and the cap is charged
        // BEFORE the skip predicate runs, so a sentinel sorting second would trip
        // the cap and `break` without the predicate ever being consulted — the
        // slot would then pass whether or not the skip existed.
        QVERIFY(writeMetadata(dir + QStringLiteral("/aaa-none"), QStringLiteral("none-pkg"), 2));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        strategy.setPerSubdirSkip([](const QString& name) {
            return name == QLatin1String("aaa-none");
        });
        // TWO, not one. The skipped subdir IS charged against the cap — the
        // strategy documents and implements charge-before-skip deliberately, so a
        // caller-supplied predicate cannot be work a spray buys for free. A cap of
        // one would therefore stop at the sentinel and never reach `keep`,
        // regardless of the predicate. With two, a broken skip registers BOTH packs
        // and arms two watches, so the assertions below bite.
        strategy.setMaxEntries(2);

        CapturingAdapter adapter(strategy);
        WatchedDirectorySet set(adapter);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("keep")));
        QVERIFY(!strategy.contains(QStringLiteral("none-pkg")));

        // NEVER REACHED, not merely never registered. The watch set is this
        // file's established proxy for "the subdir was considered" (see
        // testCapBoundsSubdirsConsideredNotEntriesRegistered), so a skip
        // implemented as "parse it, then discard the result" would show up here
        // as a second watch path. Without this leg only the non-registration was
        // pinned, and two of the three claims in the doc above were untested.
        for (const QString& watch : adapter.lastWatches) {
            QVERIFY2(!watch.contains(QStringLiteral("/aaa-none")),
                     qPrintable(QStringLiteral("a skipped subdir was still reached: ") + watch));
        }
        QCOMPARE(adapter.lastWatches.size(), 1);
    }

    /// Setting the user path *after* `registerDirectories` must
    /// reclassify already-discovered entries — the prior scan baked in
    /// the OLD (empty) user path, so every entry was `isUser=false`.
    /// `MetadataPackLoader::setUserPath` triggers a synchronous
    /// rescan when directories are registered; we drive the strategy
    /// through `WatchedDirectorySet` directly here so the test pins
    /// the strategy contract independently of the base wrapper.
    void testSetUserPathReclassifiesAfterRegistration()
    {
        const QString userDir = m_tmp->filePath(QStringLiteral("user"));
        QVERIFY(QDir().mkpath(userDir));
        QVERIFY(writeMetadata(userDir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 1));

        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [] { });
        WatchedDirectorySet set(strategy);

        // First scan with no user path configured — entry should be
        // classified as system.
        set.registerDirectory(userDir, LiveReload::Off);
        QCOMPARE(strategy.size(), 1);
        QCOMPARE(strategy.pack(QStringLiteral("pkg-a")).isUser, false);

        // Set the user path now. The strategy reads `m_userPath` at the
        // top of every `performScan`, so a subsequent rescan picks up
        // the new value and reclassifies. This mirrors the production
        // wrapper's `setUserPath` → `rescanNow` sequence.
        strategy.setUserPath(userDir);
        set.rescanNow();

        QCOMPARE(strategy.size(), 1);
        QCOMPARE(strategy.pack(QStringLiteral("pkg-a")).isUser, true);
    }

    /// Top-level shared files returned by `PerDirectoryWatchPaths` (the
    /// shader-pack registry's `common.glsl`, `audio.glsl`, etc.) must
    /// auto-fingerprint into the per-rescan signature. Without this, an
    /// edit to a top-level shared include fires the watcher rescan but
    /// the signature stays stable and `OnCommit` is silenced — consumers
    /// hold stale GPU state. Pinned with a NULL `SignatureContrib` so
    /// the strategy's own watch-set auto-mix is the only thing that can
    /// catch the edit.
    void testTopLevelSharedFileFingerprint()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        const QString sharedInclude = dir + QStringLiteral("/common.glsl");
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0));
        QVERIFY(writeFile(sharedInclude, QByteArrayLiteral("// shared v1\n")));

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setPerDirectoryWatchPaths([&sharedInclude](const QString&) {
            return QStringList{sharedInclude};
        });
        // Deliberately NO `SignatureContrib` — only the strategy's
        // watch-set auto-mix can catch the top-level file edit.

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);
        QCOMPARE(commits, 1);

        // Edit the top-level shared file. Distinct byte count guarantees
        // the size mix-in shifts even on filesystems that round mtime to
        // the second; the qWait pads the mtime resolution edge for those
        // that don't round.
        QTest::qWait(10);
        QVERIFY(writeFile(sharedInclude, QByteArrayLiteral("// shared v2 (different bytes)\n")));
        set.rescanNow();

        QCOMPARE(commits, 2);
    }

    /// Per-pack auxiliary files returned by `PerEntryWatchPaths` (a
    /// pack's `helpers.glsl` referenced via `#include` from main frag,
    /// auxiliary `extras.frag`, etc.) must auto-fingerprint into the
    /// per-rescan signature for the same reason as
    /// `testTopLevelSharedFileFingerprint`. Pinned with a NULL
    /// `SignatureContrib` so the strategy's own watch-set auto-mix is
    /// the only thing that can catch the edit.
    void testPerEntryAuxiliaryFileFingerprint()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(
            writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0, QStringLiteral("effect.frag")));
        // Sibling file the parser doesn't reference but the per-entry
        // watch callback exposes — production analogue is a pack's
        // `helpers.glsl` only reachable via `#include` from `effect.frag`.
        const QString helpersPath = dir + QStringLiteral("/pkg-a/helpers.glsl");
        QVERIFY(writeFile(helpersPath, QByteArrayLiteral("// helpers v1\n")));

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setPerEntryWatchPaths([&helpersPath](const FakePayload& p) -> QStringList {
            QStringList out;
            if (!p.fragmentShaderPath.isEmpty()) {
                out.append(p.fragmentShaderPath);
            }
            out.append(helpersPath);
            return out;
        });
        // Deliberately NO `SignatureContrib`.

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);
        QCOMPARE(commits, 1);

        QTest::qWait(10);
        QVERIFY(writeFile(helpersPath, QByteArrayLiteral("// helpers v2 (different bytes)\n")));
        set.rescanNow();

        QCOMPARE(commits, 2);
    }

    /// `setMaxEntries(0)` is a degenerate but legal cap: the strategy
    /// must register zero entries and not commit (no-content baseline).
    /// Pinned because the cap-trip check (`subdirsConsidered >= m_maxEntries`)
    /// triggers on every iteration when `cap == 0`; if a future change
    /// flips `>=` to `>`, the guard silently disappears and the cap is
    /// off-by-one. Boundary test catches that regression.
    void testSetMaxEntriesZero()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-b"), QStringLiteral("pkg-b"), 0));

        int commits = 0;
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), [&]() {
            ++commits;
        });
        strategy.setMaxEntries(0);

        WatchedDirectorySet set(strategy);
        set.registerDirectory(dir, LiveReload::Off);

        QCOMPARE(strategy.size(), 0);
        QCOMPARE(commits, 0); // no-content baseline does not commit
    }

    /// A re-entrant `OnCommit` (production analogue: a slot connected to
    /// the consumer's content-changed signal calling `refresh()`
    /// synchronously) must not corrupt strategy state. The
    /// `WatchedDirectorySet` reentry counter handles the orchestration;
    /// this test pins the strategy-level invariant that the inner scan's
    /// `m_packs` / `m_lastSignature` state survives unwind. The first
    /// scan commits and re-enters; the re-entrant scan finds identical
    /// state and does NOT commit again. Without the WatchedDirectorySet
    /// guard, the inner scan could clobber the outer's state mid-stream.
    void testReentrantOnCommit()
    {
        const QString dir = m_tmp->filePath(QStringLiteral("d"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeMetadata(dir + QStringLiteral("/pkg-a"), QStringLiteral("pkg-a"), 0));

        int commits = 0;
        bool reentered = false;
        WatchedDirectorySet* setPtr = nullptr;
        auto onCommit = [&]() {
            ++commits;
            // Re-enter the watcher exactly once — production code
            // typically gates re-entry on a "first time" flag like this.
            if (!reentered) {
                reentered = true;
                setPtr->rescanNow();
            }
        };
        MetadataPackScanStrategy<FakePayload> strategy(makeDefaultParser(), onCommit);
        WatchedDirectorySet set(strategy);
        setPtr = &set;

        set.registerDirectory(dir, LiveReload::Off);

        QVERIFY(reentered);
        // Trace: the outer scan computes signature S1, stores it, and
        // calls OnCommit (commits == 1). OnCommit re-enters via
        // `rescanNow()`, which is a direct synchronous call into
        // `rescanAll` with no reentry guard (`requestRescan` is the
        // path that defers; `rescanNow` does not). The inner scan
        // observes identical filesystem state and recomputes the same
        // signature S1; `m_signatureSeeded` is already true (set by
        // the outer just before its OnCommit call), so the inner
        // takes the `signature != m_lastSignature` branch, finds
        // them equal, and does NOT fire OnCommit again.
        //
        // The contract being pinned here is twofold: re-entry doesn't
        // crash or strand the outer's `m_packs` half-mutated, and
        // change-only emit semantics survive the recursion (a
        // re-entrant scan with identical content does not re-fire
        // OnCommit). Tightened from `>= 1` to `== 1` after the
        // recursion path was traced — the loose form would mask a
        // regression that re-fired OnCommit on the inner scan.
        QCOMPARE(strategy.size(), 1);
        QVERIFY(strategy.contains(QStringLiteral("pkg-a")));
        QCOMPARE(commits, 1);
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
        QVERIFY(writeMetadata(dir + QStringLiteral("/good"), QStringLiteral("good"), 1));
        // One subdir with garbage `metadata.json`.
        const QString badDir = dir + QStringLiteral("/bad");
        QVERIFY(QDir().mkpath(badDir));
        QVERIFY(writeFile(badDir + QStringLiteral("/metadata.json"),
                          QByteArrayLiteral("{ this is { definitely : not json")));

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
