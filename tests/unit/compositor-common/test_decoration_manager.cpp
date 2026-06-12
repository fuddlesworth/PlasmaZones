// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_manager.cpp
 * @brief Unit tests for PhosphorCompositor::DecorationManager
 *
 * The behavioral spec for unified title-bar management: ownership
 * refcounting, CSD/already-borderless handling, hide/restore ordering with
 * geometry re-assert, deferred drain races, external-reset resync, and the
 * window-rule veto. These are exactly the historical bug classes that the
 * per-mode borderless implementations kept regressing on.
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorCompositor/DecorationManager.h>

#include "fake_compositor_bridge.h"

#include <memory>

namespace {
/// Shrunk fallback interval for the timer-interplay tests (production: 500 ms).
constexpr int TestFallbackMs = 25;
} // namespace

using PhosphorCompositor::DecorationManager;
using OwnerKind = PhosphorCompositor::DecorationManager::OwnerKind;
using Placement = PhosphorCompositor::DecorationManager::Placement;
using Restore = PhosphorCompositor::DecorationManager::Restore;

namespace {
const QString Win1 = QStringLiteral("app|1");
const QString Screen1 = QStringLiteral("screen-1");
const QString Screen2 = QStringLiteral("screen-2");
const QRectF Zone(100, 100, 640, 480);
/// Mirror of the implementation's private MaxVetoRetries constant — the
/// retry-budget tests pin the contract value.
constexpr int ExpectedMaxVetoRetries = 6;
} // namespace

class TestDecorationManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testRefcounting()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});

        // Second owner: bookkeeping only, no second physical hide.
        mgr.acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(bridge.callLog.size(), 1);

        // First release: an owner remains, decoration stays hidden.
        mgr.release(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.callLog.size(), 1);

        // Last release: exactly one restore.
        mgr.release(Win1, DecorationManager::snap(Screen1));
        QVERIFY(!mgr.isBorderless(Win1));
        QVERIFY(!mgr.isOwned(Win1));
        QCOMPARE(bridge.callLog.last(), QStringLiteral("setNoBorder(app|1,false)"));
        QCOMPARE(bridge.window(Win1)->noBorder, false);
    }

    void testPerScreenGranularityAndTransfer()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.acquire(Win1, DecorationManager::autotile(Screen2));
        QCOMPARE(bridge.window(Win1)->noBorder, true);

        // Releasing one screen's claim keeps the sibling's intact.
        mgr.release(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(mgr.isBorderless(Win1));

        // Cross-screen transfer: drop everything except the target screen —
        // never a physical toggle.
        bridge.clearLog();
        mgr.releaseOthersOfKind(Win1, OwnerKind::Autotile, Screen2);
        QVERIFY(bridge.callLog.isEmpty());
        QVERIFY(mgr.isOwnedBy(Win1, DecorationManager::autotile(Screen2)));

        mgr.releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(bridge.window(Win1)->noBorder, false);
    }

    void testCsdIntentOnly()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        // userCanSetNoBorder alone gates eligibility — the manager never
        // reads hasDecoration (the CSD/SSD distinction is folded into the
        // toggleability test).
        fw->userCanSetNoBorder = false; // GTK/Electron CSD
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(mgr.isOwned(Win1));
        QVERIFY(!mgr.isBorderless(Win1));
        QVERIFY(bridge.callLog.isEmpty());

        mgr.releaseKind(Win1, OwnerKind::Autotile);
        mgr.restoreAll();
        QVERIFY(bridge.callLog.isEmpty()); // never physically touched
    }

    void testAlreadyBorderlessPreserved()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->noBorder = true; // user's own compositor rule made it borderless
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        QVERIFY(bridge.callLog.isEmpty()); // nothing to hide

        // Release restores to PRIOR state — which was borderless: no call.
        mgr.releaseKind(Win1, OwnerKind::Snap);
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(bridge.window(Win1)->noBorder, true);
    }

    void testAutotileToSnapHandoff()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        // Autotile hides; the window then arrives at snap already borderless.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        mgr.acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        mgr.releaseKind(Win1, OwnerKind::Autotile);

        // Decoration stayed hidden through the handoff: one hide, no restore.
        const int restores = bridge.callLog.filter(QStringLiteral("setNoBorder(app|1,false)")).size();
        QCOMPARE(restores, 0);
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.window(Win1)->noBorder, true);
    }

    void testAlreadyPlacedOrdering()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->moveResizeGeo = Zone; // the zone rect the compositor is moving toward
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        const QStringList expected{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(bridge.callLog, expected);
        QCOMPARE(bridge.window(Win1)->moveResizeGeo, Zone); // re-asserted target
    }

    void testAlreadyPlacedDegenerateTargetSkipsReassert()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->moveResizeGeo = QRectF(); // degenerate
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(bridge.callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

    void testCallerWillPlaceSkipsReassert()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->moveResizeGeo = Zone;
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QCOMPARE(bridge.callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

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

    void testStaleSnapshotRefreshedOnOwnerlessReacquire()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        // Hide, then force-show via rule veto, then drop the owner — the
        // veto-only entry persists with the ORIGINAL snapshot
        // (priorNoBorder=false).
        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        mgr.setRuleOverride(Win1, false);
        mgr.releaseKind(Win1, OwnerKind::Snap);
        QCOMPARE(fw->noBorder, false);

        // The user makes the window borderless THEMSELVES while we hold no
        // claim, then a mode re-acquires under the veto and the veto lifts.
        fw->noBorder = true;
        bridge.clearLog();
        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        mgr.setRuleOverride(Win1, std::nullopt);

        // The refreshed snapshot sees priorNoBorder=true: nothing to hide
        // (already borderless — no physical call at all), and the final
        // release must NOT force-decorate the user's window. A stale
        // snapshot would setNoBorder(false) here.
        mgr.releaseKind(Win1, OwnerKind::Snap);
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(fw->noBorder, true);
    }

    void testRuleHideAlsoRefreshesStaleSnapshot()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        // Same staleness as the acquire() case, reached via the RULE path:
        // ownerless veto-only entry with priorNoBorder=false captured, then
        // the user makes the window borderless themselves.
        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        mgr.setRuleOverride(Win1, false);
        mgr.releaseKind(Win1, OwnerKind::Snap);
        fw->noBorder = true;
        bridge.clearLog();

        // A rule HIDE starts a new ownership epoch through the same refresh:
        // the fresh snapshot sees priorNoBorder=true (already borderless —
        // no physical call at all), so removing the rule must not
        // force-decorate the user's window.
        mgr.setRuleOverride(Win1, true);
        mgr.setRuleOverride(Win1, std::nullopt);
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(fw->noBorder, true);
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

    void testResyncAfterExternalReset()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->moveResizeGeo = Zone;
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        QCOMPARE(fw->noBorder, true);

        // KWin silently resets noBorder on a desktop switch.
        fw->noBorder = false;
        bridge.clearLog();
        mgr.resyncWindow(Win1);
        const QStringList expected{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(bridge.callLog, expected);

        // Unowned window: no-op.
        bridge.clearLog();
        mgr.resyncWindow(QStringLiteral("ghost|9"));
        QVERIFY(bridge.callLog.isEmpty());
    }

    void testVetoWinsOverOwners()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(fw->noBorder, true);
        fw->moveResizeGeo = Zone;

        // Rule force-show: restore + geometry re-assert (window is zone-placed).
        bridge.clearLog();
        mgr.setRuleOverride(Win1, false);
        const QStringList restoreSeq{
            QStringLiteral("setNoBorder(app|1,false)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(bridge.callLog, restoreSeq);
        QVERIFY(mgr.isVetoed(Win1));
        QVERIFY(mgr.isOwned(Win1)); // mode owner persists under the veto

        // Acquiring while vetoed must not hide.
        bridge.clearLog();
        mgr.acquire(Win1, DecorationManager::snap(Screen2), Placement::AlreadyPlaced);
        QVERIFY(bridge.callLog.isEmpty());

        // Veto lifted: owners re-assert with geometry re-assert.
        mgr.setRuleOverride(Win1, std::nullopt);
        const QStringList hideSeq{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(bridge.callLog, hideSeq);
        QVERIFY(!mgr.isVetoed(Win1));
    }

    void testRuleOwnerHidesAndReleases()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        // Rule hide on an otherwise unmanaged (floating) window.
        mgr.setRuleOverride(Win1, true);
        QVERIFY(mgr.isBorderless(Win1));

        // Mode owner joins; rule goes away — mode keeps it hidden.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        mgr.setRuleOverride(Win1, std::nullopt);
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.window(Win1)->noBorder, true);

        // Mode releases — now it restores.
        mgr.releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(bridge.window(Win1)->noBorder, false);
    }

    void testClearAllRuleOverrides()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);

        mgr.setRuleOverride(QStringLiteral("a|1"), true); // rule-only hide
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));
        mgr.setRuleOverride(QStringLiteral("b|1"), true); // rule + mode

        mgr.clearAllRuleOverrides();
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, false); // restored
        QCOMPARE(bridge.window(QStringLiteral("b|1"))->noBorder, true); // mode still owns
    }

    void testRestoreAllAndForget()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        auto* csd = bridge.addWindow(QStringLiteral("csd|1"));
        csd->userCanSetNoBorder = false;
        DecorationManager mgr(bridge);

        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));
        mgr.acquire(QStringLiteral("csd|1"), DecorationManager::autotile(Screen1));

        // forgetWindow: zero compositor calls, state dropped.
        bridge.clearLog();
        mgr.forgetWindow(QStringLiteral("a|1"));
        QVERIFY(bridge.callLog.isEmpty());
        QVERIFY(!mgr.isOwned(QStringLiteral("a|1")));

        mgr.restoreAll();
        QCOMPARE(bridge.window(QStringLiteral("b|1"))->noBorder, false);
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, true); // forgotten — untouched
        QVERIFY(!mgr.isOwned(QStringLiteral("b|1")));
        // CSD window: still zero physical calls.
        QVERIFY(!bridge.callLog.contains(QStringLiteral("setNoBorder(csd|1,false)")));
    }

    void testReleaseAllOfKindImmediate()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(QStringLiteral("a|1"));
        bridge.addWindow(QStringLiteral("b|1"));
        DecorationManager mgr(bridge);

        // a is autotile-owned on two screens; b is snap-owned.
        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr.acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen2));
        mgr.acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));

        // The per-mode hide-title-bars-OFF path: all owners of ONE kind drop
        // (across every screen); the other kind is untouched.
        mgr.releaseAllOfKind(OwnerKind::Autotile);
        QCOMPARE(bridge.window(QStringLiteral("a|1"))->noBorder, false);
        QVERIFY(!mgr.isOwned(QStringLiteral("a|1")));
        QCOMPARE(bridge.window(QStringLiteral("b|1"))->noBorder, true);
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

    void testRuleOverrideClearOnVetoOnlyEntryPrunes()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        // Veto-only entry: a force-show rule on an unowned window.
        mgr.setRuleOverride(Win1, false);
        QVERIFY(mgr.isVetoed(Win1));
        QVERIFY(!mgr.isOwned(Win1));
        QVERIFY(bridge.callLog.isEmpty()); // nothing was hidden — nothing to do

        // Rule removed: the veto-only entry must clear (and prune) with no
        // physical toggle.
        mgr.setRuleOverride(Win1, std::nullopt);
        QVERIFY(!mgr.isVetoed(Win1));
        QVERIFY(bridge.callLog.isEmpty());
        QCOMPARE(fw->noBorder, false);
    }

    void testResyncNoopWhileVetoedOrPending()
    {
        FakeCompositorBridge bridge;
        auto* fw = bridge.addWindow(Win1);
        fw->moveResizeGeo = Zone;
        DecorationManager mgr(bridge);

        // Vetoed: the decoration is deliberately visible — resync must not
        // re-hide it.
        mgr.acquire(Win1, DecorationManager::snap(Screen1));
        mgr.setRuleOverride(Win1, false);
        bridge.clearLog();
        mgr.resyncWindow(Win1);
        QVERIFY(bridge.callLog.isEmpty());
        mgr.setRuleOverride(Win1, std::nullopt); // re-hides via owner re-assert

        // Deferred-release state: the hide is on its way OUT — resync must
        // not fight the queued restore. (The no-op follows from the emptied
        // owner set; the pendingRestore term in resync's guard is defensive
        // redundancy, since pendingRestore is only ever set once the owner
        // set emptied.)
        mgr.releaseKind(Win1, OwnerKind::Snap, Restore::Deferred);
        bridge.clearLog();
        fw->noBorder = false; // simulate an external reset mid-defer
        mgr.resyncWindow(Win1);
        QVERIFY(bridge.callLog.isEmpty());
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

    void testAcquireIntentOnlyThenResolvedHides()
    {
        FakeCompositorBridge bridge;
        DecorationManager mgr(bridge);

        // Window not resolvable at first acquire: intent recorded, no calls.
        mgr.acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(mgr.isOwned(Win1));
        QVERIFY(!mgr.isBorderless(Win1));
        QVERIFY(bridge.callLog.isEmpty());

        // Window appears and a later acquire retries: the hide lands.
        bridge.addWindow(Win1);
        mgr.acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(mgr.isBorderless(Win1));
        QCOMPARE(bridge.window(Win1)->noBorder, true);
    }

    void testWindowDecorationRestoredSignalPayload()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);
        QSignalSpy restored(&mgr, &DecorationManager::windowDecorationRestored);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        QCOMPARE(restored.count(), 0); // hides never emit it
        mgr.releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(restored.first().first().toString(), Win1);
    }

    void testReleaseNonOwnerIsNoop()
    {
        FakeCompositorBridge bridge;
        bridge.addWindow(Win1);
        DecorationManager mgr(bridge);

        mgr.acquire(Win1, DecorationManager::autotile(Screen1));
        bridge.clearLog();
        // Untile-diff calls release for windows that may not be owned here.
        mgr.release(Win1, DecorationManager::snap(Screen1));
        mgr.release(Win1, DecorationManager::autotile(Screen2));
        mgr.releaseKind(QStringLiteral("ghost|9"), OwnerKind::Autotile);
        QVERIFY(bridge.callLog.isEmpty());
        QVERIFY(mgr.isBorderless(Win1));
    }
};

QTEST_GUILESS_MAIN(TestDecorationManager)
#include "test_decoration_manager.moc"
