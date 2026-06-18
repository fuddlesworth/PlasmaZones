// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_windowrulestorewatcher.cpp
 * @brief Unit tests for WindowRuleStoreWatcher.
 *
 * Pins the cross-process auto-reload contract: an external write to
 * windowrules.json reloads a watched store, and a no-op rewrite (identical
 * content) does NOT churn — the property that makes the watcher safe against
 * the store's own save() emissions.
 *
 * File-watch timing is inherently asynchronous: tests drive the event loop via
 * QSignalSpy::wait() / QTest::qWait() with generous budgets (the watcher's
 * 50 ms debounce plus filesystem-notification latency).
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <PhosphorWindowRules/ContextRuleBridge.h>
#include <PhosphorWindowRules/WindowRule.h>
#include <PhosphorWindowRules/WindowRuleStore.h>
#include <PhosphorWindowRules/WindowRuleStoreWatcher.h>

using namespace PhosphorWindowRules;

class TestWindowRuleStoreWatcher : public QObject
{
    Q_OBJECT

private:
    static QString storePath(const QTemporaryDir& dir)
    {
        return dir.path() + QStringLiteral("/windowrules.json");
    }

    static WindowRule makeRule(const QString& screenId)
    {
        return ContextRuleBridge::makeAssignmentRule(screenId, screenId, 0, QString(), QStringLiteral("snapping"),
                                                     QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
                                                     QString());
    }

private Q_SLOTS:

    /// A separate process's write to windowrules.json triggers a reload of a
    /// watched store: rulesChanged fires and the new rule becomes visible
    /// without any manual load() on the watched store.
    void testExternalWrite_reloadsStore()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        WindowRuleStore watched(storePath(dir));
        QCOMPARE(watched.count(), 0);

        WindowRuleStoreWatcher watcher(watched);
        watcher.start();

        QSignalSpy spy(&watched, &WindowRuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        // A separate store (stand-in for another process) creates the file.
        WindowRuleStore writer(storePath(dir));
        QVERIFY(writer.addRule(makeRule(QStringLiteral("DP-1"))));

        // The watcher's debounced rescan reloads `watched` from disk.
        QVERIFY(spy.wait(5000));
        QCOMPARE(watched.count(), 1);
    }

    /// A no-op external rewrite (identical content) must NOT emit rulesChanged.
    /// The store's idempotent load() guards against churn — the same property
    /// that makes a watcher event from the store's OWN save() harmless.
    ///
    /// The test first drives a REAL change and waits for the reload, proving the
    /// watch pipeline is live; only then does it assert that an identical
    /// rewrite emits nothing. Without that liveness step, the negative
    /// assertion could pass for the wrong reason (a rescan that never fired
    /// looks identical to one that fired and correctly suppressed the emit).
    void testIdenticalRewrite_noEmit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Seed one rule and persist it so every store below reads the same
        // on-disk rule (identical id + content).
        {
            WindowRuleStore seed(storePath(dir));
            QVERIFY(seed.addRule(makeRule(QStringLiteral("DP-1"))));
        }
        WindowRuleStore watched(storePath(dir));
        QCOMPARE(watched.count(), 1);

        WindowRuleStoreWatcher watcher(watched);
        watcher.start();

        QSignalSpy spy(&watched, &WindowRuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        WindowRuleStore writer(storePath(dir));
        QCOMPARE(writer.count(), 1);

        // Liveness check: a genuine change must reach the watched store. This
        // proves the rescan pipeline works before the negative assertion below.
        QVERIFY(writer.addRule(makeRule(QStringLiteral("DP-2"))));
        QVERIFY(spy.wait(5000));
        QCOMPARE(watched.count(), 2);
        const int emitsAfterRealChange = spy.count();

        // Now re-persist IDENTICAL content. The watcher still rescans, but the
        // store's idempotent load() must reload to identical content and emit
        // nothing further.
        QVERIFY(writer.save());
        QTest::qWait(1500);
        QCOMPARE(spy.count(), emitsAfterRealChange);
        QCOMPARE(watched.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TestWindowRuleStoreWatcher)
#include "test_windowrulestorewatcher.moc"
