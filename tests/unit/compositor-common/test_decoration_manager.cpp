// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_manager.cpp
 * @brief Unit tests for PhosphorCompositor::DecorationManager
 *
 * The synchronous half of the behavioral spec for unified title-bar
 * management: ownership refcounting, CSD/already-borderless handling,
 * hide/restore ordering with geometry re-assert, external-reset resync, and
 * the window-rule veto. The deferred-restore drain / fallback-timer family
 * lives in test_decoration_manager_timers.cpp (<800-line guideline split).
 * These are exactly the historical bug classes that the per-mode borderless
 * implementations kept regressing on.
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorCompositor/DecorationManager.h>

#include "fake_compositor_bridge.h"

using PhosphorCompositor::DecorationManager;
using OwnerKind = PhosphorCompositor::DecorationManager::OwnerKind;
using Placement = PhosphorCompositor::DecorationManager::Placement;
using Restore = PhosphorCompositor::DecorationManager::Restore;

namespace {
const QString Win1 = QStringLiteral("app|1");
const QString Screen1 = QStringLiteral("screen-1");
const QString Screen2 = QStringLiteral("screen-2");
const QRectF Zone(100, 100, 640, 480);
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
