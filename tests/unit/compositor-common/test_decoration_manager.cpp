// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_manager.cpp
 * @brief Unit tests for PhosphorCompositor::DecorationManager
 *
 * The behavioral spec for unified title-bar management, which now flows
 * entirely through the window-rule owner: ownership/veto refcounting,
 * CSD/already-borderless handling, hide/restore ordering with the
 * AlreadyPlaced geometry re-assert, external-reset resync, the rule
 * force-show veto, and teardown (restoreAll) re-entrancy. These are exactly
 * the historical bug classes that the per-mode borderless implementations
 * kept regressing on.
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorCompositor/DecorationManager.h>

#include "fake_compositor_bridge.h"

#include <memory>

using PhosphorCompositor::DecorationManager;
using Placement = PhosphorCompositor::DecorationManager::Placement;

namespace {
const QString Win1 = QStringLiteral("app|1");
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

    void testRuleHideAndRestore()
    {
        m_bridge->addWindow(Win1);

        // Rule hide: one physical hide, window reported borderless and owned.
        m_mgr->setRuleOverride(Win1, true);
        QVERIFY(m_mgr->isBorderless(Win1));
        QVERIFY(m_mgr->isOwned(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);

        // Rule cleared: exactly one restore, ownership dropped.
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(!m_mgr->isOwned(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, false);
    }

    void testAcquireRefcountIsIdempotent()
    {
        m_bridge->addWindow(Win1);

        // The Rule owner is a singleton: acquiring it twice is bookkeeping
        // only, no second physical hide.
        m_mgr->acquire(Win1, DecorationManager::rule(), Placement::CallerWillPlace);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
        m_mgr->acquire(Win1, DecorationManager::rule(), Placement::AlreadyPlaced);
        QCOMPARE(m_bridge->callLog.size(), 1);
        QVERIFY(m_mgr->isOwnedBy(Win1, DecorationManager::rule()));
    }

    void testCsdIntentOnly()
    {
        auto* fw = m_bridge->addWindow(Win1);
        // userCanSetNoBorder alone gates eligibility — the manager never
        // reads hasDecoration (the CSD/SSD distinction is folded into the
        // toggleability test).
        fw->userCanSetNoBorder = false; // GTK/Electron CSD

        m_mgr->setRuleOverride(Win1, true);
        QVERIFY(m_mgr->isOwned(Win1));
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(m_bridge->callLog.isEmpty());

        m_mgr->setRuleOverride(Win1, std::nullopt);
        m_mgr->restoreAll();
        QVERIFY(m_bridge->callLog.isEmpty()); // never physically touched
    }

    void testAlreadyBorderlessPreserved()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->noBorder = true; // user's own compositor rule made it borderless

        m_mgr->setRuleOverride(Win1, true);
        QVERIFY(m_bridge->callLog.isEmpty()); // nothing to hide

        // Clear restores to PRIOR state — which was borderless: no call.
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testAlreadyPlacedOrdering()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone; // the zone rect the compositor is moving toward

        // setRuleOverride routes through acquire(rule(), AlreadyPlaced).
        m_mgr->setRuleOverride(Win1, true);
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

        m_mgr->setRuleOverride(Win1, true);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

    void testCallerWillPlaceSkipsReassert()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->acquire(Win1, DecorationManager::rule(), Placement::CallerWillPlace);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,true)")});
    }

    void testRuleHideRefreshesStaleSnapshot()
    {
        auto* fw = m_bridge->addWindow(Win1);

        // Force-show veto on a hidden window leaves an ownerless veto-only
        // entry whose snapshot still says priorNoBorder=false.
        m_mgr->setRuleOverride(Win1, true);
        m_mgr->setRuleOverride(Win1, false);
        QCOMPARE(fw->noBorder, false);

        // The user makes the window borderless THEMSELVES while we hold no
        // claim.
        fw->noBorder = true;
        m_bridge->clearLog();

        // A rule HIDE starts a new ownership epoch through the refresh: the
        // fresh snapshot sees priorNoBorder=true (already borderless — no
        // physical call at all), so removing the rule must not force-decorate
        // the user's window.
        m_mgr->setRuleOverride(Win1, true);
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(fw->noBorder, true);
    }

    void testForceShowVetoRestores()
    {
        auto* fw = m_bridge->addWindow(Win1);

        m_mgr->setRuleOverride(Win1, true);
        QCOMPARE(fw->noBorder, true);

        // Rule force-show: restore and pin the decoration visible. The rule
        // owner is dropped, so the restore does not re-assert geometry.
        m_bridge->clearLog();
        m_mgr->setRuleOverride(Win1, false);
        QCOMPARE(m_bridge->callLog, QStringList{QStringLiteral("setNoBorder(app|1,false)")});
        QVERIFY(m_mgr->isVetoed(Win1));
        QVERIFY(!m_mgr->isOwned(Win1));

        // A rule hide arriving while vetoed clears the veto and re-hides.
        m_bridge->clearLog();
        m_mgr->setRuleOverride(Win1, true);
        QVERIFY(!m_mgr->isVetoed(Win1));
        QCOMPARE(fw->noBorder, true);
    }

    void testResyncAfterExternalReset()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->setRuleOverride(Win1, true);
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

    void testResyncNoopWhileStillSuppressed()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        m_mgr->setRuleOverride(Win1, true);
        QCOMPARE(fw->noBorder, true);

        // No external reset happened: the compositor still reports
        // noBorder=true, so resync must detect "nothing drifted" and stay
        // silent — no redundant re-hide / geometry churn.
        m_bridge->clearLog();
        m_mgr->resyncWindow(Win1);
        QVERIFY(m_bridge->callLog.isEmpty());
    }

    void testResyncNoopWhileVetoed()
    {
        auto* fw = m_bridge->addWindow(Win1);
        fw->moveResizeGeo = Zone;

        // Vetoed: the decoration is deliberately visible — resync must not
        // re-hide it even if the compositor reports it decorated.
        m_mgr->setRuleOverride(Win1, true);
        m_mgr->setRuleOverride(Win1, false);
        m_bridge->clearLog();
        fw->noBorder = false;
        m_mgr->resyncWindow(Win1);
        QVERIFY(m_bridge->callLog.isEmpty());
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

    void testClearAllRuleOverrides()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        auto* b = m_bridge->addWindow(QStringLiteral("b|1"));
        b->noBorder = true; // user-borderless before any rule touched it

        m_mgr->setRuleOverride(QStringLiteral("a|1"), true); // hide
        m_mgr->setRuleOverride(QStringLiteral("b|1"), true); // already borderless — no-op

        m_mgr->clearAllRuleOverrides();
        QCOMPARE(m_bridge->window(QStringLiteral("a|1"))->noBorder, false); // restored
        QCOMPARE(m_bridge->window(QStringLiteral("b|1"))->noBorder, true); // prior state preserved
        QVERIFY(!m_mgr->isOwned(QStringLiteral("a|1")));
        QVERIFY(!m_mgr->isOwned(QStringLiteral("b|1")));
    }

    void testRestoreAllAndForget()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        auto* csd = m_bridge->addWindow(QStringLiteral("csd|1"));
        csd->userCanSetNoBorder = false;

        m_mgr->setRuleOverride(QStringLiteral("a|1"), true);
        m_mgr->setRuleOverride(QStringLiteral("b|1"), true);
        m_mgr->setRuleOverride(QStringLiteral("csd|1"), true);

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

    void testAcquireIntentOnlyThenResolvedHides()
    {
        // Window not resolvable at first acquire: intent recorded, no calls.
        m_mgr->acquire(Win1, DecorationManager::rule(), Placement::CallerWillPlace);
        QVERIFY(m_mgr->isOwned(Win1));
        QVERIFY(!m_mgr->isBorderless(Win1));
        QVERIFY(m_bridge->callLog.isEmpty());

        // Window appears and a later acquire retries: the hide lands.
        m_bridge->addWindow(Win1);
        m_mgr->acquire(Win1, DecorationManager::rule(), Placement::CallerWillPlace);
        QVERIFY(m_mgr->isBorderless(Win1));
        QCOMPARE(m_bridge->window(Win1)->noBorder, true);
    }

    void testWindowDecorationRestoredSignalPayload()
    {
        m_bridge->addWindow(Win1);
        QSignalSpy restored(m_mgr.get(), &DecorationManager::windowDecorationRestored);

        m_mgr->setRuleOverride(Win1, true);
        QCOMPARE(restored.count(), 0); // hides never emit it
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(restored.first().first().toString(), Win1);
    }

    void testClearUnmanagedWindowIsNoop()
    {
        m_bridge->addWindow(Win1);
        // Clearing a rule on a window the manager never tracked is a no-op.
        m_mgr->setRuleOverride(Win1, std::nullopt);
        QVERIFY(m_bridge->callLog.isEmpty());
        QVERIFY(!m_mgr->isOwned(Win1));
    }

    void testResolveExactNeverTogglesFuzzySibling()
    {
        m_bridge->fuzzyFindByAppId = true;
        m_bridge->addWindow(QStringLiteral("app|1"));
        m_bridge->addWindow(QStringLiteral("app|2"));

        m_mgr->setRuleOverride(QStringLiteral("app|1"), true);
        QCOMPARE(m_bridge->window(QStringLiteral("app|1"))->noBorder, true);

        // The hidden window dies; the bridge's fuzzy fallback now resolves
        // the dead id to the same-app SIBLING. The clear's restore must
        // detect the id mismatch (resolveExact) and touch nothing — toggling
        // the sibling under the dead key would be unreleasable.
        m_bridge->removeWindow(QStringLiteral("app|1"));
        m_bridge->clearLog();
        m_mgr->setRuleOverride(QStringLiteral("app|1"), std::nullopt);
        QVERIFY(m_bridge->callLog.isEmpty());
        QCOMPARE(m_bridge->window(QStringLiteral("app|2"))->noBorder, false);
    }

    void testRestoreAllReentrantAcquireKeepsNewClaimHidden()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));

        m_mgr->acquire(QStringLiteral("a|1"), DecorationManager::rule());
        m_mgr->acquire(QStringLiteral("b|1"), DecorationManager::rule());

        // A windowDecorationRestored handler re-acquires the SIBLING of each
        // restored window mid-teardown (e.g. a rule re-evaluation racing the
        // daemon-loss restore burst). The restore ORDER is
        // hash-iteration-dependent, but re-claiming the sibling guarantees
        // the skip-guard branch is exercised in EVERY ordering: whichever
        // window restores first triggers a re-claim of the other while it
        // is still hidden, and the loop must then skip it (re-entrant
        // m_windows entry) instead of force-restoring the new epoch's claim.
        connect(m_mgr.get(), &DecorationManager::windowDecorationRestored, m_mgr.get(),
                [this](const QString& windowId) {
                    m_mgr->acquire(windowId == QStringLiteral("a|1") ? QStringLiteral("b|1") : QStringLiteral("a|1"),
                                   DecorationManager::rule());
                });

        m_mgr->restoreAll();
        const bool aRestored = !m_bridge->window(QStringLiteral("a|1"))->noBorder;
        const bool bRestored = !m_bridge->window(QStringLiteral("b|1"))->noBorder;
        // Exactly one window restored; the sibling was re-claimed mid-burst
        // and the skip-guard left it hidden and owned.
        QVERIFY(aRestored != bRestored);
        const QString reclaimed = aRestored ? QStringLiteral("b|1") : QStringLiteral("a|1");
        QVERIFY(m_mgr->isOwnedBy(reclaimed, DecorationManager::rule()));
        QCOMPARE(m_bridge->window(reclaimed)->noBorder, true);
        // The re-claimed window must never have been force-decorated.
        QVERIFY(!m_bridge->callLog.contains(QStringLiteral("setNoBorder(%1,false)").arg(reclaimed)));

        // The physical hide was TRANSFERRED to the new epoch (the re-entrant
        // acquire evaluated against our own old hide and would otherwise have
        // latched priorNoBorder=true): the manager still reports the window
        // borderless, and the new owner's release must restore it — the
        // reclaim must never strand the title bar.
        QVERIFY(m_mgr->isBorderless(reclaimed));
        m_mgr->setRuleOverride(reclaimed, std::nullopt);
        QCOMPARE(m_bridge->window(reclaimed)->noBorder, false);
    }

    void testRestoreAllSurvivesManagerDestructionFromSlot()
    {
        m_bridge->addWindow(QStringLiteral("a|1"));
        m_bridge->addWindow(QStringLiteral("b|1"));
        auto* mgr = new DecorationManager(*m_bridge);

        mgr->acquire(QStringLiteral("a|1"), DecorationManager::rule());
        mgr->acquire(QStringLiteral("b|1"), DecorationManager::rule());

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
};

QTEST_GUILESS_MAIN(TestDecorationManager)
#include "test_decoration_manager.moc"
