// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorWorkspaces::WorkspaceReconciler;

namespace {
QString id(int n)
{
    return QStringLiteral("{d%1}").arg(n);
}
}

/// Drives the reconciler with scripted notification sequences (no D-Bus).
/// The harness plays KWin: every requestCreateDesktop is answered by invoking
/// the created/settled callbacks the way VirtualDesktopManager would.
class TestWorkspaceReconciler : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // QSignalSpy must copy these signal argument types into QVariants.
        qRegisterMetaType<QHash<int, int>>("QHash<int,int>");
        qRegisterMetaType<QList<int>>("QList<int>");
    }
    void adoption_currentFirstThenContiguous();
    void adoption_appendsTrailingEmpty();
    void createOnOccupy_appendsNextEmpty();
    void destroyOnEmpty_debouncedAndRechecked();
    void destroyOnEmpty_namedExempt();
    void destroyOnEmpty_neverTrailingOrLast();
    void externalCreation_adoptedByFocusedScreen();
    void externalRemoval_followedAndRepaired();
    void renumber_computedFromIdDelta();
    void echo_ledgerSuppressesReactivePolicy();
    void snapBack_singleCorrectionNoLoop();
    void cap_suspendsTrailingEmpty();
    void capProbe_learnsFromCreateExpiryAndSelfHeals();
    void screenRemoved_sliceReassigned();
    void verbQueries_sliceScopedNoWrap();
    void issueSetCurrent_singleInFlightPerScreen();
    void snapBack_correctsAndBreaksLoop();
    void reorderCurrentWorkspace_withinSlice();
    void transferCurrentWorkspace_reownsAndRepairs();
    void hotplug_homeStampAndMigrateBack();
    void restore_candidateReconciledAgainstReality();
    void named_createdPinnedAndExempt();
    void named_claimByKWinName();
    void named_unnamedRevertsToDynamic();
    void removalRace_signalledForPopulationOnDoomedDesktop();
    void foreign_pausedDuringRemovalThenReevaluatedAtSettle();
    void adoption_keepsKnownPopulations();
    void named_createEchoFifoMismatchHealedByKWinNames();
    void named_placeholderNameNeverClaims();
    void named_explicitPositionStaysBeforeTrailingEmpty();
    void settled_emptyListIsIgnored();
    void screenAdded_freshScreenGetsItsOwnDesktop();
    void create_secondPendingSurvivesTheFirstsSettle();
    void create_settledBeforeEchoLandsOnRequestingScreen();
    void create_settledMatchesByPositionNotRequestOrder();
    void create_threeConcurrentSettleLandOnTheirOwnScreens();

private:
    /// Two screens, each occupied and each therefore owing a trailing-empty
    /// create, with BOTH Creates open in the ledger at once. A owns {d1}
    /// (occupied), B owns {d2} (occupied); nothing has landed yet.
    /// `populateBFirst` flips which screen's population change fires first, and
    /// so which Create heads the ledger. The screen ORDER is [A,B] either way,
    /// so B-first is the case where oldest-request order and KWin-position
    /// order disagree.
    void openTwoConcurrentCreates(WorkspaceReconciler& rec, QSignalSpy& createSpy, bool populateBFirst = false)
    {
        rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
        rec.setFocusedScreen(QStringLiteral("A"));

        QHash<QString, QString> current;
        current.insert(QStringLiteral("A"), id(1));
        current.insert(QStringLiteral("B"), id(2));
        rec.adoptAll({id(1), id(2)}, current);
        QCOMPARE(createSpy.count(), 0);

        if (populateBFirst) {
            rec.onPopulationChanged(id(2), 1);
            rec.onPopulationChanged(id(1), 1);
        } else {
            rec.onPopulationChanged(id(1), 1);
            rec.onPopulationChanged(id(2), 1);
        }
        QCOMPARE(createSpy.count(), 2);
    }

    /// Adopt a two-screen world: A owns {d1} (current), B owns {d2} (current).
    /// With an empty census both slices already end in an empty dynamic
    /// desktop, so adoption alone requests nothing; occupying each current
    /// desktop then drives the trailing-empty creates, answered so A has
    /// {d1(occupied), d3(empty)} and B has {d2(occupied), d4(empty)}.
    void adoptTwoScreens(WorkspaceReconciler& rec)
    {
        rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
        rec.setFocusedScreen(QStringLiteral("A"));

        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        QHash<QString, QString> current;
        current.insert(QStringLiteral("A"), id(1));
        current.insert(QStringLiteral("B"), id(2));
        rec.adoptAll({id(1), id(2)}, current);
        QCOMPARE(createSpy.count(), 0); // both slices end in an empty desktop

        rec.onPopulationChanged(id(1), 1);
        rec.onPopulationChanged(id(2), 1);
        QCOMPARE(createSpy.count(), 2);
        // Answer them: A's lands as {d3} at global position 1, B's as {d4}.
        rec.onKwinDesktopCreated(id(3));
        rec.onKwinDesktopCreated(id(4));
        rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

        QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
        QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
        QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
        QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    }
};

void TestWorkspaceReconciler::adoption_currentFirstThenContiguous()
{
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});

    // Four desktops; A shows d2, B shows d4. d1 precedes d2 → follows A's
    // segment start rule (leading run → first screen); d3 follows d2 → A.
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(2));
    current.insert(QStringLiteral("B"), id(4));
    rec.adoptAll({id(1), id(2), id(3), id(4)}, current);

    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::adoption_appendsTrailingEmpty()
{
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    adoptTwoScreens(rec);
    // adoptTwoScreens already verified both creates were requested + realized;
    // maintenance is now quiet (both screens hold a trailing empty).
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::createOnOccupy_appendsNextEmpty()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    // A window lands on A's trailing empty {d3} → a new empty is appended.
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 1);
    // Position arithmetic: end of A's slice = global position 2.
    QCOMPARE(createSpy.first().first().toUInt(), 2u);

    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 3);
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::destroyOnEmpty_debouncedAndRechecked()
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

void TestWorkspaceReconciler::destroyOnEmpty_namedExempt()
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

void TestWorkspaceReconciler::destroyOnEmpty_neverTrailingOrLast()
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
    for (const auto& call : removeSpy) {
        QVERIFY(call.first().toString() != id(5));
    }
}

void TestWorkspaceReconciler::externalCreation_adoptedByFocusedScreen()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.setFocusedScreen(QStringLiteral("B"));

    // Unmatched desktopCreated (Pager/System Settings): adopt onto B, before
    // its trailing empty.
    rec.onKwinDesktopCreated(id(9));
    QCOMPARE(rec.map().ownerOf(id(9)), QStringLiteral("B"));
    QCOMPARE(rec.map().sliceIndexOf(id(9)), 1); // before trailing empty {d4}
}

void TestWorkspaceReconciler::externalRemoval_followedAndRepaired()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    // External removal of B's trailing empty {d4}: follow the fact, then
    // maintenance restores B's trailing empty.
    rec.onKwinDesktopRemoved(id(4));
    QVERIFY(rec.map().ownerOf(id(4)).isEmpty());
    rec.onDesktopListSettled({id(1), id(3), id(2)});
    QCOMPARE(createSpy.count(), 1);
}

void TestWorkspaceReconciler::renumber_computedFromIdDelta()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last settled list: {d1} {d3} {d2} {d4}
    QSignalSpy renumberSpy(&rec, &WorkspaceReconciler::renumberComputed);

    // {d3} (global position 2) vanishes: 3→2, 4→3; removed = [2].
    rec.onDesktopListSettled({id(1), id(2), id(4)});
    QCOMPARE(renumberSpy.count(), 1);
    const auto oldToNew = renumberSpy.first().at(0).value<QHash<int, int>>();
    const auto removed = renumberSpy.first().at(1).value<QList<int>>();
    QCOMPARE(removed, QList<int>({2}));
    QCOMPARE(oldToNew.value(3), 2);
    QCOMPARE(oldToNew.value(4), 3);
    QVERIFY(!oldToNew.contains(1));
}

void TestWorkspaceReconciler::echo_ledgerSuppressesReactivePolicy()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);

    // Our own create (from occupying the trailing empty) echoes back as
    // desktopCreated — matched, so NO adoption-by-focused-screen runs (it
    // realizes A's planned slice entry instead, even with focus on B).
    rec.setFocusedScreen(QStringLiteral("B"));
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 1);
    rec.onKwinDesktopCreated(id(7));
    QCOMPARE(rec.map().ownerOf(id(7)), QStringLiteral("A"));

    // A matched SetCurrent echo triggers no foreign-switch policy: A showing
    // its own desktop is never foreign regardless.
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);
    QCOMPARE(foreignSpy.count(), 0);
}

void TestWorkspaceReconciler::snapBack_singleCorrectionNoLoop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}; {d2} owned by B

    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);
    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    // A externally switches onto B's desktop {d2} (global index 3).
    const bool matched = rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(!matched);
    QCOMPARE(foreignSpy.count(), 1);
    QCOMPARE(foreignSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(foreignSpy.first().at(1).toString(), id(2));
    QCOMPARE(foreignSpy.first().at(2).toString(), QStringLiteral("B"));

    // Phase 2 wires foreignSwitchDetected → requestSetCurrent. Simulate that
    // correction being issued and verify the loop-breaker: while the
    // SetCurrent ledger entry is open, further foreign reports for A are
    // queued (no second foreignSwitchDetected), and the correction's own echo
    // retires the entry without re-triggering policy.
    // (The reconciler exposes no direct issue API pre-Phase-2; the queueing
    // guard is exercised through the ledger by the Phase 2 wiring. Here we
    // pin the Phase 1 half: repeated identical foreign reports re-fire the
    // signal only per report arrival, and a matched echo never does.)
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QCOMPARE(foreignSpy.count(), 2); // unmatched external reports each surface
    QCOMPARE(setCurrentSpy.count(), 0); // Phase 1 issues no corrections itself
}

void TestWorkspaceReconciler::cap_suspendsTrailingEmpty()
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

void TestWorkspaceReconciler::capProbe_learnsFromCreateExpiryAndSelfHeals()
{
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current);

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);

    // Occupying the trailing empty requests a create; KWin refuses SILENTLY
    // (no echo), so the ledger entry expires and the reconciler learns the
    // ceiling is the current count (2).
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(capSpy.count(), 1, WorkspaceReconciler::LedgerTimeoutMs + 2000);

    // At the learned cap, further trailing-empty wants are suspended.
    rec.onPopulationChanged(id(1), 2);
    QCOMPARE(createSpy.count(), 1);

    // Self-heal: an external create pushes the count past the learned cap —
    // the mislearn is forgotten and appends resume.
    rec.onKwinDesktopCreated(id(3));
    rec.onDesktopListSettled({id(1), id(2), id(3)});
    rec.onPopulationChanged(id(3), 1); // trailing occupied again
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::screenRemoved_sliceReassigned()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1,d3}, B: {d2,d4}

    rec.onScreenRemoved(QStringLiteral("B"));
    QVERIFY(!rec.map().hasScreen(QStringLiteral("B")));
    // B's entries joined A before A's trailing empty: d1, d2, d4, d3.
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("A"));
    const auto slice = rec.map().slice(QStringLiteral("A"));
    QCOMPARE(slice.size(), 4);
    QCOMPARE(slice.last().desktopId, id(3)); // trailing empty stayed last
}

void TestWorkspaceReconciler::verbQueries_sliceScopedNoWrap()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}; A shows d1 (index 1)
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QCOMPARE(rec.currentDesktopIdOf(QStringLiteral("A")), id(1));
    QCOMPARE(rec.desktopIdAtOffset(QStringLiteral("A"), 1), id(3));
    QVERIFY(rec.desktopIdAtOffset(QStringLiteral("A"), -1).isEmpty()); // top edge, no wrap
    QCOMPARE(rec.desktopIdAtSliceIndex(QStringLiteral("A"), 1), id(3));
    QVERIFY(rec.desktopIdAtSliceIndex(QStringLiteral("A"), 2).isEmpty());
    // A screen showing a FOREIGN desktop resolves no offsets.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3); // {d2}, B's
    QVERIFY(rec.desktopIdAtOffset(QStringLiteral("A"), 1).isEmpty());
}

void TestWorkspaceReconciler::issueSetCurrent_singleInFlightPerScreen()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(3)));
    QVERIFY(!rec.issueSetCurrent(QStringLiteral("A"), id(1))); // one in flight
    QVERIFY(rec.issueSetCurrent(QStringLiteral("B"), id(4))); // other screens unaffected
    QCOMPARE(setCurrentSpy.count(), 2);

    // The echo (A now reports d3 = global index 2) retires the entry and
    // frees the screen for the next switch.
    QVERIFY(rec.onScreenDesktopReport(QStringLiteral("A"), 2));
    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(1)));
}

void TestWorkspaceReconciler::snapBack_correctsAndBreaksLoop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1); // remembers d1 as owned

    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    // A lands on B's {d2} (index 3): snap-back targets the last owned d1.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);
    QCOMPARE(setCurrentSpy.first().at(1).toString(), id(1));

    // The Pager re-asserts the foreign desktop while the correction is in
    // flight: snapBack refuses (single correction), no loop.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(!rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);

    // The correction's echo retires the ledger entry; A is home, snapBack is
    // a no-op (not a fresh correction).
    QVERIFY(rec.onScreenDesktopReport(QStringLiteral("A"), 1));
    QVERIFY(!rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);
}

void TestWorkspaceReconciler::reorderCurrentWorkspace_withinSlice()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3(empty)}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QVERIFY(rec.reorderCurrentWorkspace(QStringLiteral("A"), 1));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).last().desktopId, id(1));
    QVERIFY(!rec.reorderCurrentWorkspace(QStringLiteral("A"), 1)); // edge
    QVERIFY(rec.reorderCurrentWorkspace(QStringLiteral("A"), -1));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).first().desktopId, id(1));
}

void TestWorkspaceReconciler::transferCurrentWorkspace_reownsAndRepairs()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);
    const QString moved = rec.transferCurrentWorkspace(QStringLiteral("A"), QStringLiteral("B"));
    QCOMPARE(moved, id(1));
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("B"));
    // Inserted before B's trailing empty.
    QCOMPARE(rec.map().sliceIndexOf(id(1)), 1);
    // The source snapped back onto its own slice (d3, its only remaining).
    QCOMPARE(setCurrentSpy.count(), 1);
    QCOMPARE(setCurrentSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(setCurrentSpy.first().at(1).toString(), id(3));

    // A screen never gives up its last desktop.
    rec.onScreenDesktopReport(QStringLiteral("A"), 2); // A now shows d3
    QVERIFY(rec.transferCurrentWorkspace(QStringLiteral("A"), QStringLiteral("B")).isEmpty());
}

void TestWorkspaceReconciler::hotplug_homeStampAndMigrateBack()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}

    // Unplug B: its slice fosters onto A, home-stamped.
    rec.onScreenRemoved(QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().entryFor(id(2)).homeScreenId, QStringLiteral("B"));
    QCOMPARE(rec.map().entryFor(id(4)).homeScreenId, QStringLiteral("B"));
    // A's own entries carry no home stamp.
    QVERIFY(rec.map().entryFor(id(1)).homeScreenId.isEmpty());

    // Replug B: the displaced entries migrate home in order, stamps cleared.
    rec.onScreenAdded(QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QVERIFY(rec.map().entryFor(id(2)).homeScreenId.isEmpty());
    QCOMPARE(rec.map().slice(QStringLiteral("B")).first().desktopId, id(2));
    // A keeps its own slice intact.
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::restore_candidateReconciledAgainstReality()
{
    // Previous session: A owned {d1(named chat), d2}; B owned {d9}.
    PhosphorWorkspaces::WorkspaceMap candidate;
    candidate.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    PhosphorWorkspaces::WorkspaceEntry chat;
    chat.desktopId = id(1);
    chat.name = QStringLiteral("chat");
    candidate.insert(QStringLiteral("A"), 0, chat);
    PhosphorWorkspaces::WorkspaceEntry d2;
    d2.desktopId = id(2);
    candidate.insert(QStringLiteral("A"), 1, d2);
    PhosphorWorkspaces::WorkspaceEntry d9;
    d9.desktopId = id(9);
    candidate.insert(QStringLiteral("B"), 0, d9);

    WorkspaceReconciler rec;
    QVERIFY(rec.map().fromJson(candidate.toJson(1, {}, nullptr, /*includeState=*/true)));
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});

    // Reality: {d9} vanished while the daemon was down; {d5} is new.
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(5));
    rec.adoptAll({id(1), id(2), id(5)}, current);

    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat")); // kept
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A")); // kept
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B")); // adopted (B shows it)
    QVERIFY(rec.map().ownerOf(id(9)).isEmpty()); // vanished id dropped
}

void TestWorkspaceReconciler::named_createdPinnedAndExempt()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    chat.outputId = QStringLiteral("B");
    rec.applyNamedWorkspaces({chat}, {});

    QCOMPARE(createSpy.count(), 1);
    QCOMPARE(createSpy.first().at(1).toString(), QStringLiteral("chat"));
    // A second apply while the create is pending requests nothing extra.
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(createSpy.count(), 1);

    // Realize it: lands in B's slice before the trailing empty, named.
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(5), id(4)});
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B"));
    QCOMPARE(rec.map().entryFor(id(5)).name, QStringLiteral("chat"));

    // Named + empty: destroy-exempt (it is mid-slice and empty).
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(5), 1);
    rec.onPopulationChanged(id(5), 0);
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    for (const auto& call : removeSpy) {
        QVERIFY(call.first().toString() != id(5));
    }
}

void TestWorkspaceReconciler::named_claimByKWinName()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last ids: {d1} {d3} {d2} {d4}

    // Restart-without-state-file shape: KWin still carries the name we
    // stamped last session on {d1} (a NON-trailing desktop — claiming a
    // trailing empty would legitimately trigger a trailing-empty repair
    // create); the declaration claims it, no name-carrying create.
    // The name list is KWin's RAW form: an unnamed desktop is an EMPTY entry,
    // never a "Desktop N" placeholder.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    rec.applyNamedWorkspaces({chat}, {QStringLiteral("chat"), QString(), QString(), QString()});
    QCOMPARE(createSpy.count(), 0);
    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat"));
}

void TestWorkspaceReconciler::named_placeholderNameNeverClaims()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last ids: {d1} {d3} {d2} {d4}, all unnamed in KWin

    // A workspace a user happened to call "Desktop 2" must NOT claim the
    // second desktop just because a display placeholder would read that way.
    // Raw names are empty for every unnamed desktop, so nothing matches and
    // the declaration is created instead.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace decoy;
    decoy.name = QStringLiteral("Desktop 2");
    rec.applyNamedWorkspaces({decoy}, {QString(), QString(), QString(), QString()});

    QCOMPARE(createSpy.count(), 1);
    QCOMPARE(createSpy.first().at(1).toString(), QStringLiteral("Desktop 2"));
    for (const QString& desktopId : {id(1), id(2), id(3), id(4)}) {
        QVERIFY(rec.map().entryFor(desktopId).name.isEmpty());
    }
}

void TestWorkspaceReconciler::named_explicitPositionStaysBeforeTrailingEmpty()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3(trailing empty)}

    // An out-of-range explicit position must clamp to the slot BEFORE the
    // trailing empty. Landing behind it would demote the empty desktop to
    // mid-slice (maintenance then reaps it) and hand the trailing role to a
    // named workspace, which is destroy-exempt — the invariant would never
    // recover.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    chat.outputId = QStringLiteral("A");
    chat.position = 9;
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(createSpy.count(), 1);
    // A's slice starts at global position 0 and holds two desktops, so the
    // slot before its trailing empty is global position 1.
    QCOMPARE(createSpy.first().first().toUInt(), 1u);

    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("A"));
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 1);
    // The trailing empty is still last, and still the unnamed one.
    const auto slice = rec.map().slice(QStringLiteral("A"));
    QCOMPARE(slice.last().desktopId, id(3));
    QVERIFY(slice.last().name.isEmpty());
}

void TestWorkspaceReconciler::settled_emptyListIsIgnored()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1,d3}, B: {d2,d4}

    // A failed upstream read surfaces as an empty list. KWin never has zero
    // desktops, so acting on it would drop every slice and have the engines
    // reap all per-desktop state. Refuse it and ask for a resync instead.
    QSignalSpy resyncSpy(&rec, &WorkspaceReconciler::resyncRequested);
    QSignalSpy renumberSpy(&rec, &WorkspaceReconciler::renumberComputed);
    rec.onDesktopListSettled({});

    QCOMPARE(renumberSpy.count(), 0);
    QCOMPARE(resyncSpy.count(), 1);
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
    // And the verbs still resolve against the kept list.
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);
    QCOMPARE(rec.currentDesktopIdOf(QStringLiteral("A")), id(1));
}

void TestWorkspaceReconciler::screenAdded_freshScreenGetsItsOwnDesktop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);

    // A monitor with no hotplug history joins: it owns nothing, so the
    // slice-never-empty invariant asks for a desktop of its own.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    rec.onScreenAdded(QStringLiteral("C"));
    QCOMPARE(createSpy.count(), 1);
    QVERIFY(rec.hasPendingStructuralOps()); // the Create is open

    rec.onKwinDesktopCreated(id(6));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4), id(6)});
    QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    QVERIFY(!rec.hasPendingStructuralOps()); // and retired once it landed
}

void TestWorkspaceReconciler::named_unnamedRevertsToDynamic()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.map().setName(id(1), QStringLiteral("chat"));

    QSignalSpy renameSpy(&rec, &WorkspaceReconciler::requestSetDesktopName);
    rec.applyNamedWorkspaces({}, {});
    QCOMPARE(rec.map().entryFor(id(1)).name, QString());
    QCOMPARE(renameSpy.count(), 1);
    QCOMPARE(renameSpy.first().at(0).toString(), id(1));
    QVERIFY(renameSpy.first().at(1).toString().isEmpty());
}

void TestWorkspaceReconciler::removalRace_signalledForPopulationOnDoomedDesktop()
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

void TestWorkspaceReconciler::foreign_pausedDuringRemovalThenReevaluatedAtSettle()
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

void TestWorkspaceReconciler::adoption_keepsKnownPopulations()
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

void TestWorkspaceReconciler::named_createEchoFifoMismatchHealedByKWinNames()
{
    using PhosphorWorkspaces::NamedWorkspace;
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.setFocusedScreen(QStringLiteral("A"));

    QList<NamedWorkspace> declarations;
    NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    declarations.append(chat);
    rec.applyNamedWorkspaces(declarations, {QString(), QString(), QString(), QString()});

    // An EXTERNAL creation races our named create and steals the FIFO match:
    // {d7} is realized under the declared name while OUR desktop {d8} (the
    // one KWin actually created with the name) adopts as external.
    rec.onKwinDesktopCreated(id(7));
    rec.onKwinDesktopCreated(id(8));
    rec.onDesktopListSettled({id(1), id(3), id(7), id(2), id(4), id(8)});
    QCOMPARE(rec.map().entryFor(id(7)).name, QStringLiteral("chat"));

    // Re-verifying against KWin's OWN name list (what the controller does
    // after every settle) moves the identity to the desktop that really
    // carries the name.
    // Raw KWin names: only the desktop KWin really named carries a string.
    const QStringList kwinNames{QString(), QString(), QString(), QString(), QString(), QStringLiteral("chat")};
    rec.applyNamedWorkspaces(declarations, kwinNames);
    QCOMPARE(rec.map().entryFor(id(8)).name, QStringLiteral("chat"));
    QVERIFY(rec.map().entryFor(id(7)).name.isEmpty());
}

void TestWorkspaceReconciler::create_secondPendingSurvivesTheFirstsSettle()
{
    // Two Creates open at once, one landing. The settle must retire ONLY the
    // op the landed desktop answered: retiring a second by arithmetic (the id
    // count grew by one, so "one create landed") left B's echo with no ledger
    // entry, which adopted B's desktop onto the FOCUSED screen and let
    // maintainScreen request a duplicate in the meantime.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy);

    // A's create lands: echo first, then the settled list.
    rec.onKwinDesktopCreated(id(3));
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    rec.onDesktopListSettled({id(1), id(3), id(2)});

    // B's Create is still open: no duplicate request, and B's slice is
    // untouched while it waits.
    QCOMPARE(createSpy.count(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 1);

    // B's echo lands on the screen that ASKED, not on the focused screen.
    rec.onKwinDesktopCreated(id(4));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_settledBeforeEchoLandsOnRequestingScreen()
{
    // The other order: both creates land and the settled list arrives with no
    // id-only echo at all (KWin's desktopCreated lost, or the refresh simply
    // won the race). Each new id consumes the oldest open Create — the same
    // FIFO order the echo path uses — and lands on the requesting screen
    // instead of being adopted onto the focused one.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy);

    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    // Both ops were consumed, so maintenance is quiet again.
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_settledMatchesByPositionNotRequestOrder()
{
    // Same settle-beats-the-echo race, but B's population change fires first,
    // so the ledger reads [Create(B), Create(A)] while KWin's list still puts
    // A's new desktop (d3, index 1) ahead of B's (d4, index 3). Matching the
    // oldest request first would hand d3 to B and d4 to A — each desktop in
    // the wrong screen's slice, and invisible afterwards because both screens
    // still end in a trailing empty. The match is by requested POSITION.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy, /*populateBFirst=*/true);

    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_threeConcurrentSettleLandOnTheirOwnScreens()
{
    // Three screens repairing their trailing empty in ONE maintenance pass,
    // all answered by a single settle with no echoes. globalPosition is taken
    // at request time and requestCreateAt does not mutate the map, so the
    // three requests record 1, 2 and 3 while the settled list puts the new
    // desktops at indices 1, 3 and 5. That uniform low drift is what breaks a
    // per-id nearest-distance match: for d5 (index 3) B is |2-3|=1 and C is
    // |3-3|=0, so C would win a slot that belongs to B and the two screens
    // would swap desktops silently, each still ending in a trailing empty.
    // Pairing by rank is immune to the drift.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    rec.setFocusedScreen(QStringLiteral("A"));

    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(2));
    current.insert(QStringLiteral("C"), id(3));
    rec.adoptAll({id(1), id(2), id(3)}, current);
    QCOMPARE(createSpy.count(), 0);

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 3);

    rec.onDesktopListSettled({id(1), id(4), id(2), id(5), id(3), id(6)});

    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("C")).size(), 2);
    // Every op was consumed, so maintenance asks for nothing more.
    QCOMPARE(createSpy.count(), 3);
}

QTEST_GUILESS_MAIN(TestWorkspaceReconciler)
#include "test_workspace_reconciler.moc"
