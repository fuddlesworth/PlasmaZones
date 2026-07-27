// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorFsLoader/JsonEnvelopeValidator.h>

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace PhosphorAnimation;

namespace {
/// Own category so the helper's diagnostics do not have to borrow the
/// production loader's, which this slot does not go through.
Q_LOGGING_CATEGORY(lcEnvelopeTest, "phosphoranimation.test.envelope")
} // namespace

class TestProfileLoader : public QObject
{
    Q_OBJECT

private:
    CurveRegistry m_curveRegistry;
    /// Per-test-fixture profile registry. Phase A3 of the architecture
    /// refactor retired `m_profileRegistry` —
    /// composition roots and tests own their own registries. Each test
    /// method gets a freshly cleared registry via init() / cleanup().
    PhosphorProfileRegistry m_profileRegistry;
    /// Returns false instead of QVERIFY-ing, so the failure stops the caller.
    /// QVERIFY expands to `if (!...) return;`, which in a void helper does mark
    /// the test failed (qVerify routes to QTestResult::addFailure) but then
    /// hands control back to a fixture that carries on against an empty
    /// directory — turning one clear failure into a pile of misleading
    /// secondary ones from tests that found nothing to reject. Call sites must
    /// wrap this in QVERIFY.
    [[nodiscard]] bool writeFile(const QString& path, const QString& contents) const
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        QTextStream s(&f);
        s << contents;
        s.flush();
        return f.error() == QFileDevice::NoError;
    }

private Q_SLOTS:
    void init()
    {
        m_profileRegistry.clear();
    }

    /// Guarantees no PROFILE-registry state leaks between test methods even
    /// when a test forgets to clean up after itself. `init()` already
    /// clears on entry, but pairing with `cleanup()` keeps the
    /// guarantee end-to-end rather than entry-only. The curve registry is
    /// deliberately not cleared: no test here registers a user curve, and
    /// the builtin specs the profiles reference are resolved by value.
    void cleanup()
    {
        m_profileRegistry.clear();
    }

    /// Missing directory is a no-op.
    void testMissingDirectoryIsNoOp()
    {
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        const int n = loader.loadFromDirectory(QStringLiteral("/no/such/dir"));
        QCOMPARE(n, 0);
        QCOMPARE(m_profileRegistry.profileCount(), 0);
    }

    /// Valid profile JSON — path from name field, profile fields
    /// round-trip through Profile::fromJson.
    void testLoadValidProfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("overlay.fade.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "curve": "spring:14.0,0.6",
            "duration": 250,
            "minDistance": 0
        })")));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 1);

        auto resolved = m_profileRegistry.resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 250.0);
        QVERIFY(resolved->curve);
    }

    /// Missing name field — skipped.
    void testMissingNameIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("nameless.json")), QStringLiteral(R"({
            "duration": 250
        })")));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", broken")));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// User-overrides-system collision pattern. Loads the
    /// system dir first (lower priority), then user dir overrides on
    /// the same name. systemSourcePath tracked on the user entry for
    /// future restore.
    void testUserOverridesSystem()
    {
        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid());
        QVERIFY(userDir.isValid());
        QVERIFY(writeFile(systemDir.filePath(QStringLiteral("x.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 100
        })")));
        QVERIFY(writeFile(userDir.filePath(QStringLiteral("x.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 500
        })")));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        auto resolved = m_profileRegistry.resolve(QStringLiteral("x"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().sourcePath, userDir.filePath(QStringLiteral("x.json")));
        QCOMPARE(entries.first().systemSourcePath, systemDir.filePath(QStringLiteral("x.json")));
    }

    /// The `name` field becomes the registry path and `presetName` is a
    /// user-assigned label. Two distinct concepts, and neither may stand in
    /// for the other.
    ///
    /// The second file is the load-bearing half: it carries a `name` and NO
    /// `presetName`, so a parser that ever started treating the routing key as
    /// a preset label would engage the optional here and fail the assertion.
    /// The first file only pins that an explicit label survives the envelope
    /// strip.
    ///
    /// This does NOT cover the strip itself — `Profile::fromJson` never reads a
    /// `name` key, so removing the strip is unobservable from here. The strip
    /// has its own slot below.
    void testNameDoesNotLeakIntoPresetName()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("overlay.fade.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "presetName": "My Overlay Preset",
            "duration": 150
        })")));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("overlay.slide.json")), QStringLiteral(R"({
            "name": "overlay.slide",
            "duration": 150
        })")));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());
        auto resolved = m_profileRegistry.resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->presetName.value_or(QString()), QStringLiteral("My Overlay Preset"));

        auto unlabelled = m_profileRegistry.resolve(QStringLiteral("overlay.slide"));
        QVERIFY(unlabelled.has_value());
        QVERIFY2(!unlabelled->presetName.has_value(),
                 "a profile with no presetName came back labelled — the registry path leaked into it");
    }

    /// `profilesChanged` fires on a rescan that sees content change on
    /// disk. Asserts the post-rescan resolved value matches the new file
    /// so a broken parser can't pass the spy assertion by registering
    /// empty profiles.
    void testRescanFiresSignal_whenContentChanged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("a.json"));
        QVERIFY(writeFile(path, QStringLiteral(R"({
            "name": "a",
            "duration": 123,
            "minDistance": 7
        })")));
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        auto resolved = m_profileRegistry.resolve(QStringLiteral("a"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(-1.0), 123.0);
        QCOMPARE(resolved->minDistance.value_or(-1), 7);

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        // Mutate the file on disk — next rescan sees new values.
        QVERIFY(QFile::remove(path));
        QVERIFY(writeFile(path, QStringLiteral(R"({
            "name": "a",
            "duration": 456,
            "minDistance": 9
        })")));

        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);

        // Round-trip sanity: the registry reflects the new values.
        auto reResolved = m_profileRegistry.resolve(QStringLiteral("a"));
        QVERIFY(reResolved.has_value());
        QCOMPARE(reResolved->duration.value_or(-1.0), 456.0);
        QCOMPARE(reResolved->minDistance.value_or(-1), 9);
    }

    /// `rescanNow()` must commit before it returns — no event loop, no
    /// QTRY_. That is the whole reason it exists next to the debounced
    /// `requestRescan()`: a consumer that writes a profile file and reads
    /// the registry back in the same call cannot wait for a timer.
    void testRescanNowCommitsSynchronously()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sync.json"));
        QVERIFY(writeFile(path, QStringLiteral(R"({
            "name": "sync",
            "duration": 111
        })")));
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path(), LiveReload::On);

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        QVERIFY(QFile::remove(path));
        QVERIFY(writeFile(path, QStringLiteral(R"({
            "name": "sync",
            "duration": 222
        })")));

        loader.rescanNow();
        // Both assertions run on the same stack as the call above.
        QCOMPARE(spy.count(), 1);
        const auto resolved = m_profileRegistry.resolve(QStringLiteral("sync"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(-1.0), 222.0);

        // A removal is visible just as immediately — the case the settings
        // app's per-field revert links depend on — and it is a change, so the
        // consumer signal is owed for it too.
        QVERIFY(QFile::remove(path));
        loader.rescanNow();
        QCOMPARE(spy.count(), 2);
        QVERIFY(!m_profileRegistry.resolve(QStringLiteral("sync")).has_value());
    }

    /// `rescanNow()` on a loader with nothing registered still COMMITS —
    /// an empty set, under its own owner tag. That is the dangerous shape,
    /// so pin what it may and may not touch: a pre-existing entry under the
    /// loader's OWN tag is the loader's to drop, while another owner's entry
    /// must survive untouched. And with nothing tracked to begin with, no
    /// consumer signal is owed.
    void testRescanNowWithNoDirectoriesCommitsOnlyItsOwnPartition()
    {
        m_profileRegistry.registerProfile(QStringLiteral("mine"), Profile{}, QStringLiteral("test"));
        m_profileRegistry.registerProfile(QStringLiteral("theirs"), Profile{}, QStringLiteral("someone-else"));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        loader.rescanNow();

        QCOMPARE(loader.registeredCount(), 0);
        // Same tag, so the empty commit owns it and takes it.
        QVERIFY(!m_profileRegistry.resolve(QStringLiteral("mine")).has_value());
        // Different owner, so it is not this loader's to remove.
        QVERIFY(m_profileRegistry.resolve(QStringLiteral("theirs")).has_value());
        // The loader tracked nothing before and tracks nothing now.
        QCOMPARE(spy.count(), 0);
    }

    /// The re-entrancy guard in `ProfileLoader::Sink::commitBatch`: a
    /// consumer that calls `rescanNow()` from a registry `profileChanged`
    /// handler re-enters the commit, and the nested batch must NOT clear the
    /// evidence the outer one gathered. Without the depth guard the outer
    /// `entriesChanged` reads a cleared flag and swallows `profilesChanged`
    /// for a change that really happened.
    void testNestedRescanDoesNotSwallowTheOuterChangeSignal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // `reentered` is declared BEFORE `loader` on purpose. The lambda below
        // captures it by reference and stays connected until the scope guard
        // fires; ~ProfileLoader ends in clearOwner, which emits profileChanged
        // synchronously while the destructor body runs, so a lambda outliving
        // `reentered` would read a dead stack slot. Reverse-declaration-order
        // destruction is what keeps that impossible.
        bool reentered = false;
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        // One-shot: re-enter exactly once, or the recursion is unbounded
        // (rescanNow deliberately does not defer — see its header docs).
        const auto conn =
            connect(&m_profileRegistry, &PhosphorProfileRegistry::profileChanged, &loader, [&loader, &reentered]() {
                if (reentered) {
                    return;
                }
                reentered = true;
                loader.rescanNow();
            });

        // Torn down UNCONDITIONALLY, on every exit from this slot.
        // ~ProfileLoader ends in clearOwner, which emits profileChanged
        // synchronously while the destructor body runs; a connection still live
        // there would re-enter a half-destroyed loader and re-register the very
        // entries clearOwner just removed. A plain `disconnect()` after the
        // assertions is not enough, and neither is one placed before them —
        // every QVERIFY in this slot returns early, including the fixture write
        // below, so only a scope guard covers all of them.
        const auto disconnector = qScopeGuard([&conn]() {
            QObject::disconnect(conn);
        });

        QVERIFY(writeFile(dir.filePath(QStringLiteral("nested.json")), QStringLiteral(R"({
            "name": "nested",
            "duration": 321
        })")));
        loader.rescanNow();

        QVERIFY2(reentered, "the nested path never ran, so this test proves nothing");
        // Exactly two: the nested commit's entriesChanged reads the flag while
        // it still carries the outer batch's evidence, then the outer one
        // fires as well. That duplicate is the documented accepted cost of the
        // depth guard (see Sink::lastBatchChanged) — pinned here so a change
        // that collapses it cannot leave that comment silently stale.
        QCOMPARE(spy.count(), 2);
        QVERIFY(m_profileRegistry.resolve(QStringLiteral("nested")).has_value());
    }

    /// `profilesChanged` must NOT fire on a no-op rescan — the
    /// commitBatch diff in ProfileLoader::Sink suppresses the consumer
    /// signal when the re-parsed profile set is identical to the
    /// previous commit.
    void testRescanDoesNotFireSignal_whenContentUnchanged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("stable.json")), QStringLiteral(R"({
            "name": "stable",
            "duration": 200,
            "minDistance": 5
        })")));
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        // Attach spy AFTER initial load — only interested in the
        // rescan-produced signal (or its absence).
        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        loader.requestRescan();
        QVERIFY2(!spy.wait(300),
                 "profilesChanged fired on a no-op rescan — commitBatch diff did not suppress unchanged content");
        QCOMPARE(spy.count(), 0);
    }

    /// Partitioned-ownership contract: a daemon-direct registration
    /// (empty owner) MUST survive a ProfileLoader rescan that doesn't
    /// mention the same path. Direct-wipe-on-rescan was the PR-344
    /// blocker that caused daemon fan-out profiles to be evicted.
    void testLoaderDoesNotWipeDaemonDirectEntries()
    {
        auto& reg = m_profileRegistry;

        // Daemon publishes a "direct" (untagged) entry first.
        Profile daemonProfile;
        daemonProfile.duration = 42.0;
        reg.registerProfile(QStringLiteral("Global"), daemonProfile);
        QVERIFY(reg.hasProfile(QStringLiteral("Global")));
        QCOMPARE(reg.ownerOf(QStringLiteral("Global")), QString());

        // User drops ONE profile JSON for a different path.
        QTemporaryDir userDir;
        QVERIFY(userDir.isValid());
        QVERIFY(writeFile(userDir.filePath(QStringLiteral("Zone.json")),
                          QStringLiteral(R"({"name": "Zone", "duration": 200})")));
        ProfileLoader loader(reg, m_curveRegistry, QStringLiteral("test-user-profiles"));
        loader.loadFromDirectory(userDir.path());

        // Loader's rescan must NOT have touched the direct "Global" entry.
        QVERIFY(reg.hasProfile(QStringLiteral("Global")));
        auto resolvedDaemon = reg.resolve(QStringLiteral("Global"));
        QVERIFY(resolvedDaemon.has_value());
        QCOMPARE(resolvedDaemon->duration.value_or(-1.0), 42.0);

        // And loader-owned "Zone" is there under its owner tag.
        QCOMPARE(reg.ownerOf(QStringLiteral("Zone")), QStringLiteral("test-user-profiles"));
    }

    /// Delete-user-restores-system end-to-end test. The PR reviewers
    /// flagged that the documentation claimed this works but no test
    /// actually exercises it.
    void testDeleteUserRestoresSystem()
    {
        auto& reg = m_profileRegistry;

        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid());
        QVERIFY(userDir.isValid());
        QVERIFY(writeFile(systemDir.filePath(QStringLiteral("shared.json")),
                          QStringLiteral(R"({"name": "shared", "duration": 100})")));
        QVERIFY(writeFile(userDir.filePath(QStringLiteral("shared.json")),
                          QStringLiteral(R"({"name": "shared", "duration": 500})")));

        ProfileLoader loader(reg, m_curveRegistry, QStringLiteral("test-restore"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        // User wins initially.
        auto resolved = reg.resolve(QStringLiteral("shared"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        // Delete user copy, request rescan. Spy attached BEFORE the trigger so
        // a rescan that fires early (shortened debounce, future synchronous
        // fast path) cannot slip past it, and QTRY instead of spy.wait so a
        // synchronous emission already sitting in the spy still passes — same
        // pattern as testRescanFiresSignal_whenContentChanged.
        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        QVERIFY(QFile::remove(userDir.filePath(QStringLiteral("shared.json"))));
        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);

        // System is restored via re-parse on the next rescan.
        resolved = reg.resolve(QStringLiteral("shared"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 100.0);
    }

    /// sourcesChanged as the SOLE trigger: byte-identical user and system
    /// copies, so deleting the user file shifts only the winning entry's
    /// SOURCE PATH while the payload stays equal. Deleting the
    /// sourcesChanged clause in Sink::commitBatch leaves lastBatchChanged
    /// false here and the QTRY reddens — the payload-diff siblings cannot
    /// catch that regression because their fixtures always differ in value.
    void testRescanFiresSignal_whenOnlyTheWinningSourcePathMoves()
    {
        auto& reg = m_profileRegistry;

        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid());
        QVERIFY(userDir.isValid());
        const QString body = QStringLiteral(R"({"name": "twin", "duration": 300})");
        QVERIFY(writeFile(systemDir.filePath(QStringLiteral("twin.json")), body));
        QVERIFY(writeFile(userDir.filePath(QStringLiteral("twin.json")), body));

        ProfileLoader loader(reg, m_curveRegistry, QStringLiteral("test-source-shift"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        QVERIFY(QFile::remove(userDir.filePath(QStringLiteral("twin.json"))));
        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);

        // The winning entry now reads from the system copy.
        bool found = false;
        const auto all = loader.entries();
        for (const auto& entry : all) {
            if (entry.path == QStringLiteral("twin")) {
                found = true;
                QVERIFY2(entry.sourcePath.startsWith(systemDir.path()),
                         qPrintable(QStringLiteral("expected system path, got ") + entry.sourcePath));
            }
        }
        QVERIFY(found);
    }
    /// The shared envelope helper STRIPS `name` from the root it hands back.
    ///
    /// Asserted directly on `validateJsonEnvelope`, because it cannot be seen
    /// through `ProfileLoader`: `Profile::fromJson` never reads a `name` key,
    /// so deleting the strip changes nothing a profile-level test can observe.
    /// The strip is still real contract — `name` is this layer's routing key,
    /// not schema data, and a sink that preserves unknown fields would
    /// otherwise round-trip it back out.
    void testEnvelopeValidatorStripsTheRoutingKeyFromTheRoot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("overlay.fade.json"));
        QVERIFY(writeFile(path, QStringLiteral(R"({
            "name": "overlay.fade",
            "presetName": "My Overlay Preset"
        })")));

        const auto envelope = PhosphorFsLoader::validateJsonEnvelope(path, lcEnvelopeTest());
        QVERIFY(envelope.has_value());
        QCOMPARE(envelope->name, QStringLiteral("overlay.fade"));
        QVERIFY2(!envelope->root.contains(QLatin1String("name")),
                 "the routing key survived into the schema root the sink parses");
        QCOMPARE(envelope->root.value(QLatin1String("presetName")).toString(), QStringLiteral("My Overlay Preset"));
    }
};

QTEST_MAIN(TestProfileLoader)
#include "test_profileloader.moc"
