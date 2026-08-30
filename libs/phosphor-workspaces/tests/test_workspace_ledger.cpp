// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "workspacereconcilerharness.h"

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorWorkspaces::WorkspaceReconciler;
using WorkspaceTest::adoptTwoScreens;
using WorkspaceTest::id;

/// The time-driven half of the reconciler: the destroy debounce, ledger expiry,
/// the live cap probe and the removal-refusal budget. Separated from
/// test_workspace_reconciler because every case here waits on real timers, and
/// because these are the paths where being right on the SECOND round is the
/// whole point.
class TestWorkspaceLedger : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void destroyOnEmpty_debouncedAndRechecked();
    void destroyOnEmpty_namedExempt();
    void destroyOnEmpty_neverTrailingOrLast();
    void cap_suspendsTrailingEmpty();
    void capProbe_oneExpiryRetriesAndKeepsCreating();
    void capProbe_learnsFromRepeatedExpiryAndSelfHeals();
    void capProbe_successClearsTheEvidence();
    void removalRefusals_stopAtTheBudgetAndResumeOnPopulation();
    void removalRace_signalledForPopulationOnDoomedDesktop();
    void foreign_pausedDuringRemovalThenReevaluatedAtSettle();
    void adoption_keepsKnownPopulations();
    void setCurrent_untranslatableTargetNeverReachesTheLedger();
};

void TestWorkspaceLedger::destroyOnEmpty_debouncedAndRechecked()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    // Occupy A's trailing empty, realize the new one.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);

    // {d3} (now mid-slice, occupied) empties → debounce, then remove.
    rec.onPopulationChanged(id(3), 0);
    QCOMPARE(removeSpy.count(), 0); // not before the debounce
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, 2000);
    QCOMPARE(removeSpy.first().first().toString(), id(3));

    // The destroy-debounce re-check: a desktop that re-fills inside the
    // debounce window is NOT removed when the timer fires.
    rec.onKwinDesktopRemoved(id(3));
    rec.onDesktopListSettled({id(1), id(5), id(2), id(4)});
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(1), 0);
    rec.onPopulationChanged(id(1), 2); // re-filled before the debounce fires
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    QCOMPARE(removeSpy.count(), 1); // no second remove
}

void TestWorkspaceLedger::destroyOnEmpty_namedExempt()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.map().setName(id(1), QStringLiteral("chat"));

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(1), 0);
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    QCOMPARE(removeSpy.count(), 0);
}

void TestWorkspaceLedger::destroyOnEmpty_neverTrailingOrLast()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);

    // The trailing empty {d3} bouncing 1→0 is invariant repair, not surplus.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    rec.onPopulationChanged(id(5), 0); // trailing empty stays
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    QCOMPARE(removeSpy.count(), 0);
}

void TestWorkspaceLedger::cap_suspendsTrailingEmpty()
{
    WorkspaceReconciler rec;
    rec.setDesktopCap(2);
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current); // already at cap of 2

    // Both desktops occupied → trailing-empty append wanted, but capped.
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 0);
    QCOMPARE(capSpy.count(), 1);
    // The hint fires once per episode.
    rec.onPopulationChanged(id(1), 2);
    QCOMPARE(capSpy.count(), 1);
}

void TestWorkspaceLedger::capProbe_oneExpiryRetriesAndKeepsCreating()
{
    // One unanswered create is a lost create, not a ceiling. A D-Bus stall or a
    // KWin restart expires a Create exactly the way a cap refusal does, and
    // lowering the ceiling on that evidence suspends every later create for the
    // rest of the session (requestCreateAt gates at equality), with no way left
    // for the daemon to disprove it. So the first expiry must re-ask instead.
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current);

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 1);

    // The op expires unanswered and the request is made again.
    QTRY_COMPARE_WITH_TIMEOUT(createSpy.count(), 2, WorkspaceReconciler::LedgerTimeoutMs + 2000);
    QCOMPARE(capSpy.count(), 0);
}

void TestWorkspaceLedger::capProbe_learnsFromRepeatedExpiryAndSelfHeals()
{
    // A genuine refusal, though, answers every attempt the same way: KWin
    // declines createDesktop past its maximum silently, so a run of expiries
    // with the id list unchanged throughout IS the compositor's ceiling.
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current);

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(capSpy.count(), 1,
                              WorkspaceReconciler::CapProbeExpiries * WorkspaceReconciler::LedgerTimeoutMs + 3000);
    // Exactly one attempt per expiry, and the retries stop once the ceiling is
    // believed — no re-ask/expire spin.
    QCOMPARE(createSpy.count(), WorkspaceReconciler::CapProbeExpiries);
    QTest::qWait(WorkspaceReconciler::LedgerTimeoutMs + 500);
    QCOMPARE(createSpy.count(), WorkspaceReconciler::CapProbeExpiries);

    // At the learned cap, further trailing-empty wants are suspended.
    const int atCap = createSpy.count();
    rec.onPopulationChanged(id(1), 2);
    QCOMPARE(createSpy.count(), atCap);

    // Self-heal: an external create pushes the count past the learned cap, so
    // the ceiling was not where we thought and appends resume.
    rec.onKwinDesktopCreated(id(3));
    rec.onDesktopListSettled({id(1), id(2), id(3)});
    rec.onPopulationChanged(id(3), 1); // trailing occupied again
    QCOMPARE(createSpy.count(), atCap + 1);
}

void TestWorkspaceLedger::capProbe_successClearsTheEvidence()
{
    // The daemon's OWN proof path: a create KWin actually answers says the
    // ceiling is not where the expiries pointed, so the accumulated evidence is
    // dropped and the next lost create starts a fresh episode rather than
    // completing the previous one.
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current);

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QTRY_COMPARE_WITH_TIMEOUT(createSpy.count(), 2, WorkspaceReconciler::LedgerTimeoutMs + 2000);

    // KWin answers the retry with an id-only echo. The settled list has not
    // arrived, so the id list is still the one the first expiry was recorded
    // against — the case a count that ignored successes would mislearn on.
    rec.onKwinDesktopCreated(id(3));
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));

    // Occupy the new trailing empty and lose the next create too.
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(createSpy.count(), 4, WorkspaceReconciler::LedgerTimeoutMs + 2000);
    // One expiry since the success, so no ceiling is concluded.
    QCOMPARE(capSpy.count(), 0);
}

void TestWorkspaceLedger::removalRefusals_stopAtTheBudgetAndResumeOnPopulation()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    // Occupy A's trailing empty and realize the next one, so {d3} is a
    // mid-slice desktop the destroy sweep is allowed to reap.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(3), 0);

    // KWin never answers. Each expiry re-arms the destroy check once, and the
    // re-arms stop at the budget rather than looping for the session.
    const int budget = WorkspaceReconciler::MaxRemovalRefusals;
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), budget,
                              budget * (WorkspaceReconciler::LedgerTimeoutMs + WorkspaceReconciler::DestroyDebounceMs)
                                  + 4000);
    QTest::qWait(WorkspaceReconciler::LedgerTimeoutMs + WorkspaceReconciler::DestroyDebounceMs + 500);
    QCOMPARE(removeSpy.count(), budget);
    // The desktop is still there, which is the accepted outcome.
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));

    // A population change is the compositor telling us the situation changed,
    // so the budget comes back and the sweep gets to try again.
    rec.onPopulationChanged(id(3), 1);
    rec.onPopulationChanged(id(3), 0);
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), budget + 1, 2000);
}

void TestWorkspaceLedger::removalRace_signalledForPopulationOnDoomedDesktop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    // Occupy A's trailing empty {d3}, realize the next one, then empty {d3}
    // so the destroy debounce issues its Remove.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    QSignalSpy raceSpy(&rec, &WorkspaceReconciler::removalRaceDetected);
    rec.onPopulationChanged(id(3), 0);
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, 2000);

    // A window maps onto {d3} while the Remove is in flight: the race is
    // surfaced with the owner so the controller can re-route afterwards.
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(raceSpy.count(), 1);
    QCOMPARE(raceSpy.first().at(0).toString(), id(3));
    QCOMPARE(raceSpy.first().at(1).toString(), QStringLiteral("A"));
}

void TestWorkspaceLedger::foreign_pausedDuringRemovalThenReevaluatedAtSettle()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(3), 0);
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, 2000);

    // While the Remove is open, KWin's renumber/clamp interim reports must
    // not trigger owner-wins policy (they read as foreign against the
    // pre-removal map)...
    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);
    rec.onScreenDesktopReport(QStringLiteral("B"), 1); // d1 — A's desktop
    QCOMPARE(foreignSpy.count(), 0);

    // ...and the settled list re-evaluates every screen: B genuinely sits on
    // A's desktop, so the policy fires now.
    rec.onKwinDesktopRemoved(id(3));
    rec.onDesktopListSettled({id(1), id(5), id(2), id(4)});
    QCOMPARE(foreignSpy.count(), 1);
    QCOMPARE(foreignSpy.first().at(0).toString(), QStringLiteral("B"));
    QCOMPARE(foreignSpy.first().at(2).toString(), QStringLiteral("A"));
}

void TestWorkspaceLedger::adoption_keepsKnownPopulations()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);

    // A population report can precede the desktop's adoption (the census is
    // event-driven); clearing it at settle would make destroy-on-empty
    // blind to the later 2→0 transition.
    rec.onPopulationChanged(id(9), 2);
    rec.onDesktopListSettled({id(1), id(3), id(9), id(2), id(4)});

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(9), 0);
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, 2000);
    QCOMPARE(removeSpy.first().first().toString(), id(9));
}

void TestWorkspaceLedger::setCurrent_untranslatableTargetNeverReachesTheLedger()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    // The settled list is {d1,d3,d2,d4}; {d9} is a desktop KWin has not
    // settled, so the controller could not resolve it to a live desktop number
    // and would drop the request downstream.
    QSignalSpy switchSpy(&rec, &WorkspaceReconciler::requestSetCurrent);
    QSignalSpy resyncSpy(&rec, &WorkspaceReconciler::resyncRequested);

    QVERIFY(!rec.issueSetCurrent(QStringLiteral("A"), id(9)));
    QCOMPARE(switchSpy.count(), 0);

    // The point of refusing BEFORE the ledger: nothing is open, so the very
    // next legitimate switch for the same screen goes through. Revert the
    // refusal and the hopeless entry is ledgered instead, the one-correction-
    // per-screen rule refuses this call, and the screen stays short-circuited
    // until the entry expires into a resync.
    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(3)));
    QCOMPARE(switchSpy.count(), 1);
    QCOMPARE(switchSpy.first().at(1).toString(), id(3));

    // And the refusal left no entry behind to expire.
    QCOMPARE(resyncSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TestWorkspaceLedger)
#include "test_workspace_ledger.moc"
