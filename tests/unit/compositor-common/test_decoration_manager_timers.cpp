// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_manager_timers.cpp
 * @brief DecorationManager deferred-restore drain / fallback-timer spec
 *
 * The timer-interplay half of the DecorationManager behavioral spec, split
 * from test_decoration_manager.cpp (<800-line guideline): one-per-tick drain
 * chains, re-acquire cancellation, the bounded veto and its per-epoch retry
 * budget, fallback-timer lifecycle, destruction/restoreAll racing live
 * chains, and forget/window-gone mid-drain handling. Synchronous ownership,
 * placement-ordering, and rule-veto semantics live in the sibling suite.
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorCompositor/DecorationManager.h>

#include "fake_compositor_bridge.h"

#include <memory>

namespace {
/// Shrunk fallback interval for the timer-interplay tests (production: 500 ms).
constexpr int TestFallbackMs = 25;
const QString Win1 = QStringLiteral("app|1");
const QString Screen1 = QStringLiteral("screen-1");
/// Mirror of the implementation's private MaxVetoRetries constant — the
/// retry-budget tests pin the contract value.
constexpr int ExpectedMaxVetoRetries = 6;
} // namespace

using PhosphorCompositor::DecorationManager;
using OwnerKind = PhosphorCompositor::DecorationManager::OwnerKind;
using Placement = PhosphorCompositor::DecorationManager::Placement;
using Restore = PhosphorCompositor::DecorationManager::Restore;

class TestDecorationManagerTimers : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDeferredDrainOnePerTick()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        bridge.addWindow(QStringLiteral("c|1"));
        DecorationManager mgr(bridge);
        QSignalSpy finished(&mgr, &DecorationManager::drainFinished);

        for (const QString& id : {QStringLiteral("a|1"), QStringLiteral("b|1"), QStringLiteral("c|1")}) {
            mgr.acquire(id, DecorationManager::autotile(Screen1));
            mgr.releaseKind(id, OwnerKind::Autotile, Restore::Deferred);
        }
        bridge.clearLog();

        mgr.drainPendingRestores();
        // First restore runs synchronously, the rest one per event-loop tick.
        QCOMPARE(bridge.callLog.size(), 1);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(bridge.callLog.size(), 3);
        for (const QString& call : std::as_const(bridge.callLog)) {
            QVERIFY(call.endsWith(QStringLiteral(",false)")));
        }
    }

    void testDrainSkipsReacquired()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        bridge.addWindow(QStringLiteral("sentinel|1"));
        DecorationManager mgr(bridge);
        QSignalSpy finished(&mgr, &DecorationManager::drainFinished);

        // A sentinel with a legitimate deferred restore forces the drain
        // chain to actually run (and emit drainFinished deterministically) —
        // without it a regressed implementation could pass by never
        // scheduling anything.
        mgr.acquire(QStringLiteral("sentinel|1"), DecorationManager::autotile(Screen1));
        mgr.releaseKind(QStringLiteral("sentinel|1"), OwnerKind::Autotile, Restore::Deferred);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        // Rapid re-toggle: re-acquired before the drain runs.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1));

        bridge.clearLog();
        mgr.drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        // The sentinel restored; the re-acquired window saw NO restore.
        QVERIFY(bridge.callLog.contains(QStringLiteral("setNoBorder(sentinel|1,false)")));
        QVERIFY(!bridge.callLog.contains(QStringLiteral("setNoBorder(app|1,false)")));
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.window(Win1)->noBorder, true);
    }

    void testDrainRestoreVetoRequeuesUntilVetoLifts()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        QSignalSpy finished(&mgr, &DecorationManager::drainFinished);
        bool vetoActive = true; // screen re-entered autotile
        mgr.setRestoreVeto([&vetoActive](const QString&) {
            return vetoActive;
        });

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        bridge.clearLog();

        mgr.drainPendingRestores();
        // An all-vetoed chain restores nothing and therefore does NOT emit
        // drainFinished (a rebuild would be pure churn). Let the chain run.
        QTest::qWait(50);
        QCOMPARE(finished.count(), 0);
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(bridge.window(Win1)->noBorder, true); // stays hidden

        // The vetoed restore stays QUEUED — when the veto lifts (the
        // expected re-acquire never landed) the next drain restores instead
        // of stranding an ownerless hidden window forever.
        vetoActive = false;
        mgr.drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(bridge.window(Win1)->noBorder, false);
        QVERIFY(!mgr.isOwned(Win1));
    }

    void testVetoRequeuedRestoreCancelledByReacquire()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        mgr.setFallbackIntervalForTesting(TestFallbackMs);
        mgr.setRestoreVeto([](const QString&) {
            return true;
        });

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        mgr.drainPendingRestores(); // vetoed → re-queued + fallback armed
        QTest::qWait(10); // let the chain finish

        // The expected re-acquire lands: it cancels the re-queued restore.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        bridge.clearLog();

        // The armed fallback fires into an empty queue and self-cancels —
        // the window must stay hidden under the new claim.
        QTest::qWait(TestFallbackMs * 4);
        QVERIFY(!bridge.callLog.contains(QStringLiteral("setNoBorder(app|1,false)")));
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.window(Win1)->noBorder, true);
    }

    void testVetoOverriddenAfterRetryBound()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        mgr.setFallbackIntervalForTesting(TestFallbackMs);
        int vetoCalls = 0;
        mgr.setRestoreVeto([&vetoCalls](const QString&) {
            ++vetoCalls;
            return true; // never lifts — the prediction is simply wrong
        });

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);

        // A veto that never lifts must not strand the window: after the
        // bounded number of FALLBACK retries the restore happens anyway —
        // the (MaxVetoRetries+1)th fallback cycle overrides.
        QTRY_COMPARE(bridge.window(Win1)->noBorder, false);
        QCOMPARE(vetoCalls, ExpectedMaxVetoRetries + 1);
        QVERIFY(!mgr.isOwned(Win1));
    }

    void testVetoRetryBudgetResetsPerEpoch()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        mgr.setFallbackIntervalForTesting(TestFallbackMs);
        int vetoCalls = 0;
        mgr.setRestoreVeto([&vetoCalls](const QString&) {
            ++vetoCalls;
            return true;
        });

        // First epoch: burn part of the retry budget via fallback cycles.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        QTRY_VERIFY(vetoCalls >= 2);

        // Cancel the epoch (re-acquire), then let any in-flight chain/timer
        // settle against the now-empty queue so epoch 2's count is clean.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        QTest::qWait(TestFallbackMs * 3);

        // Fresh deferred epoch: the counter must restart — a budget
        // inherited from the cancelled epoch would override the veto early
        // and flicker the decoration.
        const int callsAtSecondEpoch = vetoCalls;
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        QTRY_COMPARE(bridge.window(Win1)->noBorder, false);
        QCOMPARE(vetoCalls - callsAtSecondEpoch, ExpectedMaxVetoRetries + 1);
    }

    void testForceShowCancelsQueuedRestore()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        QSignalSpy restored(&mgr, &DecorationManager::windowDecorationRestored);

        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Snap, Restore::Deferred);

        // A force-show rule arrives while the restore is queued: the veto
        // restores synchronously AND cancels the queued entry — without the
        // cancel, the next drain would restore a second time (duplicate
        // setNoBorder(false) + duplicate signal).
        mgr.setRuleOverride(Win1, false);
        QCOMPARE(restored.count(), 1);
        mgr.drainPendingRestores();
        QTest::qWait(10);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(bridge.callLog.filter(QStringLiteral("setNoBorder(app|1,false)")).size(), 1);
    }

    void testDestructionMidDrainIsSafe()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        auto mgr = std::make_unique<DecorationManager>(bridge);

        mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        mgr->releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);

        // First step runs synchronously; destroy the manager with the chain
        // continuation still queued. The queued QTimer copy dies with the
        // QObject and the destructor breaks the chain's self-reference cycle
        // (leak half is ASAN/LSAN-visible) — observable here: no stray
        // compositor call after destruction.
        mgr->drainPendingRestores();
        const int callsBeforeDestruction = bridge.callLog.size();
        mgr.reset();
        QTest::qWait(50);
        QCOMPARE(bridge.callLog.size(), callsBeforeDestruction);
    }

    void testRestoreAllDuringDrainDoesNotDoubleRestore()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);

        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        mgr.releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        mgr.releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);

        // The drain restores one window synchronously; restoreAll() then
        // clears all tracking before the chain's second tick — that tick
        // must find its entry gone and skip, not restore twice.
        mgr.drainPendingRestores();
        mgr.restoreAll();
        QTest::qWait(50);
        const int aRestores = bridge.callLog.filter(QStringLiteral("setNoBorder(a|1,false)")).size();
        const int bRestores = bridge.callLog.filter(QStringLiteral("setNoBorder(b|1,false)")).size();
        QCOMPARE(aRestores, 1);
        QCOMPARE(bRestores, 1);
    }

    void testWindowGoneWithoutForgetIsSkippedAndPruned()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        QSignalSpy restored(&mgr, &DecorationManager::windowDecorationRestored);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);

        // The window vanishes without a forgetWindow (defensive path —
        // production always forgets on close): the drain step must perform
        // no compositor call and the entry must not linger.
        bridge.removeWindow(Win1);
        bridge.clearLog();
        mgr.drainPendingRestores();
        QTest::qWait(50);
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(restored.count(), 0);
        QVERIFY(!mgr.isOwned(Win1));
        QVERIFY(!mgr.isBorderless(Win1));
    }

    void testFallbackTimerDrains()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        mgr.setFallbackIntervalForTesting(TestFallbackMs);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        // Nobody calls drainPendingRestores() — the fallback must.
        QTRY_COMPARE(bridge.window(Win1)->noBorder, false);
    }

    void testReleaseAllOfKindDeferredDrains()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);
        QSignalSpy finished(&mgr, &DecorationManager::drainFinished);

        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));

        // The settings-toggle-off path: deferred release + immediate drain
        // keeps each restore on its own event-loop tick.
        mgr.releaseAllOfKind(OwnerKind::Autotile, Restore::Deferred);
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, true); // not yet
        mgr.drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, false);
        QCOMPARE(bridge.window(QStringLiteral("b|1"))->noBorder, false);
    }

    void testRestoreAllFlushesQueuedDeferred()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);
        mgr.setFallbackIntervalForTesting(TestFallbackMs);
        QSignalSpy restored(&mgr, &DecorationManager::windowDecorationRestored);

        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        mgr.releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);

        // Teardown with a deferred restore still queued: everything restores
        // synchronously, the per-window signal fires for each, and no
        // fallback-timer activity remains afterwards.
        mgr.restoreAll();
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, false);
        QCOMPARE(bridge.window(QStringLiteral("b|1"))->noBorder, false);
        QCOMPARE(restored.count(), 2);
        const int callsAfterRestoreAll = bridge.callLog.size();
        QTest::qWait(TestFallbackMs * 4); // past the fallback interval
        QCOMPARE(bridge.callLog.size(), callsAfterRestoreAll);
    }

    void testForgetWindowMidDrainSkipsIt()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);
        QSignalSpy finished(&mgr, &DecorationManager::drainFinished);

        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        mgr.releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        mgr.releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);
        bridge.clearLog();

        // Start the drain: the first step runs synchronously and restores
        // ONE window; the other is still held in the chain's snapshot.
        mgr.drainPendingRestores();
        QCOMPARE(bridge.callLog.size(), 1);

        // The still-queued window closes MID-DRAIN. The in-flight snapshot
        // still holds its id — the next step must skip it via the entry
        // lookup rather than restore a forgotten window.
        const QString stillQueued =
            bridge.callLog.first().contains(QStringLiteral("a|1")) ? QStringLiteral("b|1") : QStringLiteral("a|1");
        mgr.forgetWindow(stillQueued);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(bridge.callLog.size(), 1); // no second restore happened
        QVERIFY(!bridge.callLog.contains(QStringLiteral("setNoBorder(%1,false)").arg(stillQueued)));
    }
};

QTEST_GUILESS_MAIN(TestDecorationManagerTimers)
#include "test_decoration_manager_timers.moc"
