// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_windowrulestore.cpp
 * @brief Library-level unit tests for WindowRuleStore.
 *
 * The daemon-side test (tests/unit/config/test_windowrule_store.cpp) covers
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

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include <PhosphorWindowRule/WindowRuleStore.h>

using namespace PhosphorWindowRule;

class TestWindowRuleStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString storePath() const
    {
        return m_dir.path() + QStringLiteral("/windowrules.json");
    }

    static WindowRule makeRule(const QString& screenId)
    {
        return ContextRuleBridge::makeAssignmentRule(screenId, screenId, 0, QString(), /*autotile=*/false,
                                                     QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
                                                     QString());
    }

    void writeRaw(const QString& json) const
    {
        QFile f(storePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json.toUtf8());
    }

private Q_SLOTS:

    void init()
    {
        QFile::remove(storePath());
    }

    // ─── Load robustness ──────────────────────────────────────────────────

    void testMissingFile_loadsEmpty()
    {
        WindowRuleStore store(storePath());
        QCOMPARE(store.count(), 0);
        QVERIFY(store.ruleSet().isEmpty());
    }

    void testMalformedFile_loadsEmpty()
    {
        writeRaw(QStringLiteral("{ not valid json"));
        WindowRuleStore store(storePath());
        QCOMPARE(store.count(), 0);
    }

    void testWrongVersion_loadsEmpty()
    {
        writeRaw(QStringLiteral("{\"_version\":1,\"rules\":[]}"));
        WindowRuleStore store(storePath());
        QCOMPARE(store.count(), 0);
    }

    // ─── Atomic save round-trip ───────────────────────────────────────────

    void testAtomicSave_roundTrips()
    {
        const WindowRule a = makeRule(QStringLiteral("DP-1"));
        {
            WindowRuleStore store(storePath());
            QVERIFY(store.addRule(a));
        }
        // A fresh store reading the same file sees the rule intact.
        WindowRuleStore reloaded(storePath());
        QCOMPARE(reloaded.count(), 1);
        QVERIFY(reloaded.contains(a.id));

        // Atomicity check: a successful QSaveFile commit leaves no temporary
        // scratch file behind in the directory — only the final store file.
        const QStringList entries = QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
        QCOMPARE(entries, QStringList{QStringLiteral("windowrules.json")});
    }

    // ─── Revision monotonicity ────────────────────────────────────────────

    void testRevision_monotonicAcrossMutations()
    {
        WindowRuleStore store(storePath());
        const quint64 r0 = store.ruleSet().revision();

        const WindowRule a = makeRule(QStringLiteral("DP-1"));
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
        WindowRuleStore store(storePath());
        WindowRule a = makeRule(QStringLiteral("DP-1"));
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
        WindowRuleStore store(storePath());
        const WindowRule a = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(a));

        QSignalSpy spy(&store, &WindowRuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        // Replacing the set with an identical list is a no-op — no emit.
        QVERIFY(store.setAllRules(QList<WindowRule>{a}));
        QCOMPARE(spy.count(), 0);

        // A genuinely different list does emit.
        QVERIFY(store.setAllRules(QList<WindowRule>{makeRule(QStringLiteral("HDMI-1"))}));
        QCOMPARE(spy.count(), 1);
    }

    void testSetAllRules_noOpLeavesFileUntouched()
    {
        WindowRuleStore store(storePath());
        const WindowRule a = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(a));

        const QFileInfo before(storePath());
        const QDateTime beforeMtime = before.lastModified();

        // A no-op setAllRules must not rewrite the file.
        QVERIFY(store.setAllRules(QList<WindowRule>{a}));
        QFileInfo after(storePath());
        QCOMPARE(after.lastModified(), beforeMtime);
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
        WindowRuleStore store(storePath());
        // Make the store directory read-only so the QSaveFile temp-write +
        // rename inside save() fails. The in-memory mutation still succeeds,
        // so addRule must return false to signal the file is now stale.
        QVERIFY(QFile::setPermissions(m_dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        const WindowRule a = makeRule(QStringLiteral("DP-1"));
        const bool ok = store.addRule(a);

        // Restore permissions before any assertion can abort the test.
        QVERIFY(QFile::setPermissions(m_dir.path(),
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
        WindowRuleStore store(storePath());
        QVERIFY(QFile::setPermissions(m_dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        const bool ok = store.setAllRules(QList<WindowRule>{makeRule(QStringLiteral("DP-1"))});

        QVERIFY(QFile::setPermissions(m_dir.path(),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        QVERIFY2(!ok, "setAllRules must return false when the persist fails");
        QCOMPARE(store.count(), 1);
    }
};

QTEST_MAIN(TestWindowRuleStore)
#include "test_windowrulestore.moc"
