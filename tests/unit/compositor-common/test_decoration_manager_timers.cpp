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

#include <QElapsedTimer>
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

    // Per-test fixture: a fresh fake bridge + manager before every test
    // (init() / cleanup()). Tests shrink the fallback interval and install
    // vetoes themselves where the scenario needs them.
    std::unique_ptr<FakeCompositorBridge> m_bridge;
    std::unique_ptr<DecorationManager> m_mgr;

private Q_SLOTS:

    void init()
    {
        m_bridge = std::make_unique<FakeCompositorBridge>();
        m_mgr = std::make_unique<DecorationManager>(*m_bridge);
    }

    void cleanup()
    {
        // Manager references the bridge — destroy it first.
        m_mgr.reset();
        m_bridge.reset();
    }

    void testDeferredDrainOnePerTick()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        m_bridge->addWindow(QStringLiteral("c|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        for (const QString& id : {QStringLiteral("a|1"), QStringLiteral("b|1"), QStringLiteral("c|1")}) {
            m_mgr->acquire(id, DecorationManager::autotile(Screen1));
            m_mgr->releaseKind(id, OwnerKind::Autotile, Restore::Deferred);
        }
        m_bridge->clearLog();

        m_mgr->drainPendingRestores();
        // First restore runs synchronously, the rest one per event-loop tick.
        QCOMPARE(m_bridge->callLog.size(), 1);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(m_bridge->callLog.size(), 3);
        for (const QString& call : std::as_const(m_bridge->callLog)) {
            QVERIFY(call.endsWith(QStringLiteral(",false)")));
        }
    }

    void testOverlappingDrainsNeitherDoubleRestoreNorDoubleEmit()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);

        // First drain starts a chain (restores a|1 synchronously on tick 1);
        // a second deferred release + drain mid-flight must snapshot a FRESH
        // queue — no double-restore of a|1, no extra drainFinished.
        m_mgr->drainPendingRestores();
        m_mgr->releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);
        m_mgr->drainPendingRestores();
        QTRY_COMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, false);
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(restored.count(), 2);
        QCOMPARE(m_bridge->callLog.filter(QStringLiteral("setNoBorder(a|1,false)")).size(), 1);
        QCOMPARE(m_bridge->callLog.filter(QStringLiteral("setNoBorder(b|1,false)")).size(), 1);
    }

    void testDrainSkipsReacquired()
    {
        m_bridge->addWindow(Win1);
        m_bridge->addWindow(QStringLiteral("sentinel|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        // A sentinel with a legitimate deferred restore forces the drain
        // chain to actually run (and emit drainFinished deterministically) —
        // without it a regressed implementation could pass by never
        // scheduling anything.
        m_mgr->acquire(QStringLiteral("sentinel|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("sentinel|1"), OwnerKind::Autotile, Restore::Deferred);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        // Rapid re-toggle: re-acquired before the drain runs.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));

        m_bridge->clearLog();
        m_mgr->drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        // The sentinel restored; the re-acquired window saw NO restore.
        QVERIFY(m_bridge->callLog.contains(QStringLiteral("setNoBorder(sentinel|1,false)")));
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(app|1,false)")));
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testDrainRestoreVetoRequeuesUntilVetoLifts()
    {
        m_bridge->addWindow(Win1);
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);
        bool vetoActive = true; // screen re-entered autotile
        m_mgr->setRestoreVeto([&vetoActive](const QString&) {
            return vetoActive;
        });

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        m_bridge->clearLog();

        m_mgr->drainPendingRestores();
        // An all-vetoed chain restores nothing and therefore does NOT emit
        // drainFinished (a rebuild would be pure churn). Let the chain run.
        QTest::qWait(50);
        QCOMPARE(finished.count(), 0);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(m_bridge->window(Win1)->noBorder, true); // stays hidden

        // The vetoed restore stays QUEUED — when the veto lifts (the
        // expected re-acquire never landed) the next drain restores instead
        // of stranding an ownerless hidden window forever.
        vetoActive = false;
        m_mgr->drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
        QVERIFY(!m_mgr->isOwned(Win1));
    }

    void testVetoRequeuedRestoreCancelledByReacquire()
    {
        m_bridge->addWindow(Win1);
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs);
        m_mgr->setRestoreVeto([](const QString&) {
            return true;
        });

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        m_mgr->drainPendingRestores(); // vetoed → re-queued + fallback armed
        QTest::qWait(10); // let the chain finish

        // The expected re-acquire lands: it cancels the re-queued restore.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_bridge->clearLog();

        // The armed fallback fires into an empty queue and self-cancels —
        // the window must stay hidden under the new claim.
        QTest::qWait(TestFallbackMs * 4);
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(app|1,false)")));
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testVetoOverriddenAfterRetryBound()
    {
        m_bridge->addWindow(Win1);
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs);
        int vetoCalls = 0;
        m_mgr->setRestoreVeto([&vetoCalls](const QString&) {
            ++vetoCalls;
            return true; // never lifts — the prediction is simply wrong
        });

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);

        // A veto that never lifts must not strand the window: after the
        // bounded number of FALLBACK retries the restore happens anyway —
        // the (MaxVetoRetries+1)th fallback cycle overrides.
        QTRY_COMPARE(m_bridge->window(Win1)->noBorder, false);
        QCOMPARE(vetoCalls, ExpectedMaxVetoRetries + 1);
        QVERIFY(!m_mgr->isOwned(Win1));
    }

    void testVetoRetryBudgetResetsPerEpoch()
    {
        m_bridge->addWindow(Win1);
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs);
        int vetoCalls = 0;
        m_mgr->setRestoreVeto([&vetoCalls](const QString&) {
            ++vetoCalls;
            return true;
        });

        // First epoch: burn part of the retry budget via fallback cycles.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        QTRY_VERIFY(vetoCalls >= 2);

        // Cancel the epoch (re-acquire), then let any in-flight chain/timer
        // settle against the now-empty queue so epoch 2's count is clean.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        QTest::qWait(TestFallbackMs * 3);

        // Fresh deferred epoch: the counter must restart — a budget
        // inherited from the cancelled epoch would override the veto early
        // and flicker the decoration.
        const int callsAtSecondEpoch = vetoCalls;
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        QTRY_COMPARE(m_bridge->window(Win1)->noBorder, false);
        QCOMPARE(vetoCalls - callsAtSecondEpoch, ExpectedMaxVetoRetries + 1);
    }

    void testForceShowCancelsQueuedRestore()
    {
        m_bridge->addWindow(Win1);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);

        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Snap, Restore::Deferred);

        // A force-show rule arrives while the restore is queued: the veto
        // restores synchronously AND cancels the queued entry — without the
        // cancel, the next drain would restore a second time (duplicate
        // setNoBorder(false) + duplicate signal).
        m_mgr->setRuleOverride(Win1, false);
        QCOMPARE(restored.count(), 1);
        m_mgr->drainPendingRestores();
        QTest::qWait(10);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(m_bridge->callLog.filter(QStringLiteral("setNoBorder(app|1,false)")).size(), 1);
    }

    void testDestructionMidDrainIsSafe()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        // Own manager (not the fixture's) — this test destroys it mid-drain.
        auto mgr = std::make_unique<DecorationManager>(*m_bridge);

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
        const int callsBeforeDestruction = m_bridge->callLog.size();
        mgr.reset();
        QTest::qWait(50);
        QCOMPARE(m_bridge->callLog.size(), callsBeforeDestruction);
    }

    void testRestoreAllDuringDrainDoesNotDoubleRestore()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        m_mgr->releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);

        // The drain restores one window synchronously; restoreAll() then
        // clears all tracking before the chain's second tick — that tick
        // must find its entry gone and skip, not restore twice.
        m_mgr->drainPendingRestores();
        m_mgr->restoreAll();
        QTest::qWait(50);
        const int aRestores = m_bridge->callLog.filter(QStringLiteral("setNoBorder(a|1,false)")).size();
        const int bRestores = m_bridge->callLog.filter(QStringLiteral("setNoBorder(b|1,false)")).size();
        QCOMPARE(aRestores, 1);
        QCOMPARE(bRestores, 1);
    }

    void testWindowGoneWithoutForgetIsSkippedAndPruned()
    {
        m_bridge->addWindow(Win1);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);

        // The window vanishes without a forgetWindow (defensive path —
        // production always forgets on close): the drain step must perform
        // no compositor call and the entry must not linger. A chain that
        // only swept dead windows changed zero decorations, so it must not
        // fire drainFinished either (the effect would rebuild every border
        // for nothing — the signal doc's "zero decorations changed → emits
        // nothing" contract).
        m_bridge->removeWindow(Win1);
        m_bridge->clearLog();
        m_mgr->drainPendingRestores();
        QTest::qWait(50);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(restored.count(), 0);
        QCOMPARE(finished.count(), 0);
        QVERIFY(!m_mgr->isOwned(Win1));
        QVERIFY(!m_mgr->isBorderless(Win1));
    }

    void testFallbackTimerDrains()
    {
        m_bridge->addWindow(Win1);
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred);
        // Nobody calls drainPendingRestores() — the fallback must.
        QTRY_COMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testShrinkIntervalAfterDeferReArmsActiveCountdown()
    {
        // Ordering-contract pin for the test seam: a deferred release FIRST
        // arms the countdown at the production 500 ms; shrinking the
        // interval afterwards must restart the ACTIVE timer with the new
        // value (armFallbackTimer deliberately never restarts an active
        // countdown, so the seam itself has to re-arm). Without the re-arm
        // the restore would still land — but only after the full production
        // interval; the elapsed-time bound below is what pins the contract.
        m_bridge->addWindow(Win1);
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(Win1, OwnerKind::Autotile, Restore::Deferred); // arms at 500 ms
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs); // must re-arm at 25 ms

        QElapsedTimer clock;
        clock.start();
        QTRY_COMPARE(m_bridge->window(Win1)->noBorder, false);
        QVERIFY2(clock.elapsed() < 400,
                 qPrintable(QStringLiteral("fallback fired after %1 ms — the shrunk interval was not applied "
                                           "to the already-armed countdown")
                                .arg(clock.elapsed())));
    }

    void testReleaseAllOfKindDeferredDrains()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));

        // The settings-toggle-off path: deferred release + immediate drain
        // keeps each restore on its own event-loop tick.
        m_mgr->releaseAllOfKind(OwnerKind::Autotile, Restore::Deferred);
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, true); // not yet
        m_mgr->drainPendingRestores();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, false);
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, false);
    }

    void testDrainMixedDeadAndLiveEmitsOnce()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        m_mgr->releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);

        // One window dies before the drain: the chain sweeps it with zero
        // physical work, restores the survivor, and that single real restore
        // makes the chain emit drainFinished exactly once — the middle
        // ground between the all-dead (no emit) and all-live (one emit)
        // extremes.
        m_bridge->removeWindow(QStringLiteral("a|1"));
        m_mgr->drainPendingRestores();
        QTRY_COMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, false);
        QTRY_COMPARE(finished.count(), 1);
        QTest::qWait(30); // absence check: no second emission follows
        QCOMPARE(finished.count(), 1);
    }

    void testRestoreAllFlushesQueuedDeferred()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        m_mgr->setFallbackIntervalForTesting(TestFallbackMs);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);

        // Teardown with a deferred restore still queued: everything restores
        // synchronously, the per-window signal fires for each, and no
        // fallback-timer activity remains afterwards.
        m_mgr->restoreAll();
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, false);
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, false);
        QCOMPARE(restored.count(), 2);
        const int callsAfterRestoreAll = m_bridge->callLog.size();
        QTest::qWait(TestFallbackMs * 4); // past the fallback interval
        QCOMPARE(m_bridge->callLog.size(), callsAfterRestoreAll);
    }

    void testForgetWindowMidDrainSkipsIt()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        QSignalSpy finished(m_mgr.get(), &DecorationManager::drainFinished);

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));
        m_mgr->releaseKind(QStringLiteral("a|1"), OwnerKind::Autotile, Restore::Deferred);
        m_mgr->releaseKind(QStringLiteral("b|1"), OwnerKind::Autotile, Restore::Deferred);
        m_bridge->clearLog();

        // Start the drain: the first step runs synchronously and restores
        // ONE window; the other is still held in the chain's snapshot.
        m_mgr->drainPendingRestores();
        QCOMPARE(m_bridge->callLog.size(), 1);

        // The still-queued window closes MID-DRAIN. The in-flight snapshot
        // still holds its id — the next step must skip it via the entry
        // lookup rather than restore a forgotten window.
        const QString stillQueued =
            m_bridge->callLog.first().contains(QStringLiteral("a|1")) ? QStringLiteral("b|1") : QStringLiteral("a|1");
        m_mgr->forgetWindow(stillQueued);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(m_bridge->callLog.size(), 1); // no second restore happened
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(%1,false)").arg(stillQueued)));
    }
};

QTEST_GUILESS_MAIN(TestDecorationManagerTimers)
#include "test_decoration_manager_timers.moc"
