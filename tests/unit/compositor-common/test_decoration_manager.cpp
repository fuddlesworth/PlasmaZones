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

#include <memory>

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

    // Per-test fixture: a fresh fake bridge + manager before every test
    // (init() / cleanup()). Tests add their own windows; the heap-manager
    // destruction test constructs its own manager against m_bridge.
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

    void testRefcounting()
    {
        m_bridge->addWindow(Win1);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});

        // Second owner: bookkeeping only, no second physical hide.
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(m_bridge->callLog.size(), 1);

        // First release: an owner remains, decoration stays hidden.
        m_mgr->release(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->callLog.size(), 1);

        // Last release: exactly one restore.
        m_mgr->release(Win1, DecorationManager::snap(Screen1));
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(!m_mgr->isOwned(Win1));
        QCOMPARE(m_bridge->callLog.last(), QStringLiteral("setNoBorder(app|1,false)"));
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testPerScreenGranularityAndTransfer()
    {
        m_bridge->addWindow(Win1);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen2));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);

        // Releasing one screen's claim keeps the sibling's intact.
        m_mgr->release(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(m_mgr->isBorderless(Win1));

        // Cross-screen transfer: drop everything except the target screen —
        // never a physical toggle.
        m_bridge->clearLog();
        m_mgr->releaseOthersOfKind(Win1, OwnerKind::Autotile, Screen2);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(m_mgr->isOwnedBy(Win1, DecorationManager::autotile(Screen2)));

        m_mgr->releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testCsdIntentOnly()
    {
        auto* fw = m_bridge->addWindow(Win1);
        // userCanSetNoBorder alone gates eligibility — the manager never
        // reads hasDecoration (the CSD/SSD distinction is folded into the
        // toggleability test).
        fw->userCanSetNoBorder = false; // GTK/Electron CSD

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        QVERIFY(m_mgr->isOwned(Win1));
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(m_bridge->callLog.isEmpty());

        m_mgr->releaseKind(Win1, OwnerKind::Autotile);
        m_mgr->restoreAll();
        QVERIFY(m_bridge->callLog.isEmpty()); // never physically touched
    }

    void testAlreadyBorderlessPreserved()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->noBorder = true; // user's own compositor rule made it borderless

        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        QVERIFY(m_bridge->callLog.isEmpty()); // nothing to hide

        // Release restores to PRIOR state — which was borderless: no call.
        m_mgr->releaseKind(Win1, OwnerKind::Snap);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testAutotileToSnapHandoff()
    {
        m_bridge->addWindow(Win1);

        // Autotile hides; the window then arrives at snap already borderless.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        m_mgr->releaseKind(Win1, OwnerKind::Autotile);

        // Decoration stayed hidden through the handoff: one hide, no restore.
        const int restores = m_bridge->callLog.filter(QStringLiteral("setNoBorder(app|1,false)")).size();
        QCOMPARE(restores, 0);
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testAlreadyPlacedOrdering()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone; // the zone rect the compositor is moving toward

        m_mgr->acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        const QStringList expected{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(m_bridge->callLog, expected);
        QCOMPARE(m_bridge->window(Win1)->moveResizeGeo, Zone); // re-asserted target
    }

    void testAlreadyPlacedDegenerateTargetSkipsReassert()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = QRectF(); // degenerate

        m_mgr->acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

    void testCallerWillPlaceSkipsReassert()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

    void testStaleSnapshotRefreshedOnOwnerlessReacquire()
    {
        auto* fw = m_bridge->addWindow(Win1);

        // Hide, then force-show via rule veto, then drop the owner — the
        // veto-only entry persists with the ORIGINAL snapshot
        // (priorNoBorder=false).
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_mgr->setRuleOverride(Win1, false);
        m_mgr->releaseKind(Win1, OwnerKind::Snap);
        QCOMPARE(fw->noBorder, false);

        // The user makes the window borderless THEMSELVES while we hold no
        // claim, then a mode re-acquires under the veto and the veto lifts.
        fw->noBorder = true;
        m_bridge->clearLog();
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_mgr->setRuleOverride(Win1, std::nullopt);

        // The refreshed snapshot sees priorNoBorder=true: nothing to hide
        // (already borderless — no physical call at all), and the final
        // release must NOT force-decorate the user's window. A stale
        // snapshot would setNoBorder(false) here.
        m_mgr->releaseKind(Win1, OwnerKind::Snap);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(fw->noBorder, true);
    }

    void testRuleHideAlsoRefreshesStaleSnapshot()
    {
        auto* fw = m_bridge->addWindow(Win1);

        // Same staleness as the acquire() case, reached via the RULE path:
        // ownerless veto-only entry with priorNoBorder=false captured, then
        // the user makes the window borderless themselves.
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_mgr->setRuleOverride(Win1, false);
        m_mgr->releaseKind(Win1, OwnerKind::Snap);
        fw->noBorder = true;
        m_bridge->clearLog();

        // A rule HIDE starts a new ownership epoch through the same refresh:
        // the fresh snapshot sees priorNoBorder=true (already borderless —
        // no physical call at all), so removing the rule must not
        // force-decorate the user's window.
        m_mgr->setRuleOverride(Win1, true);
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(fw->noBorder, true);
    }

    void testResyncAfterExternalReset()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        QCOMPARE(fw->noBorder, true);

        // KWin silently resets noBorder on a desktop switch.
        fw->noBorder = false;
        m_bridge->clearLog();
        m_mgr->resyncWindow(Win1);
        const QStringList expected{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(m_bridge->callLog, expected);

        // Unowned window: no-op.
        m_bridge->clearLog();
        m_mgr->resyncWindow(QStringLiteral("ghost|9"));
        QVERIFY(m_bridge->callLog.isEmpty());
    }

    void testVetoWinsOverOwners()
    {
        auto* fw = m_bridge->addWindow(Win1);

        m_mgr->acquire(Win1, DecorationManager::snap(Screen1), Placement::AlreadyPlaced);
        QCOMPARE(fw->noBorder, true);
        fw->moveResizeGeo = Zone;

        // Rule force-show: restore + geometry re-assert (window is zone-placed).
        m_bridge->clearLog();
        m_mgr->setRuleOverride(Win1, false);
        const QStringList restoreSeq{
            QStringLiteral("setNoBorder(app|1,false)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(m_bridge->callLog, restoreSeq);
        QVERIFY(m_mgr->isVetoed(Win1));
        QVERIFY(m_mgr->isOwned(Win1)); // mode owner persists under the veto

        // Acquiring while vetoed must not hide.
        m_bridge->clearLog();
        m_mgr->acquire(Win1, DecorationManager::snap(Screen2), Placement::AlreadyPlaced);
        QVERIFY(m_bridge->callLog.isEmpty());

        // Veto lifted: owners re-assert with geometry re-assert.
        m_mgr->setRuleOverride(Win1, std::nullopt);
        const QStringList hideSeq{
            QStringLiteral("setNoBorder(app|1,true)"),
            QStringLiteral("moveResize(app|1,640x480)"),
        };
        QCOMPARE(m_bridge->callLog, hideSeq);
        QVERIFY(!m_mgr->isVetoed(Win1));
    }

    void testRuleOwnerHidesAndReleases()
    {
        m_bridge->addWindow(Win1);

        // Rule hide on an otherwise unmanaged (floating) window.
        m_mgr->setRuleOverride(Win1, true);
        QVERIFY(m_mgr->isBorderless(Win1));

        // Mode owner joins; rule goes away — mode keeps it hidden.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);

        // Mode releases — now it restores.
        m_mgr->releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testClearAllRuleOverrides()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));

        m_mgr->setRuleOverride(QStringLiteral("a|1"), true); // rule-only hide
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));
        m_mgr->setRuleOverride(QStringLiteral("b|1"), true); // rule + mode

        m_mgr->clearAllRuleOverrides();
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, false); // restored
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, true); // mode still owns
    }

    void testRestoreAllAndForget()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        auto* csd = m_bridge->addWindow(QStringLiteral("csd|1"));
        csd->userCanSetNoBorder = false;

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));
        m_mgr->acquire(QStringLiteral("csd|1"), DecorationManager::autotile(Screen1));

        // forgetWindow: zero compositor calls, state dropped.
        m_bridge->clearLog();
        m_mgr->forgetWindow(QStringLiteral("a|1"));
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(!m_mgr->isOwned(QStringLiteral("a|1")));

        m_mgr->restoreAll();
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, false);
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, true); // forgotten — untouched
        QVERIFY(!m_mgr->isOwned(QStringLiteral("b|1")));
        // CSD window: still zero physical calls.
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(csd|1,false)")));
    }

    void testReleaseAllOfKindImmediate()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));

        // a is autotile-owned on two screens; b is snap-owned.
        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen2));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::snap(Screen1));

        // The per-mode hide-title-bars-OFF path: all owners of ONE kind drop
        // (across every screen); the other kind is untouched.
        m_mgr->releaseAllOfKind(OwnerKind::Autotile);
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, false);
        QVERIFY(!m_mgr->isOwned(QStringLiteral("a|1")));
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, true);
    }

    void testRuleOverrideClearOnVetoOnlyEntryPrunes()
    {
        auto* fw = m_bridge->addWindow(Win1);

        // Veto-only entry: a force-show rule on an unowned window.
        m_mgr->setRuleOverride(Win1, false);
        QVERIFY(m_mgr->isVetoed(Win1));
        QVERIFY(!m_mgr->isOwned(Win1));
        QVERIFY(m_bridge->callLog.isEmpty()); // nothing was hidden — nothing to do

        // Rule removed: the veto-only entry must clear (and prune) with no
        // physical toggle.
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(!m_mgr->isVetoed(Win1));
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(fw->noBorder, false);
    }

    void testResyncNoopWhileVetoedOrPending()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        // Vetoed: the decoration is deliberately visible — resync must not
        // re-hide it.
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_mgr->setRuleOverride(Win1, false);
        m_bridge->clearLog();
        m_mgr->resyncWindow(Win1);
        QVERIFY(m_bridge->callLog.isEmpty());
        m_mgr->setRuleOverride(Win1, std::nullopt); // re-hides via owner re-assert

        // Deferred-release state: the hide is on its way OUT — resync must
        // not fight the queued restore. (The no-op follows from the emptied
        // owner set; the pendingRestore term in resync's guard is defensive
        // redundancy, since pendingRestore is only ever set once the owner
        // set emptied.)
        m_mgr->releaseKind(Win1, OwnerKind::Snap, Restore::Deferred);
        m_bridge->clearLog();
        fw->noBorder = false; // simulate an external reset mid-defer
        m_mgr->resyncWindow(Win1);
        QVERIFY(m_bridge->callLog.isEmpty());
    }

    void testAcquireIntentOnlyThenResolvedHides()
    {
        // Window not resolvable at first acquire: intent recorded, no calls.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(m_mgr->isOwned(Win1));
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(m_bridge->callLog.isEmpty());

        // Window appears and a later acquire retries: the hide lands.
        m_bridge->addWindow(Win1);
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1), Placement::CallerWillPlace);
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testWindowDecorationRestoredSignalPayload()
    {
        m_bridge->addWindow(Win1);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        QCOMPARE(restored.count(), 0); // hides never emit it
        m_mgr->releaseKind(Win1, OwnerKind::Autotile);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(restored.first().first().toString(), Win1);
    }

    void testReleaseNonOwnerIsNoop()
    {
        m_bridge->addWindow(Win1);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_bridge->clearLog();
        // Untile-diff calls release for windows that may not be owned here.
        m_mgr->release(Win1, DecorationManager::snap(Screen1));
        m_mgr->release(Win1, DecorationManager::autotile(Screen2));
        m_mgr->releaseKind(QStringLiteral("ghost|9"), OwnerKind::Autotile);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(m_mgr->isBorderless(Win1));
    }

    void testReleaseOthersOfKindToUnregisteredScreen()
    {
        m_bridge->addWindow(Win1);

        // Transfer toward a screen whose owner is NOT registered yet (the
        // contract: the caller acquires it next). The owner set empties but
        // the surgery must stay non-physical and the still-hidden entry must
        // survive the prune — the decoration staying hidden across the hop
        // is exactly the point.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_bridge->clearLog();
        m_mgr->releaseOthersOfKind(Win1, OwnerKind::Autotile, Screen2);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(!m_mgr->isOwned(Win1));
        QVERIFY(m_mgr->isBorderless(Win1)); // ownerless but still hidden

        // The expected follow-up acquire re-claims with no physical flap.
        m_mgr->acquire(Win1, DecorationManager::autotile(Screen2), Placement::CallerWillPlace);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(m_mgr->isOwnedBy(Win1, DecorationManager::autotile(Screen2)));

        // If the follow-up acquire never lands (retile declined the window),
        // restoreAll() is the safety net that recovers the hidden orphan.
        m_mgr->releaseOthersOfKind(Win1, OwnerKind::Autotile, Screen1);
        QVERIFY(m_mgr->isBorderless(Win1));
        m_mgr->restoreAll();
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testRestoreAllReentrantAcquireKeepsNewClaimHidden()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));

        // A windowDecorationRestored handler re-acquires the SIBLING of each
        // restored window mid-teardown (e.g. a snap takeover racing the
        // daemon-loss restore burst). The restore ORDER is
        // hash-iteration-dependent, but re-claiming the sibling guarantees
        // the skip-guard branch is exercised in EVERY ordering: whichever
        // window restores first triggers a re-claim of the other while it
        // is still hidden, and the loop must then skip it (re-entrant
        // m_windows entry) instead of force-restoring the new epoch's claim.
        connect(m_mgr.get(), &DecorationManager::windowDecorationRestored, m_mgr.get(),
                [this](const QString& windowId) {
                    m_mgr->acquire(windowId == QStringLiteral("a|1") ? QStringLiteral("b|1") : QStringLiteral("a|1"),
                                   DecorationManager::snap(Screen1));
                });

        m_mgr->restoreAll();
        const bool aRestored = !m_bridge->window(QStringLiteral("a|1"))->noBorder;
        const bool bRestored = !m_bridge->window(QStringLiteral("b|1"))->noBorder;
        // Exactly one window restored; the sibling was re-claimed mid-burst
        // and the skip-guard left it hidden and owned.
        QVERIFY(aRestored != bRestored);
        const QString reclaimed = aRestored ? QStringLiteral("b|1") : QStringLiteral("a|1");
        QVERIFY(m_mgr->isOwnedBy(reclaimed, DecorationManager::snap(Screen1)));
        QCOMPARE(m_bridge->window(reclaimed)->noBorder, true);
        // The re-claimed window must never have been force-decorated.
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(%1,false)").arg(reclaimed)));

        // The physical hide was TRANSFERRED to the new epoch (the re-entrant
        // acquire evaluated against our own old hide and would otherwise have
        // latched priorNoBorder=true): the manager still reports the window
        // borderless, and the new owner's release must restore it — the
        // reclaim must never strand the title bar.
        QVERIFY(m_mgr->isBorderless(reclaimed));
        m_mgr->releaseKind(reclaimed, OwnerKind::Snap);
        QCOMPARE(m_bridge->window(reclaimed)->noBorder, false);
    }

    void testRestoreAllSurvivesManagerDestructionFromSlot()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        auto* mgr = new DecorationManager(*m_bridge);

        mgr->acquire(QStringLiteral("a|1"), DecorationManager::autotile(Screen1));
        mgr->acquire(QStringLiteral("b|1"), DecorationManager::autotile(Screen1));

        // A restored-signal slot deletes the manager mid-teardown (the
        // documented re-entrancy contract). The epilogue guard must stop the
        // loop cleanly: exactly one window restored, no crash, no UAF.
        connect(mgr, &DecorationManager::windowDecorationRestored, mgr, [mgr](const QString&) {
            delete mgr;
        });
        mgr->restoreAll();

        const int restores = m_bridge->callLog.filter(QStringLiteral(",false)")).size();
        QCOMPARE(restores, 1);
    }

    void testReleaseOthersOfKindLeavesOtherKindsAlone()
    {
        m_bridge->addWindow(Win1);

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        m_mgr->acquire(Win1, DecorationManager::snap(Screen1));
        m_bridge->clearLog();

        // Kind-filtered surgery: an autotile cross-screen transfer must not
        // disturb a coexisting snap owner (mid mode-handoff).
        m_mgr->releaseOthersOfKind(Win1, OwnerKind::Autotile, Screen2);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(m_mgr->isOwnedBy(Win1, DecorationManager::snap(Screen1)));
        QVERIFY(!m_mgr->isOwnedBy(Win1, DecorationManager::autotile(Screen1)));
        QVERIFY(m_mgr->isBorderless(Win1));

        // Releasing the surviving snap owner now restores — no autotile
        // owner remains.
        m_mgr->releaseKind(Win1, OwnerKind::Snap);
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testResolveExactNeverTogglesFuzzySibling()
    {
        m_bridge->fuzzyFindByAppId = true;
        m_bridge->addWindow(QStringLiteral("app|1"));
        m_bridge->addWindow(QStringLiteral("app|2"));

        m_mgr->acquire(QStringLiteral("app|1"), DecorationManager::autotile(Screen1));
        QCOMPARE(m_bridge->window(QStringLiteral("app|1"))->noBorder, true);

        // The hidden window dies; the bridge's fuzzy fallback now resolves
        // the dead id to the same-app SIBLING. The release's restore must
        // detect the id mismatch (resolveExact) and touch nothing — toggling
        // the sibling under the dead key would be unreleasable.
        m_bridge->removeWindow(QStringLiteral("app|1"));
        m_bridge->clearLog();
        m_mgr->releaseKind(QStringLiteral("app|1"), OwnerKind::Autotile);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(m_bridge->window(QStringLiteral("app|2"))->noBorder, false);
    }

    void testResyncNoopWhileStillSuppressed()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->acquire(Win1, DecorationManager::autotile(Screen1));
        QCOMPARE(fw->noBorder, true);

        // No external reset happened: the compositor still reports
        // noBorder=true, so resync must detect "nothing drifted" and stay
        // silent — no redundant re-hide / geometry churn.
        m_bridge->clearLog();
        m_mgr->resyncWindow(Win1);
        QVERIFY(m_bridge->callLog.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDecorationManager)
#include "test_decoration_manager.moc"
