// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_rulestore.cpp
 * @brief Library-level unit tests for RuleStore.
 *
 * The daemon-side test (tests/unit/config/test_rule_store.cpp) covers
 * round-trip persistence and the basic mutation API. This library-level test
 * pins the contract additions that test does not exercise:
 *   - missing / malformed file load → empty set,
 *   - atomic-save round-trip,
 *   - rule-set revision monotonicity through the store's mutators,
 *   - no-op mutators do not emit rulesChanged,
 *   - save-failure propagation — a mutator returns false when the persist
 *     fails (parent path made un-writable).
 */

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <unistd.h>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

using namespace PhosphorRules;

class TestRuleStore : public QObject
{
    Q_OBJECT

private:
    static QString storePathIn(const QTemporaryDir& dir)
    {
        return dir.path() + QStringLiteral("/rules.json");
    }

    static Rule makeRule(const QString& screenId)
    {
        // screenId is passed as BOTH the rule name (1st arg) and the screen-id
        // dimension (2nd arg) so each fixture rule is self-describing: a distinct
        // screenId yields a distinct context tuple AND a distinct deterministic
        // id. The id derives from the context tuple, not the name.
        return ContextRuleBridge::makeAssignmentRule(screenId, screenId, 0, QString(), QStringLiteral("snapping"),
                                                     QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
                                                     QString());
    }

    static void writeRaw(const QTemporaryDir& dir, const QString& json)
    {
        QFile f(storePathIn(dir));
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray payload = json.toUtf8();
        QCOMPARE(f.write(payload), static_cast<qint64>(payload.size()));
        f.close();
    }

    /// True if a sentinel file can be created in @p dirPath — i.e. the
    /// directory is still writable. Some CI filesystems ignore POSIX mode
    /// bits, so a `setPermissions` read-only request can silently no-op;
    /// the save-failure tests probe with this to skip rather than mislead.
    static bool directoryIsWritable(const QString& dirPath)
    {
        const QString sentinel = dirPath + QStringLiteral("/.writability-probe");
        QFile probe(sentinel);
        if (!probe.open(QIODevice::WriteOnly)) {
            return false;
        }
        probe.close();
        QFile::remove(sentinel);
        return true;
    }

private Q_SLOTS:

    // Each test owns a local QTemporaryDir — the store's own file plus any
    // QSaveFile scratch artifact a save-failure test leaks are all confined
    // to that dir and discarded when it goes out of scope, so the
    // directory-content assertion in testAtomicSave_roundTrips is not
    // order-dependent on prior slots.

    // ─── Load robustness ──────────────────────────────────────────────────

    void testMissingFile_loadsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        QCOMPARE(store.count(), 0);
        QVERIFY(store.ruleSet().isEmpty());
    }

    void testMalformedFile_loadsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeRaw(dir, QStringLiteral("{ not valid json"));
        RuleStore store(storePathIn(dir));
        QCOMPARE(store.count(), 0);
    }

    void testWrongVersion_loadsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeRaw(dir, QStringLiteral("{\"_version\":1,\"rules\":[]}"));
        RuleStore store(storePathIn(dir));
        QCOMPARE(store.count(), 0);
    }

    // ─── Atomic save round-trip ───────────────────────────────────────────

    void testAtomicSave_roundTrips()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Rule a = makeRule(QStringLiteral("DP-1"));
        {
            RuleStore store(storePathIn(dir));
            QVERIFY(store.addRule(a));
        }
        // A fresh store reading the same file sees the rule intact.
        RuleStore reloaded(storePathIn(dir));
        QCOMPARE(reloaded.count(), 1);
        QVERIFY(reloaded.contains(a.id));

        // Atomicity check: a successful QSaveFile commit leaves no temporary
        // scratch file behind in the directory — only the final store file.
        const QStringList entries = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
        QCOMPARE(entries, QStringList{QStringLiteral("rules.json")});
    }

    // ─── Revision monotonicity ────────────────────────────────────────────

    void testRevision_monotonicAcrossMutations()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        const quint64 r0 = store.ruleSet().revision();

        const Rule a = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(a));
        const quint64 r1 = store.ruleSet().revision();
        QVERIFY(r1 > r0);

        QVERIFY(store.setRulePriority(a.id, 555));
        const quint64 r2 = store.ruleSet().revision();
        QVERIFY(r2 > r1);

        QVERIFY(store.removeRule(a.id));
        QVERIFY(store.ruleSet().revision() > r2);
    }

    void testRevision_noOpMutatorDoesNotBump()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        Rule a = makeRule(QStringLiteral("DP-1"));
        a.enabled = true;
        QVERIFY(store.addRule(a));
        const quint64 rev = store.ruleSet().revision();

        // A no-op enabled change must not bump the revision.
        QVERIFY(store.setRuleEnabled(a.id, true));
        QCOMPARE(store.ruleSet().revision(), rev);

        // A no-op priority change likewise.
        QVERIFY(store.setRulePriority(a.id, a.priority));
        QCOMPARE(store.ruleSet().revision(), rev);
    }

    // ─── No-op mutators do not emit ───────────────────────────────────────

    void testSetAllRules_noOpDoesNotEmit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        const Rule a = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(a));

        QSignalSpy spy(&store, &RuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        // Replacing the set with an identical list is a no-op — no emit.
        QVERIFY(store.setAllRules(QList<Rule>{a}));
        QCOMPARE(spy.count(), 0);

        // A genuinely different list does emit.
        QVERIFY(store.setAllRules(QList<Rule>{makeRule(QStringLiteral("HDMI-1"))}));
        QCOMPARE(spy.count(), 1);
        // The emit carries `persisted=true` on a successful save. Pinning the
        // bool here protects the Pass-1 signal-signature change — consumers
        // (the effect's debounce) key off it to distinguish a confirmed save
        // from an in-memory-only mutation.
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    void testSetAllRules_noOpLeavesFileUntouched()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        const Rule a = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(a));

        // Snapshot the persisted bytes. mtime equality is unreliable at
        // coarse filesystem timestamp resolution, so assert the file content
        // is byte-identical instead — a no-op setAllRules must not rewrite it.
        auto readFile = [&dir]() -> QByteArray {
            QFile f(storePathIn(dir));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QByteArray before = readFile();
        QVERIFY(!before.isEmpty());

        // A no-op setAllRules must not rewrite the file.
        QVERIFY(store.setAllRules(QList<Rule>{a}));
        QCOMPARE(readFile(), before);
    }

    // ─── Save-failure propagation ─────────────────────────────────────────

    void testAddRule_propagatesSaveFailure()
    {
        // root bypasses directory write permissions, so the read-only-dir
        // trick cannot provoke a save failure when the suite runs as root
        // (the project's Docker CI does). Skip rather than report a spurious
        // failure.
        if (::geteuid() == 0) {
            QSKIP("save-failure test requires a non-root uid");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        // Make the store directory read-only so the QSaveFile temp-write +
        // rename inside save() fails. The in-memory mutation still succeeds,
        // so addRule must return false to signal the file is now stale.
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        // Some CI filesystems ignore POSIX mode bits — verify the directory
        // actually became read-only, else the test would silently mislead.
        if (directoryIsWritable(dir.path())) {
            QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QSKIP("filesystem ignores read-only directory permissions");
        }

        const Rule a = makeRule(QStringLiteral("DP-1"));
        const bool ok = store.addRule(a);

        // Restore permissions before any assertion can abort the test — also
        // required so the QTemporaryDir can clean itself up on destruction.
        QVERIFY(QFile::setPermissions(dir.path(),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        QVERIFY2(!ok, "addRule must return false when the persist fails");
        // The in-memory set still reflects the requested change.
        QCOMPARE(store.count(), 1);
        QVERIFY(store.contains(a.id));
    }

    void testSetAllRules_propagatesSaveFailure()
    {
        // root bypasses directory write permissions — see
        // testAddRule_propagatesSaveFailure.
        if (::geteuid() == 0) {
            QSKIP("save-failure test requires a non-root uid");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RuleStore store(storePathIn(dir));
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        // Some CI filesystems ignore POSIX mode bits — see
        // testAddRule_propagatesSaveFailure.
        if (directoryIsWritable(dir.path())) {
            QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QSKIP("filesystem ignores read-only directory permissions");
        }

        QSignalSpy spy(&store, &RuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        const bool ok = store.setAllRules(QList<Rule>{makeRule(QStringLiteral("DP-1"))});

        // Restore permissions — also required for QTemporaryDir cleanup.
        QVERIFY(QFile::setPermissions(dir.path(),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        QVERIFY2(!ok, "setAllRules must return false when the persist fails");
        QCOMPARE(store.count(), 1);
        // The save-failure emission carries `persisted=false`. This is the
        // signal-channel contract for the Pass-1 refactor: the in-memory set
        // changed (so the signal fires) but the on-disk state did NOT, so
        // consumers must not trust the broadcast as a "persisted state moved"
        // notification. Pinning the false here documents and protects the
        // partial-failure semantic.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TestRuleStore)
#include "test_rulestore.moc"
